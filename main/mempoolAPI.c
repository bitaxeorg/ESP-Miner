#include "mempoolAPI.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_http_client.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"

#define MAX_RESPONSE_SIZE 8192  // 8KB max response size

static char *responseBuffer = NULL;
static size_t responseLength = 0;

// At the top with other global variables
MempoolApiState MEMPOOL_STATE = {
    .priceTimestamp = 0,
    .priceUSD = 0,
    .priceValid = false,
    .blockHeight = 0,
    .blockHeightValid = false,
    .networkHashrate = 0.0,
    .networkHashrateValid = false,
    .networkDifficulty = 0.0,
    .networkDifficultyValid = false,
};

// Add getter function implementation
MempoolApiState* getMempoolState(void) {
    return &MEMPOOL_STATE;
}

static esp_err_t priceEventHandler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE("HTTP API", "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            if (responseBuffer != NULL) 
            {
                free(responseBuffer);
                responseBuffer = NULL;
                responseLength = 0;
            }
            break;
        case HTTP_EVENT_HEADERS_SENT:
            break;
        case HTTP_EVENT_ON_HEADER:
            break;
        case HTTP_EVENT_REDIRECT:
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI("HTTP API", "Receiving price data chunk: %d bytes", evt->data_len);

            if (responseBuffer == NULL) 
            {
                responseBuffer = malloc(MAX_RESPONSE_SIZE);
                if (responseBuffer == NULL) 
                {
                    ESP_LOGE("HTTP API", "Initial memory allocation failed!");
                    responseLength = 0;
                    return ESP_ERR_NO_MEM;
                }
                // Clear the buffer
                memset(responseBuffer, 0, MAX_RESPONSE_SIZE);
                responseLength = 0;  // Reset length when allocating new buffer
            }
            
            if (responseLength + evt->data_len > MAX_RESPONSE_SIZE) {
                ESP_LOGE("HTTP API", "Response too large, maximum size is %d bytes", MAX_RESPONSE_SIZE);
                if (responseBuffer != NULL) {
                    free(responseBuffer);
                    responseBuffer = NULL;
                }
                responseLength = 0;
                return ESP_ERR_NO_MEM;
            }

            if (responseBuffer == NULL) {
                responseBuffer = malloc(MAX_RESPONSE_SIZE);
                if (responseBuffer == NULL) {
                    ESP_LOGE("HTTP API", "Initial memory allocation failed!");
                    responseLength = 0;
                    return ESP_ERR_NO_MEM;
                }
            }

            memcpy(responseBuffer + responseLength, evt->data, evt->data_len);
            responseLength += evt->data_len;
            responseBuffer[responseLength] = '\0';
            break;

        case HTTP_EVENT_ON_FINISH:
            if (responseBuffer != NULL) {
                cJSON *json = cJSON_Parse(responseBuffer);
                if (json != NULL) {
                    cJSON *priceUSD = cJSON_GetObjectItem(json, "USD");
                    if (priceUSD != NULL && cJSON_IsNumber(priceUSD)) {
                        MEMPOOL_STATE.priceUSD = priceUSD->valueint;
                        MEMPOOL_STATE.priceValid = true;
                        ESP_LOGI("HTTP API", "Price USD: %d", priceUSD->valueint);
                    }
                    cJSON_Delete(json);
                }
                free(responseBuffer);
                responseBuffer = NULL;
                responseLength = 0;
            }
            break;
            
        case HTTP_EVENT_DISCONNECTED:
            if (responseBuffer != NULL) {
                free(responseBuffer);
                responseBuffer = NULL;
                responseLength = 0;
            }
            break;
    }
    return ESP_OK;
}

static esp_err_t blockHeightEventHandler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE("HTTP API", "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            if (responseBuffer != NULL) 
            {
                free(responseBuffer);
                responseBuffer = NULL;
                responseLength = 0;
            }
            break;
        case HTTP_EVENT_HEADERS_SENT:
            break;
        case HTTP_EVENT_ON_HEADER:
            break;
        case HTTP_EVENT_REDIRECT:
            break;
        case HTTP_EVENT_ON_DATA:
        if (responseBuffer == NULL) 
            {
                responseBuffer = malloc(MAX_RESPONSE_SIZE);
                if (responseBuffer == NULL) 
                {
                    ESP_LOGE("HTTP API", "Initial memory allocation failed!");
                    responseLength = 0;
                    return ESP_ERR_NO_MEM;
                }
                // Clear the buffer
                memset(responseBuffer, 0, MAX_RESPONSE_SIZE);
                responseLength = 0;  // Reset length when allocating new buffer
            }

            ESP_LOGI("HTTP API", "Receiving price data chunk: %d bytes", evt->data_len);
            
            if (responseLength + evt->data_len > MAX_RESPONSE_SIZE) {
                ESP_LOGE("HTTP API", "Response too large, maximum size is %d bytes", MAX_RESPONSE_SIZE);
                if (responseBuffer != NULL) {
                    free(responseBuffer);
                    responseBuffer = NULL;
                }
                responseLength = 0;
                return ESP_ERR_NO_MEM;
            }

            if (responseBuffer == NULL) {
                responseBuffer = malloc(MAX_RESPONSE_SIZE);
                if (responseBuffer == NULL) {
                    ESP_LOGE("HTTP API", "Initial memory allocation failed!");
                    responseLength = 0;
                    return ESP_ERR_NO_MEM;
                }
            }

            memcpy(responseBuffer + responseLength, evt->data, evt->data_len);
            responseLength += evt->data_len;
            responseBuffer[responseLength] = '\0';
            break;

        case HTTP_EVENT_ON_FINISH:
            if (responseBuffer != NULL) {
                char *endptr;
                uint32_t blockHeight = (uint32_t)strtoul(responseBuffer, &endptr, 10);
                if (endptr != responseBuffer && *endptr == '\0') {
                    MEMPOOL_STATE.blockHeight = blockHeight;
                    MEMPOOL_STATE.blockHeightValid = true;
                    ESP_LOGI("HTTP API", "Block height: %lu", blockHeight);
                } else {
                    ESP_LOGE("HTTP API", "Failed to parse block height");
                }
                free(responseBuffer);
                responseBuffer = NULL;
                responseLength = 0;
            }
            break;
            
        case HTTP_EVENT_DISCONNECTED:
            if (responseBuffer != NULL) {
                free(responseBuffer);
                responseBuffer = NULL;
                responseLength = 0;
            }
            break;
    }
    return ESP_OK;
}

static esp_err_t networkHashrateEventHandler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) 
    {
        case HTTP_EVENT_ERROR:
            ESP_LOGE("HTTP API", "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            if (responseBuffer != NULL) 
            {
                free(responseBuffer);
                responseBuffer = NULL;
                responseLength = 0;
            }
            break;
        case HTTP_EVENT_HEADERS_SENT:
            break;
        case HTTP_EVENT_ON_HEADER:
            break;
        case HTTP_EVENT_REDIRECT:
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI("HTTP API", "Receiving price data chunk: %d bytes", evt->data_len);
            
            if (responseBuffer == NULL) 
            {
                responseBuffer = malloc(MAX_RESPONSE_SIZE);
                if (responseBuffer == NULL) 
                {
                    ESP_LOGE("HTTP API", "Initial memory allocation failed!");
                    responseLength = 0;
                    return ESP_ERR_NO_MEM;
                }
                // Clear the buffer
                memset(responseBuffer, 0, MAX_RESPONSE_SIZE);
                responseLength = 0;  // Reset length when allocating new buffer
            }

            if (responseLength + evt->data_len > MAX_RESPONSE_SIZE) {
                ESP_LOGE("HTTP API", "Response too large, maximum size is %d bytes", MAX_RESPONSE_SIZE);
                if (responseBuffer != NULL) {
                    free(responseBuffer);
                    responseBuffer = NULL;
                }
                responseLength = 0;
                return ESP_ERR_NO_MEM;
            }

            if (responseBuffer == NULL) {
                responseBuffer = malloc(MAX_RESPONSE_SIZE);
                if (responseBuffer == NULL) {
                    ESP_LOGE("HTTP API", "Initial memory allocation failed!");
                    responseLength = 0;
                    return ESP_ERR_NO_MEM;
                }
            }

            memcpy(responseBuffer + responseLength, evt->data, evt->data_len);
            responseLength += evt->data_len;
            responseBuffer[responseLength] = '\0';
            break;
            
        case HTTP_EVENT_ON_FINISH:
            if (responseBuffer != NULL) 
            {
                ESP_LOGI("HTTP API", "Parsing network hashrate data, buffer length: %d", responseLength);
                if (responseLength > 0) {
                    ESP_LOGI("HTTP API", "Response: %s", responseBuffer);
                } else {
                    ESP_LOGW("HTTP API", "Empty response buffer");
                }
                
                cJSON *json = cJSON_Parse(responseBuffer);
                if (json == NULL) {
                    ESP_LOGE("HTTP API", "JSON parsing failed: %s", cJSON_GetErrorPtr());
                } else {
                    // Get current hashrate (in EH/s)
                    cJSON *currentHashrate = cJSON_GetObjectItem(json, "currentHashrate");
                    if (currentHashrate != NULL && cJSON_IsNumber(currentHashrate)) 
                    {
                        MEMPOOL_STATE.networkHashrate = currentHashrate->valuedouble;
                        MEMPOOL_STATE.networkHashrateValid = true;
                        ESP_LOGI("HTTP API", "Network hashrate: %.2f EH/s", MEMPOOL_STATE.networkHashrate / 1e18);
                    }

                    // Get current difficulty
                    cJSON *currentDifficulty = cJSON_GetObjectItem(json, "currentDifficulty");
                    if (currentDifficulty != NULL && cJSON_IsNumber(currentDifficulty)) 
                    {
                        MEMPOOL_STATE.networkDifficulty = currentDifficulty->valuedouble;
                        MEMPOOL_STATE.networkDifficultyValid = true;
                        ESP_LOGI("HTTP API", "Network difficulty: %.2f", MEMPOOL_STATE.networkDifficulty);
                    }
                    
                    cJSON_Delete(json);
                }
                free(responseBuffer);
                responseBuffer = NULL;
                responseLength = 0;
            }
            break;
            
        case HTTP_EVENT_DISCONNECTED:
            if (responseBuffer != NULL) {
                free(responseBuffer);
                responseBuffer = NULL;
                responseLength = 0;
            }
            break;
    }
    return ESP_OK;
}



// Update the mempool_api_price function
esp_err_t mempool_api_price(void) {
    static int64_t lastUpdate = 0;  
    int64_t currentTime = esp_timer_get_time();
    
    // Check if it's time to send the request. Limit the number of requests here
    if (((currentTime - lastUpdate)/1000000) > 300 || lastUpdate == 0)
    {
        lastUpdate = currentTime;
        // Set up the HTTP client configuration using esp cert bndles
        //Using Coingecko API key for testing
        // TODO: move this to own server for production to limit the number of requests
        esp_http_client_config_t config = {
            .url = "https://mempool.space/api/v1/prices",
            .event_handler = priceEventHandler,  // Use specific handler
            .transport_type = HTTP_TRANSPORT_OVER_SSL,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };

        // Initialize client
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) 
        {
            ESP_LOGE("HTTP API", "Failed to initialize HTTP client");
            return ESP_FAIL;
        }

        /* // Set headers with error checking Not needed for this API
        esp_err_t err;
        if ((err = esp_http_client_set_header(client, "accept", "application/json")) != ESP_OK) 
        {
            ESP_LOGE("HTTP API", "Failed to set accept header: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            return err;
        }

        if ((err = esp_http_client_set_header(client, "x-cg-demo-api-key", "CG-iUyVPGbu2nECCwVo8yXXUf57")) != ESP_OK) 
        {
            ESP_LOGE("HTTP API", "Failed to set API key header: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            return err;
        }*/

        // Perform request 
        esp_err_t err;
        err = esp_http_client_perform(client);
        
        if (err == ESP_OK) 
        {
            ESP_LOGI("HTTP API", "HTTP GET Status = %d", esp_http_client_get_status_code(client));
        } 
        else 
        {
            ESP_LOGE("HTTP API", "HTTP GET request failed: %s", esp_err_to_name(err));
        }
        
        esp_http_client_cleanup(client);
        return err;
    }
    
    return ESP_OK;
}

// Update the mempool_api_block_tip_height function
esp_err_t mempool_api_block_tip_height(void) {
    static int64_t lastUpdate = 0;  
    int64_t currentTime = esp_timer_get_time();
    
    // Check if it's time to send the request. Limit the number of requests here
    if (((currentTime - lastUpdate)/1000000) > 300 || lastUpdate == 0)
    {
        lastUpdate = currentTime;
        // Set up the HTTP client configuration using esp cert bndles
        //Using Coingecko API key for testing
        // TODO: move this to own server for production to limit the number of requests
        esp_http_client_config_t config = {
            .url = "https://mempool.space/api/blocks/tip/height",
            .event_handler = blockHeightEventHandler,  // Use specific handler
            .transport_type = HTTP_TRANSPORT_OVER_SSL,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };

        // Initialize client
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) 
        {
            ESP_LOGE("HTTP API", "Failed to initialize HTTP client");
            return ESP_FAIL;
        }

        /* // Set headers with error checking Not needed for this API
        esp_err_t err;
        if ((err = esp_http_client_set_header(client, "accept", "application/json")) != ESP_OK) 
        {
            ESP_LOGE("HTTP API", "Failed to set accept header: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            return err;
        }

        if ((err = esp_http_client_set_header(client, "x-cg-demo-api-key", "CG-iUyVPGbu2nECCwVo8yXXUf57")) != ESP_OK) 
        {
            ESP_LOGE("HTTP API", "Failed to set API key header: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            return err;
        }*/

        // Perform request 
        esp_err_t err;
        err = esp_http_client_perform(client);
        
        if (err == ESP_OK) 
        {
            ESP_LOGI("HTTP API", "HTTP GET Status = %d", esp_http_client_get_status_code(client));
        } 
        else 
        {
            ESP_LOGE("HTTP API", "HTTP GET request failed: %s", esp_err_to_name(err));
        }
        
        esp_http_client_cleanup(client);
        return err;
    }
    
    return ESP_OK;
}

esp_err_t mempool_api_network_hashrate(void)
{
    static int64_t lastUpdate = 0;  
    int64_t currentTime = esp_timer_get_time();
    
    // Check if it's time to send the request. Limit the number of requests here
    if (((currentTime - lastUpdate)/1000000) > 300 || lastUpdate == 0)
    {
        lastUpdate = currentTime;
        // Set up the HTTP client configuration using esp cert bndles
        //Using Coingecko API key for testing
        // TODO: move this to own server for production to limit the number of requests
        esp_http_client_config_t config = {
            .url = "https://mempool.space/api/v1/mining/hashrate/3d",
            .event_handler = networkHashrateEventHandler,  // Use specific handler
            .transport_type = HTTP_TRANSPORT_OVER_SSL,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };

        // Initialize client
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client == NULL) 
        {
            ESP_LOGE("HTTP API", "Failed to initialize HTTP client");
            return ESP_FAIL;
        }

        /* // Set headers with error checking Not needed for this API
        esp_err_t err;
        if ((err = esp_http_client_set_header(client, "accept", "application/json")) != ESP_OK) 
        {
            ESP_LOGE("HTTP API", "Failed to set accept header: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            return err;
        }

        if ((err = esp_http_client_set_header(client, "x-cg-demo-api-key", "CG-iUyVPGbu2nECCwVo8yXXUf57")) != ESP_OK) 
        {
            ESP_LOGE("HTTP API", "Failed to set API key header: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            return err;
        }*/

        // Perform request 
        esp_err_t err;
        err = esp_http_client_perform(client);
        
        if (err == ESP_OK) 
        {
            ESP_LOGI("HTTP API", "HTTP GET Status = %d", esp_http_client_get_status_code(client));
        } 
        else 
        {
            ESP_LOGE("HTTP API", "HTTP GET request failed: %s", esp_err_to_name(err));
        }
        
        esp_http_client_cleanup(client);
        return err;
    }
    
    return ESP_OK;
}

esp_err_t mempool_api_network_difficulty_adjustement(void) {
    // Temporary implementation
    return ESP_OK;
}

