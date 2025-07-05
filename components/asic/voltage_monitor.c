#include "voltage_monitor.h"
#include "esp_log.h"
// #include "driver/i2c.h"
#include "i2c_bitaxe.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "cJSON.h"
#include "esp_timer.h"

static const char *TAG = "voltage_monitor";

// Global state
static voltage_monitor_t g_monitor = {0};
static SemaphoreHandle_t g_data_mutex = NULL;
static TaskHandle_t g_monitor_task_handle = NULL;
static uint32_t g_scan_interval_ms = VOLTAGE_SCAN_INTERVAL_MS;
static i2c_master_dev_handle_t ads1115_handle = NULL;

// Forward declarations of static functions
static esp_err_t check_ads1115_present(void);
static esp_err_t init_mux_gpio(void);
static esp_err_t configure_ads1115(void);
static void select_mux_channel(uint8_t first_stage, uint8_t second_stage);
static float read_ads1115_voltage(void);
static void voltage_monitor_task(void *pvParameters);

// Mux channel mapping for cascaded configuration
// For current 12 ASIC design (will expand for 40 later)
static const uint8_t asic_to_mux_map[12][2] = {
    {0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7},
    {1, 0}, {1, 1}, {1, 2}, {1, 3}
};

static esp_err_t check_ads1115_present(void) {
    // First try to add the device
    esp_err_t ret = i2c_bitaxe_add_device(ADS1115_ADDR, &ads1115_handle, "ADS1115");
    if (ret != ESP_OK) {
        return ret;
    }

    // Now try to actually communicate with it
    // Try to read from the ADS1115 config register
    uint8_t data[2];
    ret = i2c_bitaxe_register_read(ads1115_handle, 0x01, data, 2);  // Config register

    if (ret != ESP_OK) {
        // Device didn't respond, clean up the handle
        ads1115_handle = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t configure_ads1115(void) {
    // Configure for continuous conversion, ±4.096V range
    uint8_t config[3] = {
        0x01,  // Config register
        0xC3,  // MSB: Start conversion, AIN0, ±4.096V, continuous
        0x83   // LSB: 128 SPS, traditional comparator
    };
    
    // Write configuration
    return i2c_bitaxe_register_write_bytes(ads1115_handle, config, 3);
}

static esp_err_t init_mux_gpio(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL<<MUX1_S0) | (1ULL<<MUX1_S1) | (1ULL<<MUX1_S2) |
                        (1ULL<<MUX2_S0) | (1ULL<<MUX2_S1) | (1ULL<<MUX2_S2),
        .pull_down_en = 0,
        .pull_up_en = 0
    };
    return gpio_config(&io_conf);
}

static void select_mux_channel(uint8_t first_stage, uint8_t second_stage) {
    gpio_set_level(MUX2_S0, (first_stage >> 0) & 1);
    gpio_set_level(MUX2_S1, (first_stage >> 1) & 1);
    gpio_set_level(MUX2_S2, (first_stage >> 2) & 1);
    
    gpio_set_level(MUX1_S0, (second_stage >> 0) & 1);
    gpio_set_level(MUX1_S1, (second_stage >> 1) & 1);
    gpio_set_level(MUX1_S2, (second_stage >> 2) & 1);
    
    vTaskDelay(pdMS_TO_TICKS(VOLTAGE_SETTLING_TIME_MS));
}

static float read_ads1115_voltage(void) {
    uint8_t data[2];

    // Point to conversion register
    esp_err_t ret = i2c_bitaxe_register_write_addr(ads1115_handle, 0x00);
    if (ret != ESP_OK) {
        return -1.0f;
    }

    // Read conversion result
    ret = i2c_bitaxe_register_read(ads1115_handle, 0x00, data, 2);
    if (ret != ESP_OK) {
        return -1.0f;
    }

    int16_t raw = (data[0] << 8) | data[1];
    float voltage = (raw * 4.096f) / 32768.0f;

    return voltage;
}

static void voltage_monitor_task(void *pvParameters) {
    ESP_LOGI(TAG, "Voltage monitor task started");
    
    while (1) {
        if (!g_monitor.hardware_present) {
            vTaskDelay(pdMS_TO_TICKS(10000));  // Check every 10s if hardware appears
            if (check_ads1115_present() == ESP_OK) {
                g_monitor.hardware_present = true;
                ESP_LOGI(TAG, "Voltage monitor hardware detected");
            }
            continue;
        }
        
        // Scan all chains
        for (int chain = 0; chain < g_monitor.chain_count; chain++) {
            chain_voltage_data_t *chain_data = &g_monitor.chains[chain];
            
            // Reset statistics
            chain_data->total_voltage = 0;
            chain_data->min_voltage = 999.0f;
            chain_data->max_voltage = 0;
            chain_data->failed_asics = 0;
            
            // Read each ASIC
            for (int asic = 0; asic < chain_data->asic_count; asic++) {
                // Map to physical mux channels
                int global_asic = chain * MAX_ASICS_PER_CHAIN + asic;
                if (global_asic >= 12) break;  // Current hardware limit
                
                uint8_t first_stage = asic_to_mux_map[global_asic][0];
                uint8_t second_stage = asic_to_mux_map[global_asic][1];
                
                select_mux_channel(first_stage, second_stage);
                float voltage = read_ads1115_voltage();
                
                if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                    chain_data->asics[asic].voltage = voltage;
                    chain_data->asics[asic].timestamp = esp_timer_get_time() / 1000;
                    
                    if (voltage > ASIC_VOLTAGE_MIN && voltage < ASIC_VOLTAGE_MAX) {
                        chain_data->asics[asic].is_valid = true;
                        chain_data->total_voltage += voltage;
                        
                        if (voltage < chain_data->min_voltage) {
                            chain_data->min_voltage = voltage;
                        }
                        if (voltage > chain_data->max_voltage) {
                            chain_data->max_voltage = voltage;
                        }
                    } else {
                        chain_data->asics[asic].is_valid = false;
                        chain_data->failed_asics++;
                    }
                    
                    xSemaphoreGive(g_data_mutex);
                }
            }
            
            // Calculate average
            if (chain_data->asic_count > chain_data->failed_asics) {
                chain_data->average_voltage = chain_data->total_voltage / 
                                            (chain_data->asic_count - chain_data->failed_asics);
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(g_scan_interval_ms));
    }
}

esp_err_t voltage_monitor_init(void) {
    ESP_LOGI(TAG, "Initializing voltage monitor");
    
    if (!VOLTAGE_MONITOR_ENABLED) {
        ESP_LOGW(TAG, "Voltage monitor disabled in configuration");
        return ESP_OK;
    }
    
    // Create mutex
    g_data_mutex = xSemaphoreCreateMutex();
    if (g_data_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // Initialize GPIO for mux control
    esp_err_t ret = init_mux_gpio();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to initialize mux GPIO: %s", esp_err_to_name(ret));
        ESP_LOGW(TAG, "Voltage monitoring will be disabled");
        return ESP_OK;  // Not a fatal error
    }
    
    // Check if ADS1115 is present
    ret = check_ads1115_present();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "ADS1115 not detected on I2C bus");
        ESP_LOGW(TAG, "Voltage monitoring will run in detection mode");
        g_monitor.hardware_present = false;
    } else {
        // Configure ADS1115
        ret = configure_ads1115();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to configure ADS1115: %s", esp_err_to_name(ret));
            g_monitor.hardware_present = false;
        } else {
            g_monitor.hardware_present = true;
            ESP_LOGI(TAG, "Voltage monitor hardware initialized successfully");
        }
    }
    
    // Initialize data structures
    g_monitor.chain_count = 3;  // Default for current hardware
    for (int i = 0; i < g_monitor.chain_count; i++) {
        g_monitor.chains[i].asic_count = 4;  // 12 ASICs / 3 chains
    }
    g_monitor.monitoring_enabled = true;
    
    // Create monitoring task
    xTaskCreate(voltage_monitor_task, 
                "voltage_monitor", 
                4096, 
                NULL, 
                5, 
                &g_monitor_task_handle);
    
    return ESP_OK;
}

bool voltage_monitor_is_present(void) {
    return g_monitor.hardware_present && g_monitor.monitoring_enabled;
}

esp_err_t voltage_monitor_get_chain_data(uint8_t chain_id, chain_voltage_data_t *data) {
    if (!g_monitor.monitoring_enabled || chain_id >= g_monitor.chain_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memcpy(data, &g_monitor.chains[chain_id], sizeof(chain_voltage_data_t));
        xSemaphoreGive(g_data_mutex);
        return ESP_OK;
    }
    
    return ESP_ERR_TIMEOUT;
}

float voltage_monitor_get_asic_voltage(uint8_t chain_id, uint8_t asic_id) {
    if (!g_monitor.monitoring_enabled || 
        chain_id >= g_monitor.chain_count ||
        asic_id >= g_monitor.chains[chain_id].asic_count) {
        return -1.0f;
    }
    
    float voltage = -1.0f;
    if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        voltage = g_monitor.chains[chain_id].asics[asic_id].voltage;
        xSemaphoreGive(g_data_mutex);
    }
    
    return voltage;
}

uint16_t voltage_monitor_suggest_frequency(uint8_t chain_id) {
    if (!g_monitor.monitoring_enabled || chain_id >= g_monitor.chain_count) {
        return 490;  // Default frequency
    }
    
    float min_voltage = g_monitor.chains[chain_id].min_voltage;
    
    // Frequency selection based on minimum voltage in chain
    if (min_voltage >= 1.35f) return 575;
    else if (min_voltage >= 1.30f) return 550;
    else if (min_voltage >= 1.25f) return 525;
    else if (min_voltage >= 1.20f) return 500;
    else if (min_voltage >= 1.166f) return 490;
    else return 475;  // Conservative fallback
}

char* voltage_monitor_get_json_status(void) {
    cJSON *root = cJSON_CreateObject();
    
    cJSON_AddBoolToObject(root, "enabled", g_monitor.monitoring_enabled);
    cJSON_AddBoolToObject(root, "hardware_present", g_monitor.hardware_present);
    cJSON_AddNumberToObject(root, "scan_interval_ms", g_scan_interval_ms);
    
    if (g_monitor.monitoring_enabled && g_monitor.hardware_present) {
        cJSON *chains = cJSON_CreateArray();
        
        if (xSemaphoreTake(g_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            for (int c = 0; c < g_monitor.chain_count; c++) {
                cJSON *chain = cJSON_CreateObject();
                chain_voltage_data_t *data = &g_monitor.chains[c];
                
                cJSON_AddNumberToObject(chain, "chain_id", c);
                cJSON_AddNumberToObject(chain, "total_voltage", data->total_voltage);
                cJSON_AddNumberToObject(chain, "average_voltage", data->average_voltage);
                cJSON_AddNumberToObject(chain, "min_voltage", data->min_voltage);
                cJSON_AddNumberToObject(chain, "max_voltage", data->max_voltage);
                cJSON_AddNumberToObject(chain, "failed_asics", data->failed_asics);
                cJSON_AddNumberToObject(chain, "suggested_frequency", 
                                      voltage_monitor_suggest_frequency(c));
                
                cJSON *asics = cJSON_CreateArray();
                for (int a = 0; a < data->asic_count; a++) {
                    cJSON *asic = cJSON_CreateObject();
                    cJSON_AddNumberToObject(asic, "id", a);
                    cJSON_AddNumberToObject(asic, "voltage", data->asics[a].voltage);
                    cJSON_AddBoolToObject(asic, "valid", data->asics[a].is_valid);
                    cJSON_AddItemToArray(asics, asic);
                }
                
                cJSON_AddItemToObject(chain, "asics", asics);
                cJSON_AddItemToArray(chains, chain);
            }
            xSemaphoreGive(g_data_mutex);
        }
        
        cJSON_AddItemToObject(root, "chains", chains);
    }
    
    char *json = cJSON_Print(root);
    cJSON_Delete(root);
    return json;
}

void voltage_monitor_set_scan_interval(uint32_t interval_ms) {
    if (interval_ms >= 100 && interval_ms <= 60000) {
        g_scan_interval_ms = interval_ms;
        ESP_LOGI(TAG, "Scan interval set to %d ms", interval_ms);
    }
}
