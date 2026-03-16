# ESP-Miner-Gateway

A fork of [ESP-Miner](https://github.com/bitaxeorg/ESP-Miner) modified to work as a hardware gateway for [HeliumDeploy](https://app.heliumdeploy.com/), a SaaS platform for managing cryptocurrency ASIC miners.

## What's different from upstream ESP-Miner

This fork adds server-driven gateway functionality so the ESP32 can be remotely managed by the HeliumDeploy platform:

- **WebSocket client** (`ws_client_task`) — Maintains a persistent WS/WSS connection to the HeliumDeploy server with auto-reconnect and keepalive
- **Miner adapter** (`miner_adapter`) — Discovers and communicates with miners on the local network via their HTTP APIs
- **Gateway task** (`gateway_task`) — Coordinates polling, command execution, and network scanning as directed by the server
- **Binary-patchable credentials** — Firmware contains placeholder sentinels (`HASHLY_CID:`, `HASHLY_SEC:`, `HASHLY_URL:`) that are patched at download time with per-organization credentials, so each device connects to the right account automatically
- **TLS support** — Full WSS support with embedded CA certificate bundle, TLS 1.2/1.3, and hardware-accelerated crypto

## Architecture

```
[Miners] <--HTTP--> [ESP32 Gateway] <--WSS--> [HeliumDeploy Server] <--Redis--> [API] <--tRPC--> [Frontend]
```

The gateway is server-driven: the ESP32 stores only its WS URL and credentials. All commands (poll miners, scan network, update config) are sent from the server.

## Protocol

1. Gateway connects to WS server and sends `auth` with client ID + secret
2. Server responds with `auth_ok`
3. Server sends commands: `poll_miners`, `scan_network`, `set_config`, `restart_miner`, etc.
4. Gateway executes commands against local miners and reports results back

## Building

### Prerequisites

- [ESP-IDF v5.5.1](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/) toolchain
- Node.js/npm (for AxeOS web UI build)
- Java (for OpenAPI code generation)

### Build

```bash
. ~/esp/esp-idf/export.sh
idf.py build
```

Or use the build script from the [hashly](https://github.com/moken-io/hashly) monorepo:

```bash
./scripts/build-firmware.sh
```

This builds the firmware and copies the output to `packages/api/firmware/esp-miner.bin` for the firmware download API to serve.

### Firmware patching

The built binary is a template. The HeliumDeploy API patches it per-organization at download time:

| Sentinel | Field size | Value |
|---|---|---|
| `HASHLY_CID:__PLACEHOLDER_CLIENT_ID__` | 128 bytes | Connector client ID |
| `HASHLY_SEC:__PLACEHOLDER_CLIENT_SECRET__` | 128 bytes | Connector client secret |
| `HASHLY_URL:__PLACEHOLDER_WS_URL__` | 256 bytes | WebSocket server URL |

After patching, ESP32 checksums (XOR + SHA-256) are recalculated.

## AxeOS Web UI

The local web UI is the standard AxeOS Angular application, served from a SPIFFS partition. It provides a local dashboard for the miner at `http://<device-ip>/`.

For details on the AxeOS API, see [`main/http_server/openapi.yaml`](./main/http_server/openapi.yaml).

## Flashing

With the device connected via USB:

```bash
pip install bitaxetool
bitaxetool --config ./config-xxx.cvs --firmware ./esp-miner-merged.bin
```

## Upstream

Based on [bitaxeorg/ESP-Miner](https://github.com/bitaxeorg/ESP-Miner). See the upstream repo for general ESP-Miner documentation, hardware compatibility, and community support.
