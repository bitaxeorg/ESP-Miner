#ifndef VOLTAGE_MONITOR_H
#define VOLTAGE_MONITOR_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// Configuration constants
#define VOLTAGE_MONITOR_ENABLED 1
#define VOLTAGE_SCAN_INTERVAL_MS 5000  // 5 seconds between full scans
#define VOLTAGE_SETTLING_TIME_MS 2     // Mux settling time

// Hardware configuration
#define MAX_CHAINS_PER_BOARD 7
#define MAX_ASICS_PER_CHAIN 12  // Will expand to 40 later
#define VOLTAGE_MONITOR_I2C_NUM I2C_NUM_0
#define ADS1115_ADDR 0x48

// Mux control pins - adjust based on your hardware
#define MUX1_S0 GPIO_NUM_4
#define MUX1_S1 GPIO_NUM_5
#define MUX1_S2 GPIO_NUM_6
#define MUX2_S0 GPIO_NUM_7
#define MUX2_S1 GPIO_NUM_8
#define MUX2_S2 GPIO_NUM_9

// Voltage thresholds
#define ASIC_VOLTAGE_MIN 0.8f
#define ASIC_VOLTAGE_MAX 1.6f
#define ASIC_VOLTAGE_NOMINAL 1.2f

typedef struct {
    float voltage;
    uint32_t timestamp;
    bool is_valid;
} asic_voltage_t;

typedef struct {
    asic_voltage_t asics[MAX_ASICS_PER_CHAIN];
    float total_voltage;
    float average_voltage;
    float min_voltage;
    float max_voltage;
    uint8_t failed_asics;
    uint8_t asic_count;
} chain_voltage_data_t;

typedef struct {
    chain_voltage_data_t chains[MAX_CHAINS_PER_BOARD];
    uint8_t chain_count;
    bool monitoring_enabled;
    bool hardware_present;
} voltage_monitor_t;

// Public API
esp_err_t voltage_monitor_init(void);
bool voltage_monitor_is_present(void);
esp_err_t voltage_monitor_get_chain_data(uint8_t chain_id, chain_voltage_data_t *data);
float voltage_monitor_get_asic_voltage(uint8_t chain_id, uint8_t asic_id);
uint16_t voltage_monitor_suggest_frequency(uint8_t chain_id);
char* voltage_monitor_get_json_status(void);
void voltage_monitor_set_scan_interval(uint32_t interval_ms);

// Future API for self-healing
typedef enum {
    ASIC_STATUS_OK,
    ASIC_STATUS_LOW_VOLTAGE,
    ASIC_STATUS_HIGH_VOLTAGE,
    ASIC_STATUS_OPEN_CIRCUIT,
    ASIC_STATUS_SHORT_CIRCUIT
} asic_status_t;

#endif // VOLTAGE_MONITOR_H
