# Native host tests

This suite compiles the existing Unity test bodies and their production C
sources for Linux. It does not require ESP-IDF, an ESP32 image, or QEMU.

Read the canonical [unit-testing strategy and contribution
guide](../doc/unit_testing.md) before adding tests, production sources, shims,
or environment tags to this harness.

From the repository root, run the supported developer entry point:

```sh
./tools/run_host_tests.sh
```

After the ESP-IDF project has been configured, the same full suite is also
available through its build frontend:

```sh
idf.py host-test
```

Pass a Unity test name or tag substring for a focused development run:

```sh
./tools/run_host_tests.sh '[mining]'
```

Generate native production-code coverage with GCC and gcovr 8.6:

```sh
python3 -m pip install -r host-tests/requirements.txt
./tools/run_host_coverage.sh
# Or, after configuring the ESP-IDF project:
idf.py host-coverage
```

Text, detailed HTML, and machine-readable JSON reports are written to
`build/host-coverage/coverage`. The generated `coverage-summary.txt` and
`coverage-summary.json` separate source-file instrumentation breadth from line
and branch depth within instrumented files. See the canonical guide for source
selection, CI floors, explicit third-party exclusions, and the distinction
between 0% compiled coverage and uninstrumented (`--%`) production files.

Dependencies are fetched at exact revisions during the first CMake configure.
The default build enables AddressSanitizer and UndefinedBehaviorSanitizer.
