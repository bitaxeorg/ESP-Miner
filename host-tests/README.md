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

Dependencies are fetched at exact revisions during the first CMake configure.
The default build enables AddressSanitizer and UndefinedBehaviorSanitizer.
