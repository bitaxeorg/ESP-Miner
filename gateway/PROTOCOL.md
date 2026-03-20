# ESP-Miner Gateway Protocol

The ESP32 gateway connects to the hashly server over a single persistent WebSocket
connection and acts as a dumb network proxy — all mining adapter logic lives on the
server.  The wire protocol is a **custom tRPC WebSocket dialect** implemented in
[`src/wslink.c`](src/wslink.c) on the ESP32 and served by the tRPC router in
[`packages/server/src/routers/client.router.ts`](../../packages/server/src/routers/client.router.ts).

---

## Table of Contents

1. [Connection lifecycle](#1-connection-lifecycle)
2. [Wire format](#2-wire-format)
3. [ESP32 → Server messages](#3-esp32--server-messages)
4. [Server → ESP32 messages](#4-server--esp32-messages)
5. [Command types](#5-command-types)
6. [Complete sequence example](#6-complete-sequence-example)
7. [Reconnect and replay](#7-reconnect-and-replay)
8. [Authentication](#8-authentication)
9. [Logging tags](#9-logging-tags)

---

## 1. Connection lifecycle

```
ESP32                                    Server
  |                                        |
  |  WebSocket upgrade (GET /?connectionParams=1)
  |--------------------------------------->|
  |                                        |
  |  connectionParams  (method)            |
  |--------------------------------------->|
  |                                        |
  |  subscription: client.onCommand        |
  |--------------------------------------->|
  |                                        |
  |  result: { type:"started" }            |
  |<---------------------------------------|
  |        [gateway is now authenticated]  |
  |                                        |
  |  data: { commandId, type, ...fields }  |  (server pushes commands)
  |<---------------------------------------|
  |                                        |
  |  mutation: client.commandResult        |  (ESP32 reports result)
  |--------------------------------------->|
  |                                        |
  |  mutation: client.commandAck           |  (optional ACK for user commands)
  |--------------------------------------->|
  |                                        |
  |  PING                                  |  (server keep-alive, every ~30 s)
  |<---------------------------------------|
  |  PONG                                  |
  |--------------------------------------->|
```

The URL query string `?connectionParams=1` is appended by `wslink_prepare_url()`
whenever a `get_connection_params` callback is configured (always, in practice).
The server uses this flag to expect the `connectionParams` message before processing
any operations.

---

## 2. Wire format

All messages are **JSON text frames** except for the plain-text `PING`/`PONG`
keep-alive frames (see §2.4).

### 2.1 ESP32 → Server: outgoing request

```json
{
  "id":     <number>,          // monotonically increasing per-connection counter
  "method": "<method>",        // "connectionParams" | "subscription" | "mutation" | "subscription.stop"
  "params": { ... }            // present for subscription / mutation (not connectionParams)
}
```

### 2.2 Server → ESP32: response

```json
{
  "id":     <number>,          // echoes the request id
  "result": {
    "type": "<type>",          // "started" | "data" | "stopped"
    "data": { ... }            // present for "data" type only
  }
}
```

On error:

```json
{
  "id":    <number>,
  "error": { "message": "<string>" }
}
```

### 2.3 Server → ESP32: keep-alive

The server sends the raw 4-byte text frame `PING` periodically.
The ESP32 responds immediately with `PONG` (also a raw 4-byte text frame).
Neither side attempts JSON parsing on these frames.

### 2.4 Message ID space

IDs are scoped to a single WebSocket connection.  The ESP32 resets `next_id` to `1`
after every reconnect (the `wslink_client_t` is re-used but its ID counter is not
reset — IDs just keep incrementing across reconnects, which is fine because the
server matches by `commandId` strings, not by `id` integers).

---

## 3. ESP32 → Server messages

### 3.1 `connectionParams`

Sent immediately after the WebSocket transport opens, before any subscription or
mutation.  Carries credentials and device metadata.

```json
{
  "method": "connectionParams",
  "data": {
    "clientId":     "<connector client-id>",
    "clientSecret": "<connector client-secret>",
    "firmware":     "<version string>",
    "mac":          "<ESP32 MAC address>",
    "localIp":      "<ESP32 LAN IP>"
  }
}
```

Note: no `id` field — `connectionParams` is not a tracked request.

The server authenticates using `clientId` + `clientSecret`.  On success it creates
(or updates) the connector record; on failure the WebSocket is closed.

### 3.2 `subscription` — `client.onCommand`

Sent once after `connectionParams` (and replayed automatically on every reconnect).

```json
{
  "id":     1,
  "method": "subscription",
  "params": {
    "path": "client.onCommand"
  }
}
```

The server responds with `{ "type": "started" }` when the connector is authenticated,
then streams command objects as `{ "type": "data", "data": { ... } }` whenever the
server enqueues a command for this connector.

### 3.3 `mutation` — `client.commandResult`

Sent after executing a command to deliver the result (or error).

```json
{
  "id":     <number>,
  "method": "mutation",
  "params": {
    "path":  "client.commandResult",
    "input": {
      "commandId": "<GW_xxx or user-command-id>",
      "success":   true | false,
      "result":    { ... },      // present on success
      "error":     "<string>",   // present on failure
      "partial":   true          // omitted unless this is a streaming partial (SCAN_IP_RANGE)
    }
  }
}
```

For `SCAN_IP_RANGE` the ESP32 sends one partial result per discovered host, then
a final non-partial result with diagnostics:

```
partial=true  →  { ip: "10.0.0.x", ports: { "80": true, "4028": false } }
partial=false →  { totalFound: <n>, freeHeap: <bytes> }   (final, terminates the command)
```

### 3.4 `mutation` — `client.commandAck`

Sent to acknowledge user-facing commands (distinct from gateway-internal GW_xxx
commands which do not need an ACK).

```json
{
  "id":     <number>,
  "method": "mutation",
  "params": {
    "path":  "client.commandAck",
    "input": {
      "commandId": "<user-command-id>"
    }
  }
}
```

### 3.5 `subscription.stop`

Sent to cancel a subscription (used during teardown; not sent on disconnect).

```json
{
  "id":     <subscription-id>,
  "method": "subscription.stop"
}
```

---

## 4. Server → ESP32 messages

### 4.1 Subscription data — command delivery

Every command pushed by the server arrives as a tRPC subscription data frame on the
`client.onCommand` subscription:

```json
{
  "id":     1,
  "result": {
    "type": "data",
    "data": {
      "commandId": "<GW_xxx>",
      "type":      "<COMMAND_TYPE>",
      // ... command-specific fields (see §5)
    }
  }
}
```

`commandId` is a server-generated string (`GW_<timestamp>-<seq>` for gateway-internal
commands, or a DB-record ID for user-facing commands).  It must be echoed back in
`commandResult` / `commandAck`.

### 4.2 Subscription `started`

```json
{ "id": 1, "result": { "type": "started" } }
```

Received once after the subscription is accepted.  The ESP32 treats this as the
authentication confirmation (`g_authenticated = true`).

### 4.3 Subscription `stopped`

```json
{ "id": 1, "result": { "type": "stopped" } }
```

The server stopped the subscription (e.g. connector retired on reconnect from
another device).  The ESP32 clears `g_authenticated` but keeps the WebSocket open;
the subscription will be replayed on the next reconnect.

### 4.4 Mutation response

Responses to `commandResult` and `commandAck` mutations:

```json
{ "id": <number>, "result": { "type": "data", "data": { "ok": true } } }
```

The ESP32 fires-and-forgets these mutations (no callback registered), so the
response is silently discarded.

---

## 5. Command types

All commands share the envelope `{ commandId, type, ...fields }`.

### 5.1 `HTTP_REQUEST`

Execute a single HTTP call on the local network and return the response.

**Server → ESP32 (input):**

| Field        | Type   | Default | Description                        |
|--------------|--------|---------|------------------------------------|
| `ip`         | string | —       | Target IP address (required)       |
| `port`       | number | `80`    | TCP port                           |
| `method`     | string | `"GET"` | `GET`, `POST`, `PATCH`, `PUT`      |
| `path`       | string | `"/"`   | URL path (e.g. `/api/v1/summary`)  |
| `body`       | string | `null`  | Request body (for POST/PATCH/PUT)  |
| `timeoutMs`  | number | `10000` | Per-request timeout in ms          |

**ESP32 → Server (result on success):**

```json
{ "status": 200, "body": "<raw response text>" }
```

**ESP32 → Server (on failure):**

```json
{ "success": false, "error": "HTTP GET http://...:80/... failed" }
```

Response body buffer is capped at **8 192 bytes**.

---

### 5.2 `TCP_REQUEST`

Send one message over a raw TCP socket and read one response.  Used for CGMiner API
(`port 4028`) and other line-protocol miners.

**Server → ESP32 (input):**

| Field       | Type   | Default | Description                  |
|-------------|--------|---------|------------------------------|
| `ip`        | string | —       | Target IP (required)         |
| `port`      | number | `4028`  | TCP port                     |
| `data`      | string | `""`    | Bytes to send                |
| `timeoutMs` | number | `8000`  | Connect + receive timeout ms |

**ESP32 → Server (result on success):**

```json
{ "body": "<raw response text>" }
```

Response buffer is capped at **4 096 bytes**.

---

### 5.3 `SCAN_IP_RANGE`

TCP-probe every host in a CIDR range and report which ports are open.
Used for initial miner discovery.

**Server → ESP32 (input):**

| Field   | Type     | Default           | Description                              |
|---------|----------|-------------------|------------------------------------------|
| `cidr`  | string   | auto (local /24)  | CIDR range, e.g. `"192.168.1.0/24"`     |
| `ports` | number[] | `[80, 4028]`      | Ports to probe on each host (max 16)     |

If `cidr` is omitted, the ESP32 derives the range from its own IP + netmask.

**ESP32 → Server (partial result per alive host):**

```json
{
  "commandId": "...",
  "success":   true,
  "partial":   true,
  "result":    { "ip": "192.168.1.42", "ports": { "80": true, "4028": false } }
}
```

**ESP32 → Server (final result, `partial` omitted):**

```json
{
  "commandId": "...",
  "success":   true,
  "result":    { "totalFound": 3, "freeHeap": 180000 }
}
```

Only one scan can be in progress at a time; a second `SCAN_IP_RANGE` while one is
running is silently ignored.  Server timeout: **10 minutes**.

---

### 5.4 `MAC_LOOKUP`

Resolve one or more MAC addresses to their current LAN IPs.  The ESP32 checks the
lwIP ARP cache first; if any MACs are still unresolved **and** CIDR ranges are
provided, it runs an ARP sweep (TCP-probe port 80 on every host, checking the ARP
cache for all pending MACs inline after each probe).

**Why inline checking?** The lwIP ARP table has only 10 slots.  During a 254-host
sweep old entries are evicted before the sweep completes, so each MAC must be
checked immediately after it appears in the table.

**Server → ESP32 (input):**

| Field   | Type     | Default | Description                                       |
|---------|----------|---------|---------------------------------------------------|
| `macs`  | string[] | —       | MAC addresses to resolve (required, max 64)       |
| `cidrs` | string[] | `[]`    | CIDR ranges to sweep if any MACs are unresolved   |

MACs must be colon-separated lowercase or uppercase hex, e.g. `"aa:bb:cc:dd:ee:ff"`.

**ESP32 → Server (result):**

```json
{
  "AA:BB:CC:DD:EE:FF": "192.168.1.42",
  "11:22:33:44:55:66": null
}
```

Each key is the requested MAC; the value is the resolved IP string or `null`.

**Timing:** ARP sweep over a /24 takes ~13 s (254 hosts × 50 ms probe timeout).
Server timeout: **60 seconds**.

---

### 5.5 `IP_LOOKUP`

Reverse ARP: given an IP, return the MAC address currently assigned to it in the
lwIP ARP table.  Used by `GatewayPollManager` to periodically verify that the IP
stored for a miner still belongs to the same MAC (detects DHCP reassignments).

**Server → ESP32 (input):**

| Field | Type   | Description         |
|-------|--------|---------------------|
| `ip`  | string | IP to look up       |

**ESP32 → Server (result):**

```json
{ "mac": "aa:bb:cc:dd:ee:ff" }   // or  { "mac": null }  if not in ARP table
```

Server timeout: **5 seconds** (ARP cache lookup only, no network I/O).

---

## 6. Complete sequence example

Below is a typical startup + one `HTTP_REQUEST` command cycle.

```
ESP32                                          Server
 |                                               |
 | WS open /?connectionParams=1                 |
 |--------------------------------------------->|
 |                                               |
 | { method:"connectionParams",                 |
 |   data: { clientId:"abc", firmware:"1.2",   |
 |            mac:"aa:bb:...", localIp:"10.x" }}|
 |--------------------------------------------->|
 |                                               |  (server authenticates connector)
 |                                               |
 | { id:1, method:"subscription",               |
 |   params:{ path:"client.onCommand" }}        |
 |--------------------------------------------->|
 |                                               |
 |           { id:1, result:{ type:"started" }} |
 |<---------------------------------------------|
 |                                               |  [gateway is now authenticated]
 |                                               |
 |  (server enqueues HTTP_REQUEST)               |
 |                                               |
 | { id:1, result:{ type:"data", data:{         |
 |     commandId: "GW_1710000000000-1",         |
 |     type: "HTTP_REQUEST",                    |
 |     ip: "192.168.1.42", port: 80,            |
 |     method: "GET", path: "/api/v1/summary"   |
 |   }}}                                        |
 |<---------------------------------------------|
 |                                               |  (ESP32 executes HTTP call)
 |                                               |
 | { id:2, method:"mutation", params:{          |
 |     path:"client.commandResult", input:{     |
 |       commandId: "GW_1710000000000-1",       |
 |       success: true,                         |
 |       result: { status:200, body:"{...}" }   |
 |     }}}                                      |
 |--------------------------------------------->|
 |                                               |
 |           { id:2, result:{ type:"data",      |
 |                            data:{ ok:true }} }|
 |<---------------------------------------------|
 |                                               |
 |  (30 s later — server keep-alive)            |
 |                                               |
 |                                        "PING" |
 |<---------------------------------------------|
 | "PONG"                                        |
 |--------------------------------------------->|
```

---

## 7. Reconnect and replay

When the WebSocket drops (network error, server restart, etc.):

1. `wslink_on_disconnected()` is called.
2. All in-flight **mutations** are failed immediately (fire-and-forget from the
   server's perspective anyway).
3. All registered **subscriptions** (`client.onCommand`) remain in the request table
   in state `OUTGOING`.
4. `gateway_core_run()` waits 5 s, then reconnects.
5. On the new connection `wslink_on_connected()`:
   a. Sends `connectionParams`.
   b. Replays all `OUTGOING` subscriptions — `client.onCommand` is re-subscribed
      automatically, triggering re-authentication.

No commands are buffered during the disconnect window; the server's `GatewayConnection`
detects the stale connection and will re-enqueue pending commands on reconnect.

---

## 8. Authentication

Authentication is implicit in the `connectionParams` exchange:

| Field          | Source                                         |
|----------------|------------------------------------------------|
| `clientId`     | Provisioned into firmware (NVS / build config) |
| `clientSecret` | Provisioned into firmware (NVS / build config) |
| `firmware`     | Build-time version string                      |
| `mac`          | `esp_read_mac()` (ESP32 base MAC)              |
| `localIp`      | `esp_netif_get_ip_info()`                      |

The server matches `clientId` + `clientSecret` against the `Connector` table.
If authentication fails the server closes the WebSocket.  The `client.onCommand`
subscription's `{ type: "started" }` response is the definitive signal that the
gateway is authenticated and ready.

---

## 9. Logging tags

All gateway log lines are prefixed so they can be filtered with a single pattern
(`gw_`):

| Tag          | Source file                          | What it covers                  |
|--------------|--------------------------------------|---------------------------------|
| `gw_core`    | `src/gateway_core.c`                 | Commands, scan, connect loop    |
| `gw_wslink`  | `src/wslink.c`                       | tRPC wire framing, request table|
| `gw_ws`      | `platform/esp32/gateway_platform_esp32.c` | Raw WebSocket TX/RX        |

Filter all gateway logs (ESP-IDF monitor or idf.py):

```
idf.py monitor | grep "gw_"
```
