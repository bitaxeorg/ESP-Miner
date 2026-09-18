## Unit Testing
ESP-Miner includes unit tests that can run on the target (the esp32s3).
Tests are located in the `test` subdirectory of a component (e.g. `components/stratum/test`).

For more information on unit testing with the esp32s3, see https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32/api-guides/unit-tests.html.

### Building
To built unit tests (examples provided on Ubuntu 24.04), from the ESP-Miner root directory:
```
cd test
idf.py build
```

### QEMU and UBSan

With ESP-IDF 6.0.2 sourced and `qemu-system-xtensa` on `PATH`, run the ordinary
suite or enable UndefinedBehaviorSanitizer (UBSan):

```sh
bash tools/run_qemu_tests.sh
bash tools/run_qemu_tests.sh --ubsan
```

CI runs both variants on every push and pull request as `build-and-test` and
`build-and-test-ubsan`. Each check fails on a test failure, UBSan report, or
missing or malformed test report. The `test-results` and `test-results-ubsan`
artifacts retain the test report, serial log, and ELF for decoding backtraces.

The local runner uses separate build directories (`.cache/qemu-test-build` and
`.cache/qemu-ubsan-test-build`) and generated sdkconfig files for each mode.
`ESP_QEMU_BUILD_DIR` overrides the build directory. `QEMU_TIMEOUT` overrides the
five-minute emulator timeout. The runner prints serial output on failure and
requires a passing Unity summary.

Direct ESP-IDF builds in `test` or `test-ci` can enable UBSan with
`idf.py -D ENABLE_UBSAN=ON build`; the CMake option defaults to `OFF`.
Instrumentation covers repository components and their test libraries.
ESP-IDF components remain uninstrumented to limit RAM usage. Alignment and
shift-base checks are enabled. Firmware and both test apps share the cJSON
allocator, which requests the type's required alignment and uses PSRAM when
available. Stratum timing records also request their required alignment.

Check the local runner without building firmware:

```sh
python3 -m unittest discover -s tools/tests -p 'test_qemu_runner.py'
```

### Flashing
**NOTE: Flashing the unit test binary will replace the existing firmware on the ESP32. For example, you will no longer have access to the AxeOS web UI and must flash a release or build and flash a non-test binary to recover. Do not attempt to do this unless you are willing to spend time recovering (or have dedicated test devices)**

At the conclusion of the build, instructions are provided to flash it. To ensure esptool uses the correct serial port and has permission to do so, ensure the serial device of the esp32 is known (e.g. from `dmesg` output on connection) and ensure the user is in the `dialout` group.

```
sudo usermod -aG dialout $USER
```

For example, if the serial device is `/dev/ttyACM0`:
```
python -m esptool -p /dev/ttyACM0 --chip esp32s3 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 2MB --flash_freq 80m 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x10000 build/unit_test_stratum.bin
```

To view test output, the serial monitor can be used:
```
idf.py -p /dev/ttyACM0 monitor
```
(`CTRL-]` can be used to stop the monitor)

### Adding a Unit Test
In the following example, a unit test is added to the `foo` component.

```
$ ls -R components/foo
components/foo:
CMakeLists.txt  foo.c  include

components/foo/include:
foo.h
```

components/foo/CMakeLists.txt:
```
idf_component_register(
SRCS
    "foo.c"

INCLUDE_DIRS
    "include"

REQUIRES
)
```

components/foo/include/foo.h:
```c
#ifndef FOO_H
#define FOO_H

int foo(int i);

#endif // FOO_H
```

components/foo/foo.c:
```c
#include "foo.h"

int foo(int i) {
    return i;
}
```

A directory `components/foo/test` is created, with its test file and `CMakeLists.txt`

components/foo/test/CMakeLists.txt:
```
idf_component_register(SRC_DIRS "."
                    INCLUDE_DIRS "."
                    REQUIRES unity foo)

```

components/foo/test/test_foo.c:
```c
#include "unity.h"
#include "foo.h"

TEST_CASE("Foo returns what is provided", "[foo]")
{
    TEST_ASSERT_EQUAL_INT(42, foo(42));
}

```

The unit test application's `test/CMakeLists.txt` is modified to include `foo` in the test binary:
```diff
-set(TEST_COMPONENTS "bm1397 stratum" CACHE STRING "List of components to test")
+set(TEST_COMPONENTS "bm1397 stratum foo" CACHE STRING "List of components to test")
```

Build, flash, and monitor the test binary. Output from the new test should be present.
```
#### Running all the registered tests #####

Running Foo returns what is provided...
/home/dev/myrepos/ESP-Miner/components/foo/test/test_foo.c:4:Foo returns what is provided:PASS
...
```
