# Unit-testing strategy

Tests live next to the component they exercise, for example
`components/stratum/test`. A test body should have one source of truth even
when it runs in more than one environment.

## Test layers

| Layer | Runs in | Primary purpose |
| --- | --- | --- |
| Host unit | Native GCC and Clang | Fast, deterministic tests of portable production code with sanitizers and strict warnings |
| QEMU integration | ESP-IDF ESP32-S3 image | ESP-IDF component wiring, target compilation, FreeRTOS/ESP behavior, and integration that does not require real peripherals |
| Device integration | Physical ESP32-S3 | ESP behavior that QEMU cannot reproduce reliably |
| Hardware end-to-end | Complete miner | ASIC communication, clocks, GPIO, thermal behavior, power behavior, and full mining flows |

Portable Unity tests currently run on both the host and QEMU. This overlap is
intentional: the same assertions provide fast host feedback and verify that the
code still builds and behaves under ESP-IDF. Do not copy a test into a separate
host-only file merely to run it natively.

Reserve scarce miner hardware for end-to-end coverage. Prefer host tests for
pure transformations and state machines, and QEMU for ESP-IDF integration.

## How the host suite works

The native harness in `host-tests` compiles the existing Unity test bodies and
the production C sources they exercise:

- `host-tests/CMakeLists.txt` explicitly lists host-eligible test files,
  production sources, include paths, and pinned dependencies.
- `host-tests/include/host_test_compat.h` adapts ESP-IDF's `TEST_CASE` macro to
  the native runner. Test bodies remain shared with ESP-IDF.
- Code under `host-tests/include` and `host-tests/src` provides narrow ESP
  compatibility surfaces. It exists only in the host build and must not
  reimplement domain behavior.
- Production modules use link-time platform ports instead of host-only compile
  switches for behavioral dependencies. Simple compatibility surfaces use the
  general compatibility code under `host-tests`; for example,
  `sv1_protocol.c` is shared unchanged while each build supplies the
  appropriate `esp_log.h` and implementation.
- `tools/test_inventory.py` discovers active `TEST_CASE` declarations,
  classifies their host eligibility, detects duplicate names and conflicting
  environment tags, and verifies host CMake registration.
- `tools/run_host_tests.sh` is the supported entry point. It configures, builds,
  and runs the suite with AddressSanitizer and UndefinedBehaviorSanitizer by
  default.

The host build enables warnings as errors and probes additional warnings before
using them. A source-specific exception must have a comment explaining why it
is necessary and should be removed. The imported `base58.c` implementation
currently has the only `-Wvla` exception.

The first configure downloads Unity, cJSON, and Mbed TLS at revisions pinned in
`host-tests/CMakeLists.txt`. Later runs reuse the CMake build directory.

## Running host tests

Run the complete native suite from the repository root:

```sh
./tools/run_host_tests.sh --all
```

Run a focused test by a unique name or tag substring while developing:

```sh
./tools/run_host_tests.sh '[mining]'
./tools/run_host_tests.sh 'Validate merkle root calculation'
```

Exercise both supported compilers when changing the harness or portability
boundaries:

```sh
CC=gcc ./tools/run_host_tests.sh --all
CC=clang ./tools/run_host_tests.sh --all
```

Sanitizers should remain enabled for normal development and CI. They can be
disabled temporarily to diagnose a toolchain problem:

```sh
ESP_MINER_HOST_SANITIZERS=OFF ./tools/run_host_tests.sh --all
```

Run the inventory independently with:

```sh
python3 tools/test_inventory.py --check
```

After the root ESP-IDF project has been configured, the same suite is exposed
through its build frontend:

```sh
idf.py host-test
```

## Measuring native coverage

The host suite is the source of code-coverage metrics. Coverage runs use GCC's
gcov instrumentation in a build separate from the sanitizer builds. QEMU and
hardware tests remain integration checks; they are not mixed into the native
coverage percentage.

Install the reporter once in the Python environment from which you run the
tests:

```sh
python3 -m pip install -r host-tests/requirements.txt
```

Then run coverage through the ESP-IDF build frontend or directly:

```sh
idf.py host-coverage
# Equivalent standalone entry point:
./tools/run_host_coverage.sh
```

The command always runs the complete native suite and writes these ignored
build artifacts under `build/host-coverage/coverage`:

- `coverage.txt`: per-source line summary.
- `index.html`: browsable line and branch details.
- `coverage.json`: gcovr's machine-readable report for CI tooling.
- `gcovr-summary.json`: gcovr's standard per-file and aggregate totals.
- `coverage-summary.txt`: breadth, depth, and non-regression gate results.
- `coverage-summary.json`: machine-readable breadth, depth, file lists, and gates.
- `coverage-summary.md`: the summary rendered in the GitHub Actions job page.

The report inventories every C and C++ production source under `components`
and `main`, including files that are not part of the native test executable.
An uncompiled file has no compiler metadata identifying its executable lines,
so gcovr displays it as uninstrumented (`--%`). This is intentionally distinct
from a compiled file whose executable lines were never reached, which displays
as 0% and contributes to the coverage totals.

The source-file instrumentation breadth is the number of report files with
compiler coverage points divided by the complete eligible source inventory.
The foundation baseline is 9 of 76 files (11.8%). The inventory is discovered
automatically rather than enumerated: every C or C++ source under `components`
and `main` is eligible, subject only to the explicit exclusions below. The
current inventory matches the repository-owned sources registered in the
ESP-IDF firmware build. Keeping the filesystem scan slightly stricter also
prevents a source from disappearing from the denominator merely because it was
accidentally removed from an ESP-IDF component registration.

The native job deliberately does not parse the ESP-IDF component graph; doing
so would require a complete ESP-IDF installation merely to run portable host
tests. If repository-owned firmware sources are introduced outside
`components` or `main`, add the new source root to `tools/run_host_coverage.sh`.
Generated build outputs remain outside the inventory.

The instrumented numerator is also derived, not configured separately. A
production source registered with the native target in
`host-tests/CMakeLists.txt` appears as instrumented when gcov produces coverage
points for it. This means normal test additions change the metric without
editing a coverage manifest. Host production sources remain explicit in CMake
because only portable modules, with platform adapters selected at link time,
belong in the native executable.

Test bodies, host shims, downloaded dependencies, `node_modules`, and these
third-party source copies are excluded:

- The `components/libsecp256k1` submodule.
- Espressif's copied `components/dns_server/dns_server.c` implementation.
- The copied libbase58 implementation in `components/stratum/base58.c`.
- The copied reference implementation in `components/stratum/segwit_addr.c`.

Keep the exclusions explicit and narrow. A new repository-owned production
source is included automatically and should remain visible even before it can
be compiled by the host harness. A newly copied third-party implementation
must add a documented exclusion; do not exclude first-party code to improve a
percentage.

After removing third-party sources from the measured set, CI enforces two
different kinds of non-regression floor:

- Source-file instrumentation breadth: 11.8% (9 of 76 eligible files).
- Coverage depth within instrumented files: 58% line and 49% branch coverage.
- `components/stratum/sv1_protocol.c`: 100% line and function coverage.

These are baselines, not quality targets. New or changed portable behavior
should be covered, and thresholds should only move upward as gaps are closed.
The SV1 protocol file has its own non-regression gate because its parser and
encoders form the compatibility boundary for later ASIC job changes. Branch
coverage remains visible in the report and should improve as meaningful input
paths are added; compiler-generated and unreachable defensive edges are not
part of the per-file 100% gate.
Local diagnostic runs can override the floors with
`ESP_MINER_COVERAGE_MIN_BREADTH`, `ESP_MINER_COVERAGE_MIN_LINE`, and
`ESP_MINER_COVERAGE_MIN_BRANCH`; CI must use the checked-in defaults. Breadth
is file-based and intentionally does not pretend that files have equal size;
line and branch depth remain the measures of exercised behavior.

## Test classification and tags

A normal Unity test is host-eligible by default. Its tags describe the feature
under test, such as `[stratum]`, `[mining]`, or `[asic-job]`.

These tags exclude a test from the host suite and classify the environment it
needs:

- `[qemu-integration]`: requires the ESP-IDF/QEMU environment.
- `[device-integration]`: requires a physical ESP32-S3.
- `[hardware]`: requires miner hardware or an ASIC.

Use `[not-on-qemu]` when an ESP-IDF test must be skipped by the QEMU runner.
This tag alone does not exclude a portable test from the host suite. Device and
hardware tests should normally include both their environment tag and
`[not-on-qemu]`.

All tests in one C source file must have the same environment classification.
Put QEMU, device, and hardware tests in dedicated `test_*.c` files instead of
mixing classifications or combining them with portable host tests. This is
necessary because CMake compiles whole source files, not individual `TEST_CASE`
declarations.

## Adding a portable unit test

1. Put the test beside its component. Add it to an existing
   `components/<component>/test/test_*.c` file, or create a new one.
2. Give every `TEST_CASE` a repository-unique, behavior-oriented name and a
   feature tag. Keep the fixture deterministic; do not depend on wall-clock
   time, a network, task scheduling, or real peripherals.
3. Exercise the public production API and assert observable results. Include
   boundary values, malformed input, ownership transfer, and failure behavior
   where relevant. Release every resource allocated by the test or production
   call so LeakSanitizer remains useful.
4. If this is a new test file, add it to the `esp_miner_host_tests` executable
   in `host-tests/CMakeLists.txt`. Add any production source, include directory,
   or target library required to exercise it.
5. Ensure the component test directory has an ESP-IDF `CMakeLists.txt`. If this
   is a new component, add its name to `TEST_COMPONENTS` in both
   `test/CMakeLists.txt` and `test-ci/CMakeLists.txt`.
6. Run the inventory, the focused test, and the complete native suite. Run QEMU
   too when the change affects ESP-IDF integration or shared test behavior.

Minimal example:

```c
#include "unity.h"
#include "foo.h"

TEST_CASE("foo preserves the submitted job id", "[foo]")
{
    foo_job_t job = {.job_id = 42};

    TEST_ASSERT_EQUAL_UINT32(42, foo_job_id(&job));
}
```

For a new test directory, the ESP-IDF registration normally looks like:

```cmake
idf_component_register(
    SRC_DIRS "."
    INCLUDE_DIRS "."
    REQUIRES unity foo
)
```

The native harness must compile real production code. If an ESP header prevents
that, use this order of preference:

1. Move the pure transformation behind a small portable API.
2. Inject the platform operation through a narrow interface.
3. Add the smallest compatible declaration or behavior to a host shim.
4. Put genuinely platform-specific behavior, such as transport or peripheral
   access, in a separate adapter source file selected by the build.

Do not use a host-only compile definition to carve a portable subset out of a
larger production source file. Split the portable core from its platform
adapter instead. Compile guards remain appropriate for narrow compiler or
operating-system compatibility details, not architectural dependency removal.

Do not reproduce the expected production algorithm in a shim, remove assertions
to make a host test pass, or add a second implementation path that bypasses the
interface being tested.

Before committing a host-test change, run:

```sh
python3 tools/test_inventory.py --check
./tools/run_host_tests.sh '[affected-tag]'
./tools/run_host_tests.sh --all
./tools/run_host_coverage.sh
```

## Adding an environment-specific test

Put the test in a dedicated source file and give it the appropriate
`[qemu-integration]`, `[device-integration]`, or `[hardware]` tag. Do not list
that file in `host-tests/CMakeLists.txt`. Run the inventory check to verify that
it was not accidentally registered with the host suite.

For device and hardware tests, also add `[not-on-qemu]` unless the test has a
useful QEMU path. Document required boards, ASICs, wiring, configuration, and
pass criteria next to the fixture.

## QEMU tests

The QEMU test application is `test-ci`. Run it with:

```sh
bash tools/run_qemu_tests.sh
```

Under the hood, the script builds the ESP32-S3 test image, merges its flash
image, and runs it with `qemu-system-xtensa`. Tests tagged `[not-on-qemu]` are
excluded by `test-ci/main/unit_test_all.c`.

## Physical ESP32-S3 tests

Build the physical-target test image with:

```sh
cd test
idf.py build
```

Flashing this image replaces the normal ESP-Miner firmware and AxeOS UI. Use a
dedicated test device when possible and be prepared to restore a normal build.
Follow the generated `idf.py` flash instructions for the selected serial port,
then monitor the result with:

```sh
idf.py -p /dev/ttyACM0 monitor
```

For additional target-test details, see the
[ESP-IDF unit-test guide](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/unit-tests.html).

## CI expectations

Every pull request runs the native suite under GCC and Clang with sanitizers,
enforces native line and branch coverage floors, runs the existing ESP32-S3
QEMU lane and firmware build, and runs frontend tests. A local host run is the
minimum pre-commit check; changes to portable C behavior should also run native
coverage, changes to platform integration should also be exercised in QEMU,
and hardware changes require documented physical validation.
