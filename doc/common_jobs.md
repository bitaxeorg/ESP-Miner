# Common ASIC jobs

This change starts from PR #1969 at
`7caf30f909f29ab776bc3d270c61f6e919ecedf5` on branch
`upstream-refactor/03-common-jobs-pr1969`. It adapts the common-jobs step
of the refactor plan to the QEMU characterization base.

`asic_job_t` owns the Bitcoin header fields and submission metadata. Hash
arrays hold header bytes; the header encoder writes integers explicitly in
little endian. The accepted 31-character job ID and 32-byte extranonce2
limits and full pool ID range are preserved.

The create-jobs task builds common work through `mining_build_asic_job()`
and lends it to `ASIC_send_job()` for one call. The Bitmain adapter creates
an independent `bm_job`, including metadata and software midstates, before
transferring ownership to the existing send functions. Failed allocations
release partial work. The producer frees its common job after the call.

Bitmain send functions, result processing, retention, rolling decisions,
and submission policy remain unchanged. The existing constructor remains
available for compatibility tests. Coinbase allocation failure retains the
existing zero-hash behavior. The adapter adds one short-lived allocation
per generated job.

## Validation

Run on 2026-09-12 with ESP-IDF 6.0.2 and Espressif QEMU
`esp-develop-9.2.2-20260417`, using the ESP32-S3 machine:

- Unmodified #1969 baseline: **127 tests, 0 failures, 0 ignored**.
- Refactored suite: **136 tests, 0 failures, 0 ignored**.
- All nine added tests executed, including the common header test.
- `git diff --check` passes.

The suite retains the runner's `[not-on-qemu]` exclusion. New cases cover
header byte order, owned metadata, invalid metadata, extranonce lengths,
allocation failure and recovery, and conversion compatibility across all
three job protocols and software-midstate counts 0, 1, 4, and 5. Existing
pipeline golden values, work packets, version rolling, and result tests
also pass through their real production code. The pipeline harness now
includes the real common-job builder and send adapter.

From a fresh worktree, initialize submodules, source ESP-IDF, put the QEMU
binary on PATH, and run `bash tools/run_qemu_tests.sh` from a terminal.
For an existing test build, refresh the cached component list first:

```sh
idf.py -C test-ci -D 'TEST_COMPONENTS=stratum stratum_v2 asic mining_job' reconfigure
bash tools/run_qemu_tests.sh
```

The local run requires a PTY to capture UART output. Check the Unity summary,
not just QEMU's exit status. Both baseline and candidate print `OK` before
the existing runner's `exit(0)` triggers ESP-IDF's abort/reboot path.
Local logs are in `build/common-jobs-validation/`.

These results validate the QEMU test firmware. Physical mining comparisons,
full application builds, and native coverage were not run for this change.
Hardware evidence remains a separate delivery requirement in the plan.
