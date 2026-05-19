# AxeOS

The Angular frontend for the Bitaxe open-source Bitcoin miner. All commands can be run either from this directory or from the **repository root** (commands are forwarded via the root `package.json`).

## ESP-Miner Prometheus Metrics API

A new endpoint `/api/system/metrics` is available for Prometheus-compatible monitoring. It returns metrics in Prometheus text exposition format, suitable for direct scraping.

### Example usage

```
curl http://<device_ip>/api/system/metrics
```

### Example output

```
# HELP espminer_uptime_seconds Device uptime in seconds
# TYPE espminer_uptime_seconds gauge
espminer_uptime_seconds 12345.67
# HELP espminer_hashrate_hashes_per_second Current hashrate
# TYPE espminer_hashrate_hashes_per_second gauge
espminer_hashrate_hashes_per_second 123.4
...
```

### Metric families
- `espminer_build_info{...}`: Build and device info (labels: firmware_version, device_model, asic_model, board, hostname)
- `espminer_uptime_seconds`: Device uptime
- `espminer_heap_free_bytes`, `espminer_heap_min_free_bytes`: Heap memory
- `espminer_wifi_rssi_dbm`, `espminer_wifi_connected`: WiFi status
- `espminer_hashrate_hashes_per_second`: Current hashrate
- `espminer_shares_accepted_total`, `espminer_shares_rejected_total`, `espminer_jobs_received_total`: Mining counters
- `espminer_best_share`: Best share difficulty
- `espminer_pool_connected`: Pool connection state
- `espminer_mining_enabled`: Mining enabled state
- `espminer_fan_rpm`, `espminer_fan2_rpm`: Fan speeds
- `espminer_chip_temp_celsius`, `espminer_vr_temp_celsius`: Temperatures
- `espminer_voltage_volts`, `espminer_frequency_hz`, `espminer_power_watts`, `espminer_current_amps`: Power/frequency

See the endpoint for the full list and current values.

## Development server

### Mock data (no device required)

```bash
npm run start
```

Navigate to `http://localhost:4200/`. The app will use built-in mock data so you can develop the UI without a real device. HMR is enabled — style changes apply instantly, template/TypeScript changes trigger a full page reload.

### Live device (proxy to real hardware)

To connect the dev server to an actual Bitaxe on your local network:

```bash
BITAXE_IP=192.168.1.152 npm run start:proxy
```

- Replace `192.168.1.152` with your device's IP address.
- If `BITAXE_IP` is omitted, the proxy falls back to `http://192.168.1.100`.
- All `/api` HTTP and WebSocket traffic is forwarded to the device via `proxy.conf.js`.
- The `Origin` header is automatically rewritten to pass the device's private-network CORS check.

The `mock` flag in `src/environments/environment.ts` is `false` by default, so the real device API is used when running with the proxy. If you switch back to `npm run start` (no proxy), set `mock: true` in that file to re-enable mock data.

## Build

```bash
npm run build
```

Generates the production bundle, gzip-compresses assets, and writes a `version.txt`. Build artifacts are stored in `dist/axe-os/`.

## Running unit tests

```bash
npm run test:ci
```

Runs all Karma/Jasmine unit tests in a headless Chrome environment. Also accepts the plain `npm run test` variant for interactive (watch) mode.

```bash
export CHROME_BIN=/snap/bin/chromium   # adjust path for your system
npm run test
```

## Code generation

After modifying `../openapi.yaml`, regenerate the TypeScript API client:

```bash
npm run generate:api
```

This is also run automatically as part of `npm run build` and `npm run test:ci`.

## Further help

To get more help on the Angular CLI use `ng help` or check out the [Angular CLI Overview and Command Reference](https://angular.io/cli).
