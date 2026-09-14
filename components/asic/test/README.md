# ASIC test support

These modules test job packets, ASIC responses, and result handling while using
the real production code.

## Test levels

- A **unit test** checks one function or a small module with direct inputs and
  outputs.
- A **component test** runs a driver or task with its related production code.
  Test doubles replace external hardware, timing, and service boundaries.

## How the modules fit together

1. A test case calls a test harness or a test task.
2. The harness creates the required state and test data.
3. A test instance builds the real production source with private test names.
4. A bindings header replaces hardware, timing, and service calls with test
   doubles.
5. The test checks the recorded packets, calls, state, and results.

The bindings replace only code at an external boundary. The driver and task
logic under test is unchanged.

## Support modules

| Module | Purpose |
| --- | --- |
| `bm13xx_test_harness.*` | Creates common state for BM1366, BM1368, BM1370, and BM1373 tests. It provides scripted ASIC responses and records serial packets and delays. |
| `bm13xx_test_bindings.h` | Gives each driver instance private test names. It connects serial, receive, timing, and chip setup calls to test doubles. |
| `bm1366_test_instance.c`, `bm1368_test_instance.c`, `bm1370_test_instance.c`, `bm1373_test_instance.c` | Build isolated instances of the real production drivers. |
| `bm1397_test_harness.*` | Creates BM1397 state, provides scripted ASIC responses, and records serial packets. |
| `bm1397_test_bindings.h` | Gives the BM1397 driver private test names and connects its external calls to test doubles. |
| `bm1397_test_instance.c` | Builds an isolated instance of the real BM1397 driver. |
| `result_task_test_bindings.h` | Gives the result task a private test name. It connects ASIC results, share submission, scoring, self-test, and register calls to test doubles. |
| `result_task_test_instance.c` | Builds an isolated instance of the real ASIC result task. |

## Component tests

| File | Production components and behavior |
| --- | --- |
| `test_bitmain_job_packets.c` | Runs the BM13xx and BM1397 drivers with the active-job store. It checks work packets, slot replacement, share and register responses, inactive jobs, and repeated nonces. |
| `test_version_rolling.c` | Runs the BM13xx drivers, active-job store, and SV1 and SV2 share encoders. It checks version-mask commands, driver setup, response decoding, write retries, and submitted version fields. |
| `test_asic_result_task.c` | Runs the ASIC result task with the active-job store and mining checks. It checks owned job snapshots, all job protocols, register routing, unavailable slots, share thresholds, self-test results, and repeated results. |

## Unit tests

| File | Unit behavior |
| --- | --- |
| `test_pll.c` | PLL divider selection and the calculated ASIC frequency. |
| `test_timeout.c` | ASIC timeout calculation for different chips, chain sizes, version spaces, and the zero-chip default. |

## Disabled example

| File | Purpose |
| --- | --- |
| `test_job_command.c` | A disabled hardware example for sending a BM1397 job and reading its result. It does not contain an active test. |

## Test double names

- A **fake** returns scripted input, such as an ASIC response.
- A **spy** records a call or its data so a test can check it.
- A **stub** provides a fixed or empty implementation for behavior outside the
  test.
- A **harness** owns shared test state and connects the test doubles.
- A **test instance** builds real production source for use by the tests.
