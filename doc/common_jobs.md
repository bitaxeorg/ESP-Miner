# Common ASIC jobs

This change depends on [PR #1969](https://github.com/bitaxeorg/ESP-Miner/pull/1969)
merging first. The PR branch starts from its head
`9af07d7c57c38ec3532a2a5ffd1e700a03159003`. It adapts the common-jobs step
of the refactor plan to that QEMU characterization base.

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

## Downstream compatibility

Bonanza 1002/BZM adopted this common-job interface unchanged in
[`a6ee24e4`](https://github.com/johnny9/ESP-Miner-Bonanza/commit/a6ee24e4).
The `asic_job.h` declaration and header encoder are byte-identical, and
`ASIC_send_job()` retains its void return and borrowed-pointer contract.
Bonanza keeps generation, original version, clean-job state, retry decisions,
and engine assignments in its internal bookkeeping. These do not require
additional fields or parameters in common work.

On 2026-09-14, that downstream integration passed 12 Stratum V1, seven SV2
standard, seven SV2 extended, and three focused pool-fallback Python tests on
physical Bonanza hardware. Those runs validate use of the same interface in
Bonanza; they are separate from validation of this upstream implementation.
The [downstream validation record](https://github.com/johnny9/ESP-Miner-Bonanza/blob/84765393/doc/asic-common-interface.md)
records the exact firmware, testcode version, and remaining coverage limits.

This step preserves upstream's existing Bitmain result/storage path. Common
results, timestamp-rolling permissions, driver capabilities, and self-test
refactoring belong to later changes. No BZM driver or BZM-specific common-job
fields are introduced here.

## Validation

Revalidated on 2026-09-14 after rebasing onto #1969 head `9af07d7c`:

- ESP-IDF 6.0.2 / ESP32-S3 QEMU: **136 tests, 0 failures, 0 ignored**.
  All nine common-job tests ran (one header, five builder, three adapter).
- Full ESP32-S3 application and embedded web UI build passed with mDNS 1.12.0;
  35% app partition space free.
- CI's fresh resolution selected mDNS 1.13.0, which fails in
  `mdns_receive.c:704` with GCC 15.2's `-Werror=stringop-truncation`. The manifest
  now pins mDNS 1.12.0 so clean checkouts select the working release without a
  local dependency lock or suppressed compiler diagnostics.
- The same compiler failure was reproduced with mDNS 1.13.0 on upstream
  `1df7ba1e`, immediately before the #1914 swarm merge. Reverting that merge
  would retain the failing dependency resolution.
- The common-job header and encoder match the original proposal and Bonanza
  byte for byte. `git diff --check` passes.

Current local logs are in `build/stage03-pr-validation/`. No physical miner was
flashed or exercised for this upstream PR; the downstream runs above validate
the interface in Bonanza, not this Bitmain implementation. Native coverage was
not run for this QEMU-based stage.

### Earlier characterization comparison

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

That earlier comparison validated the QEMU test firmware only. The current
application build and downstream hardware evidence are recorded separately above.
