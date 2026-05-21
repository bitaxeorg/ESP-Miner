# GridPool Direct Milestone 3 End-To-End Measurements

Date: 2026-05-20

Target:

- Bitaxe Gamma at `192.168.1.214`
- GridPool API: `https://gridpool.net`
- Bitcoin Core RPC: LAN node at `http://192.168.1.169:8334`
- Firmware mode: GridPool Direct

Result:

- Stratum protocol task is stopped when GridPool Direct is enabled.
- Firmware fetches GridPool payouts and share advice from Boot.
- Firmware fetches `getblocktemplate` from the LAN Bitcoin node.
- Firmware builds direct `bm_job` work and sends it to the BM1370 ASIC.
- ASIC nonces are scored locally with the existing `test_nonce_value` path.
- Valid direct shares are submitted to `POST /api/mining/share`.
- GridPool returns accepted responses for submitted mainnet shares.

Key implementation notes:

- Share proof submission uses the exact `bm_job` header layout used for local nonce scoring.
- Share proof submission sends the legacy coinbase serialization so Boot hashes the same txid committed into the header merkle root.
- Accepted shares request a direct-work refresh because GridPool payout outputs can change after an on-deck update.
- Stale work is skipped locally after a refresh request instead of being submitted and rejected.
- GridPool HTTPS calls are serialized with a mutex to avoid concurrent TLS setup pressure on internal heap.
- Full template refreshes fetch fresh GridPool payouts before building work.
- When the block tip is unchanged, payout refresh can update the direct context without fetching a new full template.

Representative post-fix sample:

| Metric | Value |
| --- | ---: |
| Uptime | `261 s` |
| Template height | `950229` |
| Template bytes | `1295273` |
| Template tx count | `1158` |
| Template payout fetch | `1749 ms` |
| Template RPC fetch | `1894 ms` |
| Template parse | `804 ms` |
| Per-work build | `2 ms` |
| Full template total | `4548 ms` |
| Direct jobs sent | `236` |
| GridPool accepted | `54` |
| GridPool rejected | `0` |
| Stale/rate skipped | `152` |
| Hashrate 1m | `1090.6 GH/s` |
| CPU | `37.5%` |
| Free internal heap | `39467 bytes` |
| Free SPIRAM heap | `7401976 bytes` |
| ASIC temp | `63.9 C` |
| VR temp | `73 C` |

Validation history:

- Initial share submissions failed with `Coinbase transaction does not match the header merkle root`.
  - Fixed by serializing the submitted header from the active `bm_job` and submitting legacy coinbase bytes for proof hashing.
- A later run produced `Coinbase winners payouts do not match the required Boot outputs`.
  - Fixed by invalidating old work after accepted shares or payout-sequence changes, then refreshing payouts before producing new work.
- A later run hit intermittent `ESP_ERR_HTTP_CONNECT` with `mbedtls_ssl_setup returned -0x7F00`.
  - Fixed for the current sample by serializing GridPool HTTPS GET/POST calls.

Current limitations:

- This is still a short functional run, not a soak test.
- Skipped stale shares are expected when accepted shares update GridPool payout requirements before the ASIC drains old work.
- Long-term heap trend, Wi-Fi stability, and behavior across multiple Bitcoin block tips still need Milestone 5 soak coverage.
