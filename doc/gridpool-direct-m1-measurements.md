# GridPool Direct Milestone 1 Measurements

Date: 2026-05-20

Probe command:

```bash
python3 tools/gridpool_m1_probe.py --json-out /tmp/gridpool-m1-probe.json
```

Environment:

- GridPool API: `https://gridpool.net`
- Bitcoin Core RPC: local mainnet node at `http://127.0.0.1:8334`
- Bitcoin tip used by template: `00000000000000000000cb047360b7cfd20abc37f7045b609034c8b7801b0b5e`
- Template height: `950181`
- Template bits: `17020f79`
- Slot-0 address used for probe: first current GridPool payout address

Result:

- Bitcoin Core `getblocktemplate` proposal result: `null`
- Proposal accepted: yes
- Full template transaction count: `1265`
- Selected transaction count: `1265`
- Dropped transaction count: `0`
- GridPool coinbase output count after slot 0: `3`
- Coinbase value: `313492451` sats
- GridPool output total: `311458134` sats
- Slot-0 value: `2034317` sats
- Coinbase txid: `641ae059548a587c36073fd98b3d6edbda62c1c4694b6975c36affc45606dcb6`
- Merkle root: `d01ebf106a2bcd2411c21772d6be789fe46a6b4b7b70f3df86e98c6145350d84`
- Merkle path entries: `11`

Timing:

- Fetch `getblocktemplate`: `20.57 ms`
- Fetch GridPool payouts: `97.38 ms`
- Build coinbase, merkle path, and header: `6.02 ms`
- Bitcoin Core proposal validation: `41.19 ms`

Size and heap-relevant estimates:

- Raw `getblocktemplate` JSON-RPC response: `1838632` bytes
- Compact template JSON body: `1838586` bytes
- GridPool payouts response: `59150` bytes
- Coinbase transaction: `284` bytes
- Block proposal assembled by probe: `787675` bytes
- Firmware txid retention for selected txs: `40480` bytes
- Merkle working level for selected txs plus coinbase: `40512` bytes
- Merkle path retained in job context: `352` bytes

Notes:

- The proposal probe mirrors the firmware builder's slot-0 coinbase, GridPool winner outputs, txid-only merkle construction, and block header serialization.
- The local node accepted the candidate without trimming transactions. If future templates are closer to the weight limit, transaction tail trimming or a local compact-template sidecar may still be needed.
- The slot-0 probe address is not a firmware configuration recommendation; production direct mode should use the user's configured payout address.
