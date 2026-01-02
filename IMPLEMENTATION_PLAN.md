# ACS ESP-Miner Refactoring & Test Coverage Implementation Plan

## Executive Summary

This plan transforms the ACS ESP-Miner codebase into a well-tested, maintainable, agent-ready system. The approach is **incremental** - each phase delivers value while building toward the full vision.

**Goals**:
- 80% unit test coverage
- Host-based testing (no hardware required)
- Clean Code principles (Uncle Bob)
- Elimination of global state coupling
- Unified logging system
- Pre-commit hooks and PR checks

**Priority Order** (per owner requirements):
1. Power Management + Autotune (fix race condition)
2. Logging Module (unified multi-destination)
3. Database Layer
4. BAP API (byte manipulation refactor)
5. HTTP API

---

## Phase 0: Test Infrastructure Foundation

### 0.1 Host-Based Test Environment Setup

**Objective**: Enable running tests on developer machines without ESP32 hardware.

**Deliverables**:
- `test/host/` directory structure
- CMakeLists.txt for host compilation
- Unity integration for host builds
- Platform abstraction headers

**Directory Structure**:
```
test/
├── host/                          # NEW: Host-based tests
│   ├── CMakeLists.txt            # Host build configuration
│   ├── unity_config.h            # Unity configuration for host
│   ├── mocks/                    # Mock implementations
│   │   ├── mock_nvs.c/h          # NVS mock
│   │   ├── mock_esp_log.c/h      # Logging mock
│   │   ├── mock_freertos.c/h     # FreeRTOS stubs
│   │   ├── mock_i2c.c/h          # I2C mock
│   │   └── mock_gpio.c/h         # GPIO mock
│   ├── fakes/                    # Fake implementations (stateful)
│   │   ├── fake_nvs_storage.c/h  # In-memory NVS
│   │   └── fake_time.c/h         # Controllable time
│   ├── stubs/                    # ESP-IDF type definitions
│   │   ├── esp_err.h             # Error codes
│   │   ├── esp_log.h             # Log macros (redirected)
│   │   └── freertos/             # FreeRTOS types
│   └── test_*.c                  # Test files
├── target/                        # Renamed from current structure
│   └── ...                       # Existing target-based tests
└── CMakeLists.txt                # Top-level test config
```

**Implementation Steps**:

1. **Create stub headers for ESP-IDF types**
   - Extract type definitions without implementations
   - Enable host compilation of production code

2. **Implement mock framework**
   - Manual mocks for simple interfaces (GPIO, I2C reads/writes)
   - Auto-generated mocks (CMock) for complex interfaces
   - Fake NVS with in-memory storage for stateful testing

3. **Configure CMake for dual-target builds**
   - Host target: `cmake -DTARGET=host ..`
   - ESP32 target: continues using `idf.py build`

4. **Create test runner for host**
   - Executable that runs all host tests
   - Exit codes for CI integration

**Commit Milestone**: "feat(test): add host-based test infrastructure with mocking framework"

---

### 0.2 Pre-commit Hooks Setup

**Objective**: Automated quality gates before commits.

**Deliverables**:
- `.pre-commit-config.yaml`
- `scripts/pre-commit-checks.sh`

**Hooks to Implement**:

| Hook | Purpose | Tool |
|------|---------|------|
| Format check | Code style | clang-format (existing `.clang-format`) |
| Lint | Static analysis | cppcheck |
| Host tests | Unit test pass | CMake + Unity |
| Build check | Compilation | idf.py build (optional, slow) |

**Configuration**:
```yaml
# .pre-commit-config.yaml
repos:
  - repo: local
    hooks:
      - id: clang-format
        name: clang-format
        entry: clang-format --dry-run --Werror
        language: system
        files: \.(c|h)$

      - id: cppcheck
        name: cppcheck
        entry: cppcheck --error-exitcode=1 --enable=warning,style
        language: system
        files: \.(c|h)$

      - id: host-tests
        name: host unit tests
        entry: ./scripts/run-host-tests.sh
        language: script
        pass_filenames: false
```

**Commit Milestone**: "chore: add pre-commit hooks for formatting, linting, and tests"

---

### 0.3 CI/CD Foundation (GitHub Actions)

**Objective**: Automated PR validation.

**Deliverables**:
- `.github/workflows/pr-check.yml`
- `.github/workflows/build.yml`

**PR Check Workflow**:
```yaml
name: PR Checks
on: [pull_request]
jobs:
  lint:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install cppcheck
        run: sudo apt-get install -y cppcheck clang-format
      - name: Check formatting
        run: find main components -name "*.c" -o -name "*.h" | xargs clang-format --dry-run --Werror
      - name: Static analysis
        run: cppcheck --error-exitcode=1 main/ components/

  host-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build and run host tests
        run: |
          cd test/host
          cmake -B build
          cmake --build build
          ./build/run_tests

  esp-build:
    runs-on: ubuntu-latest
    container: espressif/idf:v5.4
    steps:
      - uses: actions/checkout@v4
      - name: Build firmware
        run: idf.py build
```

**Commit Milestone**: "ci: add GitHub Actions for PR validation and builds"

---

## Phase 1: Power Management Module Refactoring

### 1.1 Extract PowerManagement State Interface

**Objective**: Decouple power management from GlobalState.

**Current State** (`power_management_task.h`):
```c
typedef struct {
    uint16_t fan_perc;
    uint16_t fan_rpm;
    float chip_temp[6];
    float chip_temp_avg;
    // ... more fields
} PowerManagementModule;

// Embedded in GlobalState
GlobalState->POWER_MANAGEMENT_MODULE
```

**Target State**:
```c
// power_management.h (NEW)
typedef struct PowerManagementState PowerManagementState;

// Opaque handle - internal structure hidden
PowerManagementState* power_management_create(void);
void power_management_destroy(PowerManagementState* state);

// Thread-safe accessors
float power_management_get_chip_temp_avg(const PowerManagementState* state);
void power_management_set_chip_temp_avg(PowerManagementState* state, float temp);

// Operations
esp_err_t power_management_update_readings(PowerManagementState* state,
                                            const SensorReadings* readings);
esp_err_t power_management_apply_preset(PowerManagementState* state,
                                         const char* preset_name,
                                         DeviceModel device);
```

**Implementation Steps**:

1. **Create new header with opaque type**
   - File: `main/power_management/power_management.h`
   - Define public interface with thread-safe accessors

2. **Implement state container with mutex**
   - File: `main/power_management/power_management.c`
   - Internal struct with pthread_mutex_t
   - All accessors lock/unlock appropriately

3. **Extract pure functions for unit testing**
   - `calculate_auto_fan_speed(float chip_temp)` -> returns fan %
   - `should_trigger_overheat(float temp, float threshold)` -> returns bool
   - `calculate_efficiency(float power, float hashrate)` -> returns J/TH

4. **Create adapter for backward compatibility**
   - `power_management_from_global_state(GlobalState*)` -> returns handle
   - Allows incremental migration

**Commit Milestones**:
- "refactor(power): extract pure calculation functions"
- "refactor(power): add PowerManagementState with thread-safe accessors"
- "refactor(power): add backward-compatible adapter for GlobalState"

---

### 1.2 Fix Autotune Race Condition

**Objective**: Eliminate data races in autotune system.

**Current Problem** (`power_management_task.c:147-412`):
```c
static void autotuneOffset(GlobalState * GLOBAL_STATE) {
    // Direct access to shared state without synchronization
    uint8_t currentAsicTemp = (uint8_t)power->chip_temp_avg;  // RACE!
    float currentHashrate = system->current_hashrate;          // RACE!

    // Static variables without synchronization
    static uint8_t consecutiveLowHashrateAttempts = 0;         // RACE!
    static TickType_t lastAutotuneTime = 0;                    // RACE!
}
```

**Solution Design**:

```c
// autotune.h (NEW)
typedef struct {
    // Configuration (immutable after init)
    uint16_t warmup_seconds;
    uint8_t target_temp;
    float hashrate_threshold_percent;
    uint8_t max_consecutive_failures;

    // Limits (immutable after init)
    uint16_t max_frequency;
    uint16_t min_frequency;
    uint16_t max_voltage;
    uint16_t min_voltage;
} AutotuneConfig;

typedef struct {
    float chip_temp_avg;
    float current_hashrate;
    float target_hashrate;
    uint16_t current_frequency;
    uint16_t current_voltage;
    int16_t current_power;
    uint32_t uptime_seconds;
} AutotuneInputs;

typedef struct {
    bool should_adjust;
    uint16_t new_frequency;      // 0 = no change
    uint16_t new_voltage;        // 0 = no change
    bool should_reset_preset;
    const char* reset_preset_name;
} AutotuneDecision;

// Pure function - no side effects, fully testable
AutotuneDecision autotune_calculate(
    const AutotuneConfig* config,
    const AutotuneInputs* inputs,
    uint8_t consecutive_low_hashrate,
    uint32_t ms_since_last_adjustment
);
```

**Implementation Steps**:

1. **Extract autotune logic to pure function**
   - No global state access
   - No static variables
   - Returns decision struct, doesn't apply it

2. **Create AutotuneState with mutex**
   - Holds consecutive counters, timing
   - Thread-safe accessors

3. **Update task to use new interface**
   - Collect inputs atomically
   - Call pure function
   - Apply decision with proper locking

4. **Add comprehensive unit tests**
   - Test each temperature scenario
   - Test consecutive failure handling
   - Test timing boundaries

**Test Cases**:
```c
TEST_CASE("autotune_calculate returns no_change during warmup", "[autotune]")
TEST_CASE("autotune_calculate increases frequency when temp under target", "[autotune]")
TEST_CASE("autotune_calculate decreases frequency when temp over target", "[autotune]")
TEST_CASE("autotune_calculate triggers reset after 3 low hashrate events", "[autotune]")
TEST_CASE("autotune_calculate respects frequency limits", "[autotune]")
TEST_CASE("autotune_calculate respects voltage limits", "[autotune]")
```

**Commit Milestones**:
- "refactor(autotune): extract pure calculation function"
- "test(autotune): add comprehensive unit tests for autotune logic"
- "fix(autotune): eliminate race conditions with proper state management"

---

### 1.3 Extract Overheat Handling

**Objective**: Consolidate duplicated overheat logic.

**Current Problem** (`power_management_task.c:686-791`):
- Same overheat handling duplicated for DEVICE_MAX, DEVICE_ULTRA/SUPRA, DEVICE_GAMMA
- ~100 lines repeated 3 times with minor variations

**Solution**:
```c
// overheat.h (NEW)
typedef struct {
    float chip_temp;
    float vr_temp;
    float throttle_temp;
    float vr_throttle_temp;
    uint16_t frequency;
    uint16_t voltage;
    DeviceModel device_model;
    int board_version;
} OverheatCheckInputs;

typedef enum {
    OVERHEAT_NONE,
    OVERHEAT_SOFT,    // Standard recovery
    OVERHEAT_HARD     // Task termination
} OverheatSeverity;

// Pure function
OverheatSeverity check_overheat_condition(
    const OverheatCheckInputs* inputs,
    uint16_t current_overheat_count
);

// Action function (has side effects but testable with mocks)
esp_err_t execute_overheat_recovery(
    OverheatSeverity severity,
    DeviceModel device_model,
    int board_version,
    PowerControlInterface* power_ctrl  // Mockable interface
);
```

**Commit Milestones**:
- "refactor(overheat): extract overheat condition checking"
- "refactor(overheat): consolidate recovery logic"
- "test(overheat): add unit tests for overheat detection"

---

### 1.4 Unit Tests for Power Management

**Test Files to Create**:
```
test/host/
├── test_power_calculations.c     # Pure calculation tests
├── test_autotune.c               # Autotune logic tests
├── test_overheat.c               # Overheat detection tests
└── test_fan_control.c            # Fan speed calculation tests
```

**Coverage Targets**:
- `automatic_fan_speed()` - 100%
- `autotuneOffset()` logic - 100%
- `check_overheat_condition()` - 100%
- `apply_preset()` - 100%
- `handle_overheat_recovery()` - 80% (hardware interactions mocked)

**Commit Milestone**: "test(power): achieve 80% coverage for power management module"

---

## Phase 2: Unified Logging Module

### 2.1 Design Logging Interface

**Objective**: Single API for logging to Serial, WebUI, Screen, and Database.

**Current State**:
- `ESP_LOGI/E/W/D()` for serial only
- `dataBase_log_event()` for database only
- No WebUI or Screen logging

**Target API**:
```c
// logging.h (NEW)
typedef enum {
    LOG_DEST_SERIAL   = (1 << 0),
    LOG_DEST_DATABASE = (1 << 1),
    LOG_DEST_WEBUI    = (1 << 2),
    LOG_DEST_SCREEN   = (1 << 3),
    LOG_DEST_ALL      = 0xFF
} LogDestination;

typedef enum {
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_TRACE
} LogLevel;

typedef enum {
    LOG_CAT_SYSTEM,
    LOG_CAT_POWER,
    LOG_CAT_MINING,
    LOG_CAT_NETWORK,
    LOG_CAT_ASIC,
    LOG_CAT_API
} LogCategory;

// Configuration per category
void log_set_level(LogCategory category, LogLevel level);
void log_set_destinations(LogCategory category, LogDestination destinations);

// Main logging function
void log_message(LogCategory category, LogLevel level,
                 const char* format, ...) __attribute__((format(printf, 3, 4)));

// Convenience macros
#define LOG_ERROR(cat, fmt, ...) log_message(cat, LOG_LEVEL_ERROR, fmt, ##__VA_ARGS__)
#define LOG_WARN(cat, fmt, ...)  log_message(cat, LOG_LEVEL_WARN, fmt, ##__VA_ARGS__)
#define LOG_INFO(cat, fmt, ...)  log_message(cat, LOG_LEVEL_INFO, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(cat, fmt, ...) log_message(cat, LOG_LEVEL_DEBUG, fmt, ##__VA_ARGS__)

// Structured logging for database
void log_event(LogCategory category, LogLevel level,
               const char* message, const char* json_data);
```

**Implementation Architecture**:
```
                    log_message()
                         │
                         ▼
                 ┌───────────────┐
                 │  Log Router   │
                 │  (by config)  │
                 └───────────────┘
                         │
        ┌────────────────┼────────────────┬────────────────┐
        ▼                ▼                ▼                ▼
   ┌─────────┐    ┌───────────┐    ┌──────────┐    ┌─────────┐
   │ Serial  │    │  Database │    │  WebUI   │    │ Screen  │
   │ Backend │    │  Backend  │    │ Backend  │    │ Backend │
   └─────────┘    └───────────┘    └──────────┘    └─────────┘
        │                │                │                │
        ▼                ▼                ▼                ▼
    ESP_LOG*      dataBase_log*     WebSocket        BAP Send
```

**Implementation Steps**:

1. **Create logging core**
   - Thread-safe message routing
   - Configuration storage
   - Format string handling

2. **Implement backends**
   - Serial: wrap ESP_LOG macros
   - Database: wrap existing dataBase_log_event
   - WebUI: push via WebSocket
   - Screen: send via BAP protocol

3. **Add log rotation and filtering**
   - Database log rotation (existing)
   - Memory-efficient ring buffer for recent logs
   - Level filtering per destination

4. **Migration helpers**
   - Macro to replace ESP_LOGI with LOG_INFO
   - Gradual migration of existing code

**Commit Milestones**:
- "feat(logging): add unified logging interface and router"
- "feat(logging): implement serial and database backends"
- "feat(logging): implement WebUI logging via WebSocket"
- "feat(logging): implement screen logging via BAP"
- "test(logging): add unit tests for log routing and filtering"

---

### 2.2 Logging Tests

**Test Files**:
```
test/host/
├── test_log_routing.c      # Routing logic tests
├── test_log_formatting.c   # Format string handling
└── test_log_filtering.c    # Level/destination filtering
```

**Coverage Targets**: 90% for logging module

---

## Phase 3: Database Module Enhancement

### 3.1 Abstract Database Interface

**Objective**: Testable database layer with clear interface.

**Current Issues**:
- Direct SPIFFS access throughout
- JSON parsing mixed with business logic
- No mock-ability for testing

**Target Interface**:
```c
// database_interface.h (NEW)
typedef struct DatabaseOps {
    esp_err_t (*init)(void);
    esp_err_t (*read_json)(const char* path, cJSON** out);
    esp_err_t (*write_json)(const char* path, const cJSON* data);
    esp_err_t (*delete_file)(const char* path);
    esp_err_t (*file_exists)(const char* path, bool* exists);
    esp_err_t (*get_free_space)(size_t* bytes);
} DatabaseOps;

// Production implementation
extern const DatabaseOps spiffs_database_ops;

// Test implementation
extern const DatabaseOps memory_database_ops;

// Initialize with chosen implementation
esp_err_t database_init(const DatabaseOps* ops);
```

**Implementation Steps**:

1. **Extract interface from existing code**
2. **Create SPIFFS implementation**
3. **Create in-memory implementation for tests**
4. **Refactor existing code to use interface**
5. **Add comprehensive tests**

**Commit Milestones**:
- "refactor(database): extract database operations interface"
- "feat(database): add in-memory implementation for testing"
- "test(database): add unit tests with mock database"

---

### 3.2 Log Storage Improvements

**Objective**: Robust log storage with rotation.

**Features**:
- Configurable max log size
- Automatic rotation
- Efficient querying
- Error log persistence across reboots

**Commit Milestone**: "feat(database): improve log rotation and querying"

---

## Phase 4: BAP API Refactoring

### 4.1 Extract Byte Serialization

**Objective**: Clean, testable byte manipulation.

**Current Problem** (`lvglDisplayBAP.c`):
- 900+ lines with large switch statements
- Inline byte packing
- Difficult to verify correctness

**Solution**:
```c
// bap_protocol.h (NEW)
typedef struct {
    uint8_t preamble[2];  // 0xFF, 0xAA
    uint8_t reg;
    uint8_t length;
    uint8_t data[256];
    uint16_t crc;
} BapPacket;

// Serialization functions (pure, testable)
size_t bap_serialize_float(uint8_t* buf, float value);
size_t bap_serialize_u16(uint8_t* buf, uint16_t value);
size_t bap_serialize_u32(uint8_t* buf, uint32_t value);
size_t bap_serialize_string(uint8_t* buf, const char* str, size_t max_len);

// Deserialization functions (pure, testable)
float bap_deserialize_float(const uint8_t* buf);
uint16_t bap_deserialize_u16(const uint8_t* buf);
uint32_t bap_deserialize_u32(const uint8_t* buf);
size_t bap_deserialize_string(const uint8_t* buf, char* out, size_t max_len);

// Packet building (pure, testable)
esp_err_t bap_build_packet(BapPacket* packet, uint8_t reg,
                           const void* data, size_t data_len);
uint16_t bap_calculate_crc(const uint8_t* data, size_t length);
bool bap_validate_packet(const BapPacket* packet);

// High-level data structures for each register type
typedef struct {
    float voltage;
    float current;
    float power;
    float vcore_mv;
} BapPowerStats;

size_t bap_serialize_power_stats(uint8_t* buf, const BapPowerStats* stats);
```

**Implementation Steps**:

1. **Extract CRC calculation** (already exists but not isolated)
2. **Create serialization primitives**
3. **Create deserialization primitives**
4. **Build high-level data structures**
5. **Refactor existing code to use new primitives**

**Commit Milestones**:
- "refactor(bap): extract byte serialization primitives"
- "refactor(bap): create high-level data structures for registers"
- "test(bap): add unit tests for serialization/deserialization"
- "refactor(bap): simplify lvglDisplayBAP.c using new primitives"

---

### 4.2 Register Handler Refactoring

**Objective**: Replace giant switch statement with handler table.

**Current** (700+ line switch):
```c
switch (buf[2]) {
    case LVGL_REG_SETTINGS_HOSTNAME:
        // 10 lines of handling
        break;
    case LVGL_REG_SETTINGS_WIFI_SSID:
        // 10 lines of handling
        break;
    // ... 30+ more cases
}
```

**Target**:
```c
// bap_handlers.h
typedef esp_err_t (*BapRegisterHandler)(const uint8_t* data, uint8_t len, void* ctx);

typedef struct {
    uint8_t reg;
    const char* name;
    BapRegisterHandler handler;
    size_t expected_min_len;
    size_t expected_max_len;
} BapRegisterDefinition;

// Individual handlers (each testable in isolation)
esp_err_t handle_hostname(const uint8_t* data, uint8_t len, void* ctx);
esp_err_t handle_wifi_ssid(const uint8_t* data, uint8_t len, void* ctx);
esp_err_t handle_asic_voltage(const uint8_t* data, uint8_t len, void* ctx);
// ... etc

// Registration table
static const BapRegisterDefinition bap_registers[] = {
    {LVGL_REG_SETTINGS_HOSTNAME, "hostname", handle_hostname, 1, 63},
    {LVGL_REG_SETTINGS_WIFI_SSID, "wifi_ssid", handle_wifi_ssid, 1, 63},
    // ...
};

// Dispatch function
esp_err_t bap_dispatch_register(uint8_t reg, const uint8_t* data, uint8_t len, void* ctx);
```

**Commit Milestones**:
- "refactor(bap): create register handler table"
- "refactor(bap): extract individual register handlers"
- "test(bap): add unit tests for each register handler"

---

## Phase 5: HTTP API Enhancement

### 5.1 Extract Request Handlers

**Objective**: Testable HTTP handlers.

**Current Problem** (`http_server.c` - 57KB):
- Monolithic file
- Business logic mixed with HTTP handling
- Difficult to test

**Solution**:
```c
// api_handlers/system.h
typedef struct {
    // Input (from request)
    const char* json_body;
    size_t body_len;
} ApiRequest;

typedef struct {
    // Output (to response)
    char* response_body;
    size_t response_len;
    int status_code;
    const char* content_type;
} ApiResponse;

// Pure handler functions (testable without HTTP server)
esp_err_t api_get_system_info(const ApiRequest* req, ApiResponse* res, void* ctx);
esp_err_t api_patch_system(const ApiRequest* req, ApiResponse* res, void* ctx);
esp_err_t api_post_restart(const ApiRequest* req, ApiResponse* res, void* ctx);
```

**File Structure**:
```
main/http_server/
├── http_server.c          # HTTP plumbing only
├── api_handlers/
│   ├── system_handlers.c  # /api/system/*
│   ├── swarm_handlers.c   # /api/swarm/*
│   ├── theme_handlers.c   # /api/themes/*
│   └── logs_handlers.c    # /api/logs/*
└── api_router.c           # Route registration
```

**Commit Milestones**:
- "refactor(api): extract system handlers"
- "refactor(api): extract swarm handlers"
- "test(api): add unit tests for API handlers"

---

## Phase 6: Global State Migration

### 6.1 Incremental State Extraction

**Objective**: Gradually move from monolithic GlobalState to module-owned state.

**Migration Order**:
1. PowerManagementModule (Phase 1)
2. AutotuneModule (Phase 1)
3. SystemModule (Phase 6)
4. AsicTaskModule (Phase 6)
5. SelfTestModule (Phase 6)

**Strategy for Each Module**:
1. Create opaque state type with accessors
2. Add backward-compatible accessor to GlobalState
3. Update callers one at a time
4. Remove from GlobalState when all callers migrated

**Example Migration**:
```c
// Before
float temp = GLOBAL_STATE->POWER_MANAGEMENT_MODULE.chip_temp_avg;

// Step 1: Add accessor
float temp = power_management_get_chip_temp_avg(
    global_state_get_power_management(GLOBAL_STATE));

// Step 2: Module owns state
float temp = power_management_get_chip_temp_avg(g_power_management);
```

**Commit Milestones**:
- "refactor(state): extract SystemModule state"
- "refactor(state): extract AsicTaskModule state"
- "refactor(state): remove deprecated GlobalState fields"

---

## Commit Strategy

### Commit Frequency
- **Per logical change**: One commit per completed refactoring step
- **Milestone commits**: Marked with specific messages for traceability

### Commit Message Format
```
<type>(<scope>): <description>

[optional body]

[optional footer]
```

**Types**: feat, fix, refactor, test, docs, chore, ci

### Example Commit Sequence for Phase 1.2:
```
1. refactor(autotune): extract AutotuneInputs struct
2. refactor(autotune): extract AutotuneDecision struct
3. refactor(autotune): create pure autotune_calculate function
4. test(autotune): add tests for warmup period
5. test(autotune): add tests for temperature-based adjustment
6. test(autotune): add tests for hashrate failure recovery
7. refactor(autotune): add AutotuneState with mutex
8. refactor(autotune): update task to use new autotune module
9. fix(autotune): eliminate race conditions (DONE)
```

---

## Success Criteria

### Phase 0 Complete When:
- [ ] Host tests run without hardware
- [ ] Pre-commit hooks catch formatting issues
- [ ] CI/CD runs on every PR

### Phase 1 Complete When:
- [ ] Power management functions are pure and tested
- [ ] Autotune race condition eliminated
- [ ] 80% coverage on power management module

### Phase 2 Complete When:
- [ ] Unified logging API available
- [ ] All 4 destinations functional
- [ ] Existing ESP_LOG calls migrated

### Phase 3 Complete When:
- [ ] Database operations mockable
- [ ] Log rotation working correctly
- [ ] 80% coverage on database module

### Phase 4 Complete When:
- [ ] BAP serialization is testable
- [ ] Register handlers are isolated
- [ ] 80% coverage on BAP module

### Phase 5 Complete When:
- [ ] HTTP handlers are testable without server
- [ ] API handlers have unit tests
- [ ] 80% coverage on API handlers

### Phase 6 Complete When:
- [ ] No modules directly access GlobalState fields
- [ ] All module state is thread-safe
- [ ] GlobalState is minimal coordinator only

---

## Risk Mitigation

### Risk: Breaking Production Functionality
**Mitigation**:
- Each phase has backward-compatible adapters
- Integration tests on real hardware between phases
- Feature flags for new vs old code paths during migration

### Risk: Scope Creep
**Mitigation**:
- Strict phase boundaries
- "Done" criteria before starting next phase
- Resist unrelated improvements during refactoring

### Risk: Test Maintenance Burden
**Mitigation**:
- Focus on testing pure functions
- Minimal mocking (prefer fakes)
- Test behavior, not implementation

---

## Timeline Summary

| Phase | Focus | Key Deliverables |
|-------|-------|------------------|
| 0 | Infrastructure | Host tests, CI/CD, hooks |
| 1 | Power Management | Race condition fix, 80% coverage |
| 2 | Logging | Unified API, 4 destinations |
| 3 | Database | Interface abstraction, improved rotation |
| 4 | BAP API | Byte serialization, handler table |
| 5 | HTTP API | Handler extraction, unit tests |
| 6 | Global State | Module-owned state, thread safety |

---

## Owner Decisions (Captured 2026-01-02)

1. **Test Hardware Availability**: No dedicated test devices available to primary dev. **Decision: Prioritize host-based tests for round 1.** Hardware testing will be coordinated with secondary dev who has test devices.

2. **WebSocket Implementation**: **Decision: Use existing WebSocket infrastructure** for WebUI logging (DRY principle).

3. **Logging Volume Concerns**: **Decision: Default to conservative log levels (WARN+ERROR only)** for most categories to protect embedded device performance.

4. **BAP Protocol Versioning**: **Decision: Yes, add protocol version byte** for future compatibility.

5. **Breaking Change Window**: **Decision: No preference** - changes can land as ready.

---

*Document Version: 1.1*
*Last Updated: 2026-01-02*
*Status: APPROVED - Ready for Implementation*
