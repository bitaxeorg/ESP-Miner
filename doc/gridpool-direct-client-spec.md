# GridPool Direct Bitaxe Client Spec

## Purpose

This document is a handoff spec for building an experimental AxeOS/esp-miner fork that lets a Bitaxe mine directly into the GridPool protocol without running Stratum V1 to a pool or DATUM gateway.

The intended user has:

- a Bitaxe running custom AxeOS firmware;
- a Bitcoin Core or Knots node on the same LAN;
- a configured payout address;
- a selected GridPool Boot node URL.

The Bitaxe should build sovereign mining work from the user's local Bitcoin node, mine that work locally, and submit sufficiently high-difficulty shares directly to GridPool.

## Product Concept

Add a new mining mode to AxeOS:

```text
Standard Stratum mode:
  Bitaxe -> Stratum pool

GridPool direct mode:
  Bitaxe -> local Bitcoin node for templates
  Bitaxe -> GridPool Boot HTTP API for high-diff share submission
```

GridPool direct mode is primarily for lottery miners. It should expose a compact "lotto mode" UI inside AxeOS rather than embedding the full GridPool web dashboard.

## Non-Goals For MVP

- Do not implement a DATUM server inside AxeOS.
- Do not implement the full GridPool web host interface inside AxeOS.
- Do not require a wallet or private keys on the Bitaxe.
- Do not submit raw Stratum V1 directly to GridPool.
- Do not trust a remote template provider.
- Do not attempt full GridPool peer relay in the first implementation.

Full relay mode can be explored later, but the first milestone should be a reliable direct share submitter.

## GridPool Protocol Knowledge Needed

GridPool currently exposes a direct HTTP share path designed for Hydrapool-style clients and other non-DATUM integrations.

Relevant Boot source paths:

- `/home/keegreil/Documents/GitHub/boot-protocol/docs/hydrapool-http-submission.md`
- `/home/keegreil/Documents/GitHub/boot-protocol/boot_portal/Controllers/MiningApiController.cs`
- `/home/keegreil/Documents/GitHub/boot-protocol/boot_portal/Models/MiningModels.cs`
- `/home/keegreil/Documents/GitHub/boot-protocol/boot_portal/Services/BootShareVerifier.cs`

### Get Current Payout List

Endpoint:

```http
GET /api/mining/payouts
```

The response includes:

- `sequence`, to detect payout list changes;
- `payouts`, the current winners list;
- `coinbaseOutputs`, the outputs the miner must include after slot 0;
- `network`, including Boot network state.

The Bitaxe must fetch this before building work and refresh it when the sequence changes, after a Bitcoin tip change, and on a conservative periodic timer.

### Get Share Submission Advice

Endpoint:

```http
GET /api/mining/share-advice
```

This endpoint exists specifically for direct clients like AxeOS. It does not provide work, templates, transaction selection, censorship policy, or anything that would make the miner less sovereign. It only reports the current GridPool on-deck admission floor.

The response includes:

- current round and state IDs;
- current tip hash/height;
- shared winner slot count;
- current on-deck count and open slots;
- current on-deck floor difficulty;
- minimum computed difficulty needed to enter the on-deck list;
- whether the share must be strictly greater than the current floor.

Firmware should use this endpoint to avoid submitting low-difficulty telemetry shares that cannot affect the on-deck list.

### Submit A Share

Endpoint:

```http
POST /api/mining/share
```

Request shape:

```json
{
  "minerAddress": "bc1q...",
  "username": "bc1q...bitaxe",
  "headerHex": "<80-byte block header hex>",
  "coinbaseHex": "<coinbase transaction hex>",
  "merklePath": ["<32-byte merkle sibling hex>"],
  "prevBlockHash": "<parent block hash>",
  "nonce": 0,
  "difficulty": 0
}
```

Important trust model:

- `minerAddress`, `username`, `nonce`, and caller-reported `difficulty` are metadata.
- GridPool recomputes the block header hash and actual difficulty.
- GridPool rebuilds the merkle root from `coinbaseHex` and `merklePath`.
- GridPool parses the coinbase transaction and attributes the share to coinbase output slot 0.
- Slot-0 address attribution is the core protocol invariant.

If an attacker changes the slot-0 payout address after work is created, the coinbase transaction changes, the merkle root changes, and the submitted header no longer validates.

## Current esp-miner Architecture Notes

The esp-miner repository is at:

```text
/home/keegreil/Documents/GitHub/esp-miner
```

Important local source paths:

- `components/stratum/stratum_api.c`
- `components/stratum/include/stratum_api.h`
- `components/stratum/mining.c`
- `components/stratum/include/mining.h`
- `main/tasks/stratum_v1_task.c`
- `main/tasks/create_jobs_task.c`
- `main/tasks/asic_result_task.c`
- `main/tasks/scoreboard.c`
- `main/global_state.h`
- `main/http_server/openapi.yaml`
- `main/http_server/axe-os/src/app/`

### Existing Stratum V1 Flow

The current firmware:

- connects to a Stratum V1 pool;
- sends `mining.subscribe`;
- sends `mining.authorize`;
- receives `mining.set_difficulty`;
- receives `mining.notify`;
- builds ASIC jobs from the notify payload;
- tests ASIC nonces locally;
- submits shares back with `mining.submit`.

Useful functions:

- `STRATUM_V1_receive_jsonrpc_line()` reads newline-delimited JSON-RPC.
- `STRATUM_V1_submit_share()` submits the normal Stratum nonce tuple.
- `calculate_coinbase_tx_hash()` builds the coinbase tx hash from Stratum coinbase pieces and extranonce.
- `calculate_merkle_root_hash()` builds the merkle root from coinbase tx hash plus merkle branches.
- `construct_bm_job()` constructs an ASIC `bm_job`.
- `test_nonce_value()` reconstructs the 80-byte block header and computes nonce difficulty.

### Existing ASIC Result Path

`main/tasks/asic_result_task.c` receives ASIC nonces, looks up the active `bm_job`, computes `nonce_diff`, and submits if `nonce_diff >= active_job->pool_diff`.

For GridPool direct mode, this is the key integration point. Instead of calling `STRATUM_V1_submit_share()`, the firmware should reconstruct the full GridPool share proof and POST it to `/api/mining/share`.

## Proposed Firmware Architecture

Add a compile-time and runtime selectable mining backend:

```text
Mining backend
  - stratum_v1
  - stratum_v2
  - gridpool_direct
```

The `gridpool_direct` backend needs these modules:

```text
gridpool_status_client
  -> fetches /api/mining/payouts

bitcoin_rpc_client
  -> fetches local node block templates

gridpool_coinbase_builder
  -> builds coinbase transaction with slot-0 payout plus GridPool winner outputs

gridpool_merkle_builder
  -> computes coinbase merkle branch/path

gridpool_job_builder
  -> converts local template into ASIC bm_job work

gridpool_share_submitter
  -> posts complete share proofs to GridPool

gridpool_ui
  -> limited lottery-mode AxeOS UI widgets
```

### Data Flow

```text
1. Fetch GridPool payout list.
2. Fetch local Bitcoin block template.
3. Build coinbase transaction:
     output 0 = user's payout address
     outputs 1..N = GridPool required winner outputs
4. Build merkle root and merkle path.
5. Build bm_job and send work to ASIC.
6. ASIC returns nonce.
7. Firmware computes share difficulty.
8. If difficulty exceeds the current `/api/mining/share-advice` threshold:
     reconstruct headerHex
     reconstruct coinbaseHex
     attach merklePath
     POST /api/mining/share
9. UI reports accepted, duplicate, or rejected response.
```

## Template Construction Design

### Direct Bitcoin RPC Mode

The purist design is for the Bitaxe to call the local Bitcoin node directly:

```text
Bitaxe -> Bitcoin Core/Knots JSON-RPC getblocktemplate
```

Required RPC capabilities:

- fetch current parent block hash;
- fetch block version;
- fetch compact target/nbits;
- fetch current time;
- fetch transaction txids and any data needed to compute the merkle path;
- build a valid coinbase transaction;
- refresh quickly on new tips.

Concern:

`getblocktemplate` responses can be large. A full block template may include thousands of transactions. Parsing and storing that on an ESP32-class device may be too heavy, especially while mining and running the AxeOS UI.

Rough sizing estimate:

- Bitcoin Core documents that `getblocktemplate` returns a `transactions` array where each entry includes byte-for-byte transaction hex plus txid, witness hash, dependency, fee, sigop, and weight metadata.
- A full mainnet block is bounded by block weight, but transaction hex alone can still be several megabytes because binary transaction data is hex-encoded.
- JSON object overhead, tx metadata, allocation overhead, and parser buffering can push a large template well beyond the raw block-data size.
- This is plausible trouble for ESP32-class memory, especially if the implementation builds a full DOM rather than streaming only the fields it needs.

Recommendation:

- Test direct `getblocktemplate` against real mainnet templates, because testnet is not useful for mining behavior right now.
- Measure heap use with current large mainnet templates.
- Prefer a streaming parser or txid-only retention strategy if possible.
- Do not assume direct full-template parsing will be viable until measured.

### Practical Sovereign Sidecar Mode

If direct Bitcoin RPC is too heavy, use a tiny open-source LAN sidecar on the Bitcoin node.

```text
Bitaxe -> local template sidecar -> Bitcoin Core/Knots
```

The sidecar would:

- run on the user's own Bitcoin node machine;
- call `getblocktemplate`;
- build or compact the transaction list;
- return only the fields the Bitaxe needs:
  - version
  - previous block hash
  - nbits
  - ntime policy
  - coinbase template hooks
  - merkle branch/path data
  - optional selected transaction count/fees/weight

This still preserves sovereignty because the sidecar is local, open source, and controlled by the miner. It avoids putting Bitcoin RPC credentials and large template parsing directly on the Bitaxe.

This should be treated as fallback architecture, not a retreat from the sovereign model.

### Bitcoin RPC Credential Guidance

Bitcoin Core's RPC interface is powerful. Official Bitcoin Core documentation says RPC access can control the node, affect verification, read private data, and potentially spend funds if wallets are present. It also notes that remote RPC is not encrypted and should not be exposed to the public Internet.

Recommended setup for GridPool direct mode:

- Run the Bitcoin node without a wallet if possible.
- Do not expose Bitcoin RPC outside the LAN.
- Bind RPC to localhost or the LAN address only as needed.
- Use firewall rules to allow only the Bitaxe's static LAN IP to reach the RPC port.
- Prefer `rpcauth` static credentials over cleartext `rpcuser`/`rpcpassword` in `bitcoin.conf`.
- Use `rpcwhitelist` and `rpcwhitelistdefault=0` to restrict the Bitaxe RPC user.
- Treat `rpcwhitelist` as defense-in-depth, not a perfect sandbox. Use network and OS isolation too.

Likely minimum whitelist for a direct miner:

```ini
rpcwhitelistdefault=0
rpcwhitelist=bitaxe_gridpool:getblocktemplate,getbestblockhash,getblockheader,getblockchaininfo,getblockcount,submitblock
```

`submitblock` matters because GridPool's HTTP share endpoint receives enough data to verify and rank a share, but not the full block transaction list. If the Bitaxe finds an actual Bitcoin block, the firmware or local helper must publish the full block through the user's own Bitcoin node.

Cookie authentication is preferred by Bitcoin Core for same-machine clients, but it is awkward for a separate Bitaxe because the firmware would need access to the node's filesystem cookie. For a LAN device, a restricted `rpcauth` user plus firewalling is the more practical first design.

## Job Context Requirements

The existing `bm_job` structure is enough for ASIC work and local difficulty testing, but GridPool direct mode needs more context so the firmware can submit a complete proof.

For each active job ID, retain a `GridPoolJobContext`:

```c
typedef struct {
    char payout_address[MAX_ADDRESS_LEN];
    uint64_t gridpool_sequence;
    char prev_block_hash[65];
    uint32_t version;
    uint32_t nbits;
    uint32_t ntime;
    double submit_difficulty;

    uint8_t *coinbase_prefix;
    size_t coinbase_prefix_len;
    uint8_t *coinbase_suffix;
    size_t coinbase_suffix_len;

    uint8_t *coinbase_full;
    size_t coinbase_full_len;

    uint8_t merkle_path[MAX_MERKLE_BRANCHES][32];
    size_t merkle_path_count;

    char job_id[16];
    char extranonce2[MAX_EXTRANONCE2_HEX_LEN];
} GridPoolJobContext;
```

The exact structure can change, but the firmware must be able to reconstruct:

- the exact `headerHex` the ASIC solved;
- the exact `coinbaseHex` that commits to slot 0 and GridPool payout outputs;
- the merkle path used to produce the header merkle root;
- the parent block hash.

Without this retained context, the HTTP share submission cannot be validated by GridPool.

## Share Submission Policy

The Bitaxe should not POST every tiny nonce result to GridPool.

Initial policy:

- Track all found nonces locally in the existing scoreboard.
- Submit only if `nonce_diff` can enter the current GridPool on-deck list.
- Learn the current threshold from `GET /api/mining/share-advice`.
- If threshold is temporarily unavailable, use the last known threshold for a short grace window, then stop submitting until status recovers.
- Treat `duplicate` as non-fatal.
- Treat payout mismatch or wrong parent as useful diagnostics, not immediate firmware panic.

## UI Plan

Keep the standard AxeOS web UI. Do not embed the full GridPool Boot dashboard.

Add a compact GridPool panel on the Dashboard:

- mode: `Stratum` or `GridPool Direct`
- GridPool Boot URL
- local Bitcoin node status
- payout address
- current GridPool round/state sequence
- current team hashrate estimate
- current on-deck minimum difficulty, if available
- this Bitaxe's best share this round
- accepted / duplicate / rejected GridPool submits
- last reject reason
- rough odds or "lotto mode" progress indicator

Add settings fields:

- `gridpool_enabled`
- `gridpool_boot_url`
- `gridpool_bitcoin_rpc_url`
- `gridpool_bitcoin_rpc_auth_mode`
- `gridpool_bitcoin_rpc_username`
- `gridpool_bitcoin_rpc_password`
- `gridpool_payout_address`
- `gridpool_min_submit_difficulty`
- `gridpool_fallback_to_stratum`

Security note:

Bitcoin RPC credentials on an IoT miner are sensitive. Prefer a restricted RPC user, a cookie-proxy, or a local sidecar that exposes only template data.

## Full Relay Node Mode

Full GridPool relay mode on Bitaxe is not recommended for MVP.

A full relay node would need to:

- accept shares from peers;
- validate arbitrary peer share proofs;
- parse coinbase transactions;
- validate payout lists;
- maintain current and candidate state;
- track duplicate shares;
- relay shares onward;
- persist enough state to survive restarts;
- expose peer/network endpoints;
- protect against malformed request abuse.

This is a large amount of protocol logic for an ESP32-class device. It may be possible as a constrained "light relay", but the first firmware should focus on:

- sovereign template creation;
- local ASIC mining;
- direct HTTP submission of this device's own high-diff shares.

Possible later "light relay" scope:

- fetch network summaries;
- submit own shares;
- optionally relay only own accepted shares to multiple configured Boot nodes;
- do not accept arbitrary peer-submitted shares.

## Implementation Milestones

### Milestone 0: Read-Only GridPool Status

Goal:

- Add config for GridPool Boot URL.
- Fetch `/api/mining/payouts`.
- Display basic status in AxeOS.

Acceptance criteria:

- Firmware can fetch status over HTTP or HTTPS.
- UI shows payout sequence and Boot reachability.
- Standard Stratum mining still works unchanged when GridPool mode is disabled.

### Milestone 1: Mainnet Template Builder

Goal:

- Build ASIC work from a real local Bitcoin mainnet template.

Acceptance criteria:

- Firmware can create a coinbase with slot-0 payout address.
- Firmware can include GridPool winner outputs after slot 0.
- Firmware can compute merkle root.
- Firmware can construct a valid `bm_job`.
- Unit or integration test verifies header merkle root matches coinbase plus merkle path.
- Heap and timing measurements are recorded for real mainnet templates.

### Milestone 2: HTTP Share Submission On Mainnet

Goal:

- Submit complete share proofs to GridPool.

Acceptance criteria:

- `headerHex`, `coinbaseHex`, `merklePath`, and `prevBlockHash` are posted.
- GridPool returns `accepted` or `duplicate` for valid mainnet shares.
- Rejected shares expose useful reason in UI/logs.
- Slot-0 attribution matches the configured payout address.
- Firmware only submits shares that can enter the current on-deck list according to `/api/mining/share-advice`.

### Milestone 3: Mainnet Template Feasibility

Goal:

- Determine whether direct full `getblocktemplate` parsing is viable on target Bitaxe hardware.

Acceptance criteria:

- Measure heap usage with real mainnet template sizes.
- Measure time to parse and build merkle path.
- Confirm mining loop is not starved.
- Decide whether to continue direct RPC mode or require the practical sovereign sidecar.

### Milestone 4: Lotto UI

Goal:

- Make the mode usable by a lottery miner.

Acceptance criteria:

- UI clearly shows how to configure Bitcoin node and GridPool node.
- UI shows "best share this round".
- UI shows current on-deck threshold when available.
- UI shows accepted/rejected submits.
- UI has an obvious fallback to standard Stratum mode.

### Milestone 5: Long Soak

Goal:

- Prove the firmware can mine stably for days.

Acceptance criteria:

- `24h` no-crash run on mainnet templates.
- No heap leak trend.
- No Wi-Fi reconnect loop caused by status polling.
- No excessive HTTP POST spam.
- No impact to configured power/frequency settings.

## Open Engineering Questions

- Can ESP32 memory handle full mainnet `getblocktemplate` responses? Unknown. Estimate says this may be tight because transaction data is returned as hex inside JSON, so measure with real mainnet templates and avoid full-DOM parsing.
- Can the firmware build merkle paths cheaply enough without a sidecar? Unknown. Must be tested on hardware.
- Should GridPool expose a compact "work advice" endpoint for direct devices? No. The miner must build fully sovereign work from its own Bitcoin node. GridPool now exposes only share-submission advice, not block-template advice.
- What is the safest way to store or avoid Bitcoin RPC credentials? Prefer a restricted `rpcauth` user, `rpcwhitelist`, LAN firewalling to the Bitaxe IP, no wallet on the node, and no public RPC exposure. A local user-controlled sidecar can reduce credential exposure if direct RPC proves awkward, but it must not outsource template policy to GridPool.
- Should the direct client submit only shares that can enter the on-deck list, or also lower-difficulty telemetry shares to its selected Boot node? Only submit shares that can enter the on-deck list.
- Should GridPool direct mode use HTTPS to the public Boot node by default, and plain HTTP only for LAN test nodes? Yes. Use HTTPS by default for privacy when talking to public Boot nodes. Plain HTTP is acceptable for LAN/dev because valid shares cannot be modified or stolen without invalidating the proof.
- How should firmware react when GridPool payout sequence changes while ASICs still have old work queued? Flush old work as fast as possible and switch to new work.

## Suggested First Agent Task

Start with Milestone 0 only:

- add read-only GridPool config fields;
- fetch `/api/mining/payouts`;
- display a compact status panel in AxeOS;
- do not touch ASIC work generation yet.

This gives a safe first PR and builds familiarity with the firmware UI, settings, HTTP client, and memory profile before entering the mining hot path.

## Sources

- esp-miner repository: `https://github.com/bitaxeorg/esp-miner`
- GridPool HTTP share submission plan: `/home/keegreil/Documents/GitHub/boot-protocol/docs/hydrapool-http-submission.md`
- GridPool share validator: `/home/keegreil/Documents/GitHub/boot-protocol/boot_portal/Services/BootShareVerifier.cs`
- Stratum V1 reference linked by esp-miner: `https://reference.cash/mining/stratum-protocol`
- Bitcoin Core JSON-RPC security notes: `https://github.com/bitcoin/bitcoin/blob/master/doc/JSON-RPC-interface.md`
- Bitcoin Core `getblocktemplate` RPC: `https://bitcoincore.org/en/doc/28.0.0/rpc/mining/getblocktemplate/`
