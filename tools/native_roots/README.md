# SDK certificate-bundle regression

Run `python3 tools/test_native_roots.py --idf-path "$IDF_PATH" --output "$PWD/native-results"` with a new output directory. Requirements: Python 3, CMake, a C compiler with pthreads, and `cryptography` for the SDK certificate generator. Use an unmodified ESP-IDF v6.0.2 checkout with its mbedTLS submodule initialized. The runner checks SDK commit `7101770dc6db2667b3c477cc31365dd1acd6db4e` and mbedTLS commit `6cc42afad309e861f4c07e6f106e2ab14a9cb8e5`. It compiles their native libraries and exact SDK bundle implementation with platform/logging shims; it never opens sockets, serial ports, or devices or modifies SDK sources.

The production fix retains `components/pinned_roots` on Stratum's system-certificate path. It parses only approved real DER roots and lets native mbedTLS make all trust decisions. The SDK's cross-sign option solves the chain-building case, but ESP-IDF 6.0.2 leaks synthetic-root subject buffers on every successful lookup and mixes libc allocation with mbedTLS cleanup. A config-only enable is therefore unsafe with this release. No SDK files are patched or overridden, and custom-CA/TCP modes are unchanged. `baseline.c` dispatches through the CA callback when installed, preserving the configured profile and verification callback, rather than incorrectly using only `conf.ca_chain`.

## Frozen fixtures

Public chains were captured on **2026-10-30 UTC**, using OpenSSL 3 `s_client -showcerts -connect HOST:PORT -servername HOST`:

| Fixture | Provenance / purpose | Stock | Cross-sign |
| --- | --- | --- | --- |
| `braiins.com-full.pem` | `braiins.com:443`: leaf → WE1 → GTS R4 cross-signed by legacy GlobalSign (absent from pinned bundle) | reject | accept |
| `braiins.com-short.pem` | Same capture, dropping only its final certificate | accept | accept |
| `public-pool.io-full.pem` | `public-pool.io:4333`: leaf → Let's Encrypt YE2 → Root YE → ISRG Root X2 cross-signed by X1 | accept | accept |
| `attacker-full.pem` | Synthetic braiins.com leaf signed by a same-subject, different-key GTS R4 impostor | reject | reject |
| `bad-signature.pem` | Full Braiins chain, last DER byte of leaf signature XOR 1 | reject | reject |
| `self.pem` | Synthetic self-signed braiins.com leaf; tested with its actual hostname | reject | reject |

Braiins Stratum endpoints are plain TCP; its **website** is only a neutral chain-building fixture. These fixtures do not imply firmware connections to the named websites. Synthetic certificate keys were generated solely for these fixtures and were never serialized. `fixtures/sha256.json` binds the exact PEM files. The attacker root uses the captured GTS R4 subject but a freshly generated P-256 key, not Google's key.

Both SDK configurations use identical compact bundle bytes generated from the existing `certificates/cacert-2026-10-20.pem`; the custom helper uses those same 118 approved roots as real DER. The custom helper agrees with SDK cross-sign on the table above. All three modes reject all 12 chains from the existing checksum-bound incident ZIP, without committing unpacked copies. Wrong-host checks on otherwise trusted chains require the hostname-mismatch bit, not merely a transport or trust error. All successful verifications require zero flags. The native build follows firmware's inherited disabled time/date policy; archived fixture validity is not live endpoint acceptance.

## Ownership regression

`lifecycle.c` tracks SDK libc and mbedTLS allocations separately from cold PSA initialization through complete peer/configuration/bundle/PSA cleanup. Each invocation verifies 1 or 100 times in the same process. Counts are never reset, PSA lazy key slots are not mistaken for leaks, and process exit cannot mask residual allocations. Unknown frees fail the harness. Stock and production custom modes must have zero residual allocations and zero cross-domain frees, with no retained growth between the first and last verification. Custom lifecycle checks cover all 21 positive/negative cases; all-root selection/failure-unwind sweeps, complete-verification allocation sweeps and concurrent client tests also run.

The pinned SDK cross-sign mode is an explicitly labeled **failing lifecycle control**, not a production acceptance target: the probe exits 70 for residual ownership. Braiins loses 7 allocations / 120 bytes per verification; public-pool loses 7 / 136 bytes. The runner requires linear growth from 1 to 100 calls and reports libc-to-mbedTLS cross-domain frees separately. Its suite-level success means this known SDK failure was reproduced AND production remained leak-free; it does not approve SDK cross-sign mode.

In the SDK callback, `subject_raw.p` and each subject OID/value are independently allocated, but native certificate cleanup normally owns one raw DER buffer and only shallow-frees parsed-name nodes. The SDK also allocates the synthetic certificate/name nodes with libc although native cleanup uses the configured mbedTLS allocator (external memory in this firmware). ESP-IDF v6.0.3 source removes the independently owned subject buffers, but still uses libc allocation for certificate/name nodes. The maintained `release/v6.0` source also switches those allocations to the mbedTLS domain. An SDK upgrade is a separate compatibility/build qualification, not a config-only fix for pinned v6.0.2. Replacing private static SDK callbacks or copying its full implementation would add source-management burden rather than simplify this scoped patch.

`commands.log` and `results.json` are local evidence, not committed outputs. Native verifier tests are distinct from configured ESP SSL transport execution and full ESP32-S3 builds. Local Darwin execution does not claim Linux CI has run. None establishes device heap/PSRAM, task-stack headroom, or real pool operation, or authorizes installation.
