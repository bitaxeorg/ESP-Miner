# CLAUDE.md - AI Agent Guide for ACS ESP-Miner

## Project Overview

**ACS ESP-Miner** is an ESP32-S3 based Bitcoin mining device firmware (Bitaxe fork). It controls ASIC mining chips, manages power/thermal systems, communicates with mining pools via Stratum protocol, and provides a web interface for monitoring and configuration.

### Target Hardware
- **MCU**: ESP32-S3 (dual-core Xtensa @ 240MHz)
- **RAM**: 512KB SRAM + 8MB external PSRAM
- **Flash**: 16MB
- **ASICs Supported**: BM1366, BM1368, BM1370, BM1397
- **Device Models**: DEVICE_MAX, DEVICE_ULTRA, DEVICE_SUPRA, DEVICE_GAMMA

## Build System

### Prerequisites
- ESP-IDF v5.4.0 or later
- Node.js (for web UI builds)

### Common Commands

```bash
# Build firmware
idf.py build

# Build and flash
idf.py -p /dev/ttyACM0 flash

# Monitor serial output
idf.py -p /dev/ttyACM0 monitor

# Build web UI (from main/http_server/acs-os/)
npm install && npm run build

# Run unit tests (target-based, replaces firmware!)
cd test && idf.py build
# WARNING: Flashing test binary replaces main firmware

# Merge binaries for release
./merge_bin.sh ./esp-miner-merged.bin
```

### Build Artifacts
- Firmware: `build/esp-miner.bin`
- Merged binary: `esp-miner-merged.bin`
- Web UI: `main/http_server/acs-os/dist/`

## Architecture Overview

### Directory Structure

```
/
├── components/           # Reusable ESP-IDF components
│   ├── asic/            # ASIC drivers (BM1366/68/70/97)
│   ├── stratum/         # Mining pool protocol
│   ├── connect/         # WiFi connectivity
│   └── dns_server/      # Captive portal DNS
├── main/                # Main application
│   ├── tasks/           # FreeRTOS tasks
│   ├── http_server/     # REST API + Web UI
│   ├── database/        # JSON storage system
│   └── self_test/       # Hardware diagnostics
├── test/                # Unity test harness
└── doc/                 # Documentation
```

### FreeRTOS Tasks

| Task | File | Priority | Purpose |
|------|------|----------|---------|
| POWER_MANAGEMENT | `tasks/power_management_task.c` | 10 | Thermal management, voltage/frequency control, autotune |
| SYSTEM | `system.c` | 3 | UI updates, monitoring |
| STRATUM | `tasks/stratum_task.c` | - | Pool communication |
| ASIC | `tasks/asic_task.c` | - | Work transmission to ASICs |
| ASIC_RESULT | `tasks/asic_result_task.c` | - | Result collection from ASICs |
| CREATE_JOBS | `tasks/create_jobs_task.c` | - | Mining job queue management |

### Global State (IMPORTANT)

**Current Architecture Issue**: The codebase uses a monolithic `GlobalState` structure defined in `main/global_state.h`. This creates tight coupling and makes testing difficult.

```c
// main/global_state.h
typedef struct {
    DeviceModel device_model;
    AsicModel asic_model;
    AsicFunctions ASIC_functions;
    work_queue stratum_queue;
    work_queue ASIC_jobs_queue;

    // Embedded modules (tight coupling)
    SystemModule SYSTEM_MODULE;
    PowerManagementModule POWER_MANAGEMENT_MODULE;
    AutotuneModule AUTOTUNE_MODULE;
    SelfTestModule SELF_TEST_MODULE;
    // ... more fields
} GlobalState;
```

**Known Race Condition**: The autotune system in `power_management_task.c` accesses ASIC temps from `GlobalState` while other tasks may be writing to it. The `valid_jobs_lock` mutex only protects `valid_jobs`, not the module structs.

### Module Boundaries

| Module | Header | Source | Dependencies |
|--------|--------|--------|--------------|
| Power Management | `power_management_task.h` | `tasks/power_management_task.c` | EMC2101, INA260, TPS546, nvs_config, vcore |
| Autotune | (in power_management_task.h) | `tasks/power_management_task.c` | Power Management |
| Database | `database/dataBase.h` | `database/dataBase.c` | SPIFFS, cJSON |
| BAP Display | `lvglDisplayBAP.h` | `lvglDisplayBAP.c` | UART, nvs_config, GlobalState |
| HTTP API | `http_server/http_server.h` | `http_server/http_server.c` | esp_http_server, GlobalState |

## Testing

### Current Test Infrastructure
- **Framework**: Unity (ESP-IDF integrated)
- **Location**: `components/*/test/` and `test/`
- **Existing Tests**: ~614 lines covering stratum/mining protocol

### Running Tests (Target-Based)
```bash
cd test
idf.py build
# Flash to device (WARNING: replaces main firmware)
esptool.py -p /dev/ttyACM0 write_flash ...
idf.py -p /dev/ttyACM0 monitor
```

### Adding Tests (Current Pattern)
1. Create `components/<name>/test/test_<name>.c`
2. Add to `test/CMakeLists.txt`: `TEST_COMPONENTS`
3. Use Unity macros: `TEST_CASE("description", "[tag]")`

### Future: Host-Based Testing (Planned)
Host-based tests will be added under `test/host/` with CMake + Unity + hybrid mocking.

## Code Patterns

### ESP-IDF Conventions
```c
// Logging
static const char *TAG = "module_name";
ESP_LOGI(TAG, "Info message: %d", value);
ESP_LOGE(TAG, "Error message");

// Error handling
esp_err_t result = some_function();
if (result != ESP_OK) {
    ESP_LOGE(TAG, "Failed: %s", esp_err_to_name(result));
    return result;
}

// NVS Configuration
uint16_t value = nvs_config_get_u16(NVS_CONFIG_KEY, default_value);
nvs_config_set_u16(NVS_CONFIG_KEY, new_value);
```

### Hardware Abstraction
Sensor drivers use a flat function API (not OOP):
```c
// EMC2101 - Temperature/Fan control
float temp = EMC2101_get_external_temp();
EMC2101_set_fan_speed(0.75);  // 75%

// INA260 - Power monitoring
float voltage = INA260_read_voltage();
float current = INA260_read_current();

// TPS546 - Voltage regulator
float vin = TPS546_get_vin();
float iout = TPS546_get_iout();
```

### BAP Protocol (Bitaxe Access Port)
Serial protocol for external display communication:
```
Packet: [0xFF][0xAA][REG][LEN][DATA...][CRC16_HI][CRC16_LO]
```
- Registers defined in `lvglDisplayBAP.h` (0x21-0xF1)
- CRC16-CCITT polynomial 0x1021

## Known Issues & Technical Debt

### Critical
1. **Race Condition in Autotune**: Multiple tasks access `GlobalState` module structs without proper synchronization
2. **Global State Coupling**: All modules depend on monolithic `GlobalState`

### High Priority
1. **Logging fragmentation**: Uses ESP_LOG* but no unified logging to WebUI/Screen/Database
2. **BAP code complexity**: Large switch statements with byte manipulation
3. **Code duplication**: Overheat handling repeated for each device type

### Medium Priority
1. **Limited test coverage**: ~614 lines of tests for ~10K lines of code
2. **No host-based tests**: All tests require flashing to hardware
3. **Function length**: Many functions exceed 100 lines (Clean Code violation)

## Configuration

### NVS Keys (Non-Volatile Storage)
Key constants in `nvs_config.h`:
- `NVS_CONFIG_ASIC_VOLTAGE` - Core voltage in mV
- `NVS_CONFIG_ASIC_FREQ` - Frequency in MHz
- `NVS_CONFIG_FAN_SPEED` - Manual fan speed %
- `NVS_CONFIG_AUTO_FAN_SPEED` - Auto fan enable flag
- `NVS_CONFIG_OVERHEAT_MODE` - Overheat protection state
- `NVS_CONFIG_STRATUM_URL` - Mining pool URL

### Kconfig Options
Located in `main/Kconfig.projbuild`:
- GPIO pin assignments
- Default ASIC voltage/frequency
- Stratum server defaults

## API Reference

### REST Endpoints (main/http_server/APIreadme.md)
- `GET /api/system/info` - Mining statistics
- `PATCH /api/system` - Update configuration
- `GET /api/swarm/info` - Multi-device status
- `POST /api/system/restart` - Reboot device

### WebSocket
- Real-time updates on `/ws`

## Guidelines for AI Agents

### DO
- Read existing code before modifying
- Follow ESP-IDF error handling patterns
- Add tests for new functionality
- Use existing NVS keys when possible
- Keep functions under 40 lines (Clean Code)
- Use meaningful variable names

### DON'T
- Modify `GlobalState` structure without planning migration
- Add new global variables
- Skip error checking on ESP-IDF calls
- Create new files when editing existing ones works
- Use blocking delays in tasks (use `vTaskDelay`)

### When Adding Features
1. Check if similar functionality exists
2. Identify which module owns the feature
3. Consider thread safety (FreeRTOS tasks)
4. Add unit tests (see Testing section)
5. Update API documentation if adding endpoints

### When Fixing Bugs
1. Reproduce with serial monitor (`idf.py monitor`)
2. Check task stack sizes if crashes occur
3. Verify mutex usage for shared state
4. Test on actual hardware when possible

## File Quick Reference

| Purpose | File |
|---------|------|
| Entry point | `main/main.c` |
| Global state definition | `main/global_state.h` |
| Power/thermal management | `main/tasks/power_management_task.c` |
| Mining protocol | `components/stratum/mining.c` |
| REST API | `main/http_server/http_server.c` |
| Database/logging | `main/database/dataBase.c` |
| Display protocol | `main/lvglDisplayBAP.c` |
| NVS configuration | `main/nvs_config.c` |
| ASIC drivers | `components/asic/bm1370.c` (etc.) |

## Refactoring Roadmap (In Progress)

This codebase is undergoing refactoring to improve:
1. **Testability** - Host-based unit tests with mocking
2. **Modularity** - Extract state from GlobalState into modules
3. **Thread Safety** - Proper synchronization for shared state
4. **Clean Code** - Smaller functions, better naming, DI

See implementation plan for detailed phased approach.
