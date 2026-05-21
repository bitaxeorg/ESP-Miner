# GridPool Direct block submit and UI notes

Date: 2026-05-20

## Block submission path

- GridPool Direct jobs now retain the selected raw transaction bytes from the Bitcoin RPC template.
- If a direct ASIC result meets the Bitcoin network target, firmware serializes the solved header, witness coinbase, transaction count, and selected template transactions into a full block and posts it to the configured Bitcoin RPC with `submitblock`.
- The block is streamed as JSON-RPC hex to avoid allocating a full block-sized hex string on the ESP32.
- `submitblock` telemetry is exposed through `/api/system/info` as `gridpoolBlock*` fields.

## Work validity review

- The merkle root is still built from the legacy coinbase txid plus the template txids, as required for the block header.
- The submitted block uses the full witness coinbase and includes the `default_witness_commitment` output from `getblocktemplate`.
- The builder now parses each template transaction's raw `data` and `weight`, keeps transaction order, and trims only from the tail when estimated block weight would exceed `weightlimit`.
- The block-weight estimate includes header weight, transaction-count varint weight, full coinbase weight, and selected transaction weights.
- Host validation passed against Bitcoin Core proposal mode with the firmware tag, 8-byte extranonce shape, GridPool outputs, and the configured payout address:
  - `proposal_accepted: true`
  - `proposal_result: null`
  - `coinbase_weight: 1320`
  - `selected_tx_count: 2300`
  - `dropped_tx_count: 0`
  - `estimated_block_weight: 2230001`

## Deployed live check

Device: `192.168.1.214`

- Firmware and web UI deployed from `build-gridpool-m2`.
- API reported matching firmware/AxeOS version: `v2.14.0b2-1-g5d56ca4-dirty`.
- GridPool split mode was mining direct work and shares were accepted:
  - `gridpoolShareAccepted: 30`
  - `gridpoolShareRejected: 0`
  - `gridpoolTemplateTxCount: 294`
  - `gridpoolTemplateBytes: 219297`
  - `gridpoolTemplateTotalMs: 2506`
  - `gridpoolTemplateParseMs: 219`
  - `gridpoolTemplateBuildMs: 2`
  - `freeHeapInternal: 74523`
  - `freeHeapSpiram: 7347816`
  - `hashRate: ~1.07 TH/s`
  - `temp: ~67 C`

The live node currently returned a relatively small template, so the retained raw transaction data cost was modest. The code path is sized for larger templates via PSRAM-backed allocations, and full-template proposal validation was performed from the host side.
