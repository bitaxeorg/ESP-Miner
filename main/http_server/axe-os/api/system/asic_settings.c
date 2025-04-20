#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "cJSON.h"
#include "global_state.h"
#include "asic.h"

static const char *TAG = "asic_api";
static GlobalState *GLOBAL_STATE = NULL;

// Function declarations from http_server.c
extern esp_err_t is_network_allowed(httpd_req_t *req);
extern esp_err_t set_cors_headers(httpd_req_t *req);

// Initialize the ASIC API with the global state
void asic_api_init(GlobalState *global_state) {
    GLOBAL_STATE = global_state;
}

/* Handler for system asic endpoint */
esp_err_t GET_system_asic(httpd_req_t *req)
{
    if (is_network_allowed(req) != ESP_OK) {
        return httpd_resp_send_err(req, HTTPD_401_UNAUTHORIZED, "Unauthorized");
    }

    httpd_resp_set_type(req, "application/json");

    // Set CORS headers
    if (set_cors_headers(req) != ESP_OK) {
        httpd_resp_send_500(req);
        return ESP_OK;
    }

    cJSON *root = cJSON_CreateObject();
    
    // Add ASIC model to the JSON object
    cJSON_AddStringToObject(root, "ASICModel", GLOBAL_STATE->asic_model_str);
    
    // Create arrays for frequency and voltage options based on ASIC model
    cJSON *freqOptions = cJSON_CreateArray();
    cJSON *voltageOptions = cJSON_CreateArray();
    
    // Set different frequency and voltage options based on ASIC model
    if (strcmp(GLOBAL_STATE->asic_model_str, "BM1370") == 0) {
        // BM1370 frequency options
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(400));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(490));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(525));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(550));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(600));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(625));
        
        // BM1370 voltage options
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1000));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1060));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1100));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1150));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1200));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1250));
    }
    else if (strcmp(GLOBAL_STATE->asic_model_str, "BM1368") == 0) {
        // BM1368 frequency options
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(400));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(425));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(450));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(475));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(485));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(500));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(525));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(550));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(575));
        
        // BM1368 voltage options
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1100));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1150));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1200));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1250));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1300));
    } 
    else if (strcmp(GLOBAL_STATE->asic_model_str, "BM1366") == 0) {
        // BM1366 frequency options
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(400));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(425));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(450));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(475));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(485));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(500));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(525));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(550));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(575));
        
        // BM1366 voltage options
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1100));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1150));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1200));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1250));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1300));
    }
    else if (strcmp(GLOBAL_STATE->asic_model_str, "BM1397") == 0) {
        // BM1397 frequency options
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(400));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(425));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(450));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(475));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(485));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(500));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(525));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(550));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(575));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(600));
        
        // BM1397 voltage options
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1100));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1150));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1200));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1250));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1300));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1350));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1400));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1450));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1500));
    }
    else {
        // Default options for other ASIC models
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(400));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(425));
        cJSON_AddItemToArray(freqOptions, cJSON_CreateNumber(450));
        
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1200));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1250));
        cJSON_AddItemToArray(voltageOptions, cJSON_CreateNumber(1300));
    }
    
    // Add the arrays to the response
    cJSON_AddItemToObject(root, "frequencyOptions", freqOptions);
    cJSON_AddItemToObject(root, "voltageOptions", voltageOptions);
    
    const char *response = cJSON_Print(root);
    httpd_resp_sendstr(req, response);

    free((void *)response);
    cJSON_Delete(root);
    return ESP_OK;
}
