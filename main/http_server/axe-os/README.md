# AxeOS

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
- `espminer_mining_paused`: Mining paused state (1=paused)
- `espminer_fan_rpm` (+ `espminer_fan2_rpm` on dual-fan hardware): Fan speeds
- `espminer_chip_temp_celsius`, `espminer_vr_temp_celsius` (+ `espminer_chip_temp2_celsius` on dual-temp hardware): Temperatures
- `espminer_voltage_volts`, `espminer_frequency_hz`, `espminer_power_watts`, `espminer_current_amps`: Power/frequency

See the endpoint for the full list and current values.

---

This project was generated with [Angular CLI](https://github.com/angular/angular-cli) version 16.1.3.

## Development server

Run `ng serve` for a dev server. Navigate to `http://localhost:4200/`. The application will automatically reload if you change any of the source files.

## Code scaffolding

Run `ng generate component component-name` to generate a new component. You can also use `ng generate directive|pipe|service|class|guard|interface|enum|module`.

## Build

Run `ng build` to build the project. The build artifacts will be stored in the `dist/` directory.

## Running unit tests

Run `ng test` to execute the unit tests via [Karma](https://karma-runner.github.io).

```
export CHROME_BIN=/snap/bin/chromium
npm run test
```
The value of `CHROME_BIN` depends on your system.  
On linux you may use `which chromium` or so to get the value.  

## Running end-to-end tests

Run `ng e2e` to execute the end-to-end tests via a platform of your choice. To use this command, you need to first add a package that implements end-to-end testing capabilities.

## Further help

To get more help on the Angular CLI use `ng help` or go check out the [Angular CLI Overview and Command Reference](https://angular.io/cli) page.
