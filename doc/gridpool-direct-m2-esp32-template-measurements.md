# GridPool Direct Milestone 2 ESP32 Template Measurements

Date: 2026-05-20

Target:

- Bitaxe Gamma at `192.168.1.214`
- GridPool API: `https://gridpool.net`
- Bitcoin Core RPC: LAN node at `http://192.168.1.169:8334`
- Firmware task: background GridPool direct template probe
- Standard Stratum mining remained active during the test

Result:

- On-device `getblocktemplate` fetch: working
- On-device template parser: working
- On-device coinbase, merkle path, and `bm_job` build: working
- Template probe runs: `3`
- Template probe failures after parser fix: `0`
- Last observed template height: `950222`
- Last observed template transactions: `3566`
- Last observed merkle path entries: `12`
- Last observed generated coinbase transaction: `229` bytes
- Last observed GridPool outputs after slot 0: `1`

Representative successful runs:

| Run | GBT bytes | Tx count | RPC ms | Parse ms | Build ms | Total ms |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | `3991834` | `3384` | `4059` | `2366` | `328` | `6756` |
| 2 | `4046106` | `3432` | `4253` | `2308` | `336` | `6901` |
| 3 | `4104955` | `3566` | `4855` | `2356` | `347` | `7562` |

Steady-state resource sample after run 3:

- CPU: `19.0%`
- Hashrate 1m: `1080.6 GH/s`
- Hashrate 10m: `1078.5 GH/s`
- Temperature: `65.4 C`
- VR temperature: `75 C`
- Free heap: `7596772` bytes
- Free internal heap: `64179` bytes
- Free SPIRAM heap: `7539976` bytes
- Shares accepted: `40`
- Shares rejected: `0`

Heap observed around template builds:

- Before run 3: free heap `7600908`, internal `66063`, SPIRAM `7542288`
- After run 3: free heap `7600708`, internal `66067`, SPIRAM `7542084`
- During full-template fetch/build sampling, free heap temporarily dropped as low as about `3.2 MB`, almost entirely from SPIRAM.

Notes:

- The first ESP run failed at `previousblockhash` because the fixed-length JSON string scanner rejected exact-fit 64-character strings. The scanner now accepts exact-fit hashes and txids while still rejecting overflow.
- The RPC response was complete during the failure (`3968359/3968359` bytes), so the fix was parser-side, not transport-side.
- Current direct-template mode is still a probe: it builds and discards the `bm_job` while normal Stratum mining continues.
- The major remaining constraint is transient PSRAM pressure from holding the full JSON-RPC response while parsing. This is viable on the Gamma sample, but mainnet template growth can still push the direct full-GBT approach toward a compact local sidecar or streaming parser later.
