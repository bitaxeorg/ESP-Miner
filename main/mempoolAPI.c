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

static char *responseBuffer = NULL;
static size_t responseLength = 0;

// HTTP Event Handler to handle the response from the Coingecko API
esp_err_t httpEventHandler(esp_http_client_event_t *evt) 
{
    switch (evt->event_id) 
    {
        // Error handling
        case HTTP_EVENT_ERROR:
            ESP_LOGE("HTTP API", "HTTP Event Error");
            break;
            
        // Connection established
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGD("HTTP API", "HTTP Event Connected");
            break;
            
        case HTTP_EVENT_HEADERS_SENT:
            ESP_LOGD("HTTP API", "HTTP Event Headers Sent");
            break;
            
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD("HTTP API", "HTTP Event On Header: %s: %s", 
                evt->header_key, evt->header_value);
            break;
            
        // Data received This is most important event
        case HTTP_EVENT_ON_DATA:
            // Add debug logging before processing
            ESP_LOGI("HTTP API", "Receiving data chunk of size: %d bytes", evt->data_len);
            ESP_LOGI("HTTP API", "Raw data: %.*s", evt->data_len, (char*)evt->data);
            
            // Allocate or reallocate buffer
            char *newBuffer = realloc(responseBuffer, responseLength + evt->data_len + 1);
            if (newBuffer == NULL) 
            {
                ESP_LOGE("HTTP API", "Memory allocation failed!");
                return ESP_ERR_NO_MEM;
            }
            // Copy the received data into the response buffer
            responseBuffer = newBuffer;
            memcpy(responseBuffer + responseLength, evt->data, evt->data_len);
            responseLength += evt->data_len;
            responseBuffer[responseLength] = '\0';
            
            // Log the current total response length
            ESP_LOGI("HTTP API", "Current total response length: %d bytes", responseLength);
            break;
            
        case HTTP_EVENT_ON_FINISH:
            // Log the final response length and content
            if (responseBuffer != NULL) 
            {
                ESP_LOGI("HTTP API", "Final response length: %d bytes", responseLength);
                ESP_LOGI("HTTP API", "Complete response: %s", responseBuffer);
                
                // Parse JSON response
                cJSON *json = cJSON_Parse(responseBuffer);
                if (json != NULL) 
                {
                    char *jsonStr = cJSON_Print(json);
                    ESP_LOGI("HTTP API", "Parsed JSON: %s", jsonStr);
                    free(jsonStr);
                    
                    // Extract and log the "time" field
                    cJSON *priceTimestamp = cJSON_GetObjectItem(json, "time");
                    if (priceTimestamp != NULL && cJSON_IsNumber(priceTimestamp)) 
                    {
                        ESP_LOGI("HTTP API", "Price timestamp: %d", priceTimestamp->valueint);
                    } 
                    else 
                    {
                        ESP_LOGW("HTTP API", "No 'time' field found in response");
                    }

                    cJSON *priceUSD = cJSON_GetObjectItem(json, "USD");
                    if (priceUSD != NULL && cJSON_IsNumber(priceUSD)) 
                    {
                        ESP_LOGI("HTTP API", "Price USD: %d", priceUSD->valueint);
                    } 
                    else 
                    {
                        ESP_LOGW("HTTP API", "No 'USD' field found in response");
                    }
                    cJSON_Delete(json);
                } else {
                    ESP_LOGE("HTTP API", "Failed to parse JSON response");
                }
                
                // Cleanup and free the response buffer
                free(responseBuffer);
                responseBuffer = NULL;
                responseLength = 0;
            }
            break;
            
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGD("HTTP API", "HTTP Event Disconnected");
            if (responseBuffer != NULL) 
            {
                free(responseBuffer);
                responseBuffer = NULL;
                responseLength = 0;
            }
            break;
            
        case HTTP_EVENT_REDIRECT:
            ESP_LOGD("HTTP API", "HTTP Event Redirect");
            break;
    }
    
    return ESP_OK;
}

esp_err_t mempool_api_price(void)
{
    static int64_t lastUpdate = 0;  
    int64_t currentTime = esp_timer_get_time();
    
    // Check if it's time to send the request. Limit the number of requests here
    if (((currentTime - lastUpdate)/1000000) > 60 || lastUpdate == 0)
    {
        lastUpdate = currentTime;
        // Set up the HTTP client configuration using esp cert bndles
        //Using Coingecko API key for testing
        // TODO: move this to own server for production to limit the number of requests
        esp_http_client_config_t config =
        {
            .url = "https://mempool.space/api/v1/prices",
            .event_handler = httpEventHandler,
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

