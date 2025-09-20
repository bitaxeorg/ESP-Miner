#include "scoreboard.h"
#include "nvs_config.h"
#include "esp_log.h"
#include <stdio.h>

static const char * TAG = "scoreboard";

void scoreboard_init(Scoreboard *scoreboard) {
    scoreboard->count = 0;
    scoreboard->mutex = xSemaphoreCreateMutex();
    if (scoreboard->mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
    }

    for (int i = 0; i < MAX_SCOREBOARD; i++) {
        char key[32];
        snprintf(key, sizeof(key), NVS_CONFIG_SCOREBOARD_PREFIX "%02d", i + 1);
        char *entry_str = nvs_config_get_string(key, "");
        if (entry_str[0] == '\0') {
            free(entry_str);
            break;
        }

        ScoreboardEntry entry;
        if (sscanf(entry_str, "%lf;%31[^;];%31[^;];%lu;%lu;%lu", 
                   &entry.difficulty, 
                   entry.job_id, 
                   entry.extranonce2, 
                   &entry.ntime, 
                   &entry.nonce, 
                   &entry.version_bits) == 6) {
            strncpy(entry.nvs_entry, entry_str, sizeof(entry.nvs_entry) - 1);
            entry.nvs_entry[sizeof(entry.nvs_entry) - 1] = '\0';
            scoreboard->entries[scoreboard->count++] = entry;
        } else {
            ESP_LOGW(TAG, "Failed to parse scoreboard entry from NVS: %s", entry_str);
        }
        free(entry_str);
    }
}

static void scoreboard_save(int i, ScoreboardEntry *entry) {
    char key[32];
    snprintf(key, sizeof(key), NVS_CONFIG_SCOREBOARD_PREFIX "%02d", i + 1);
    nvs_config_set_string(key, entry->nvs_entry);
}

void scoreboard_add(Scoreboard *scoreboard, double difficulty, const char *job_id, const char *extranonce2, uint32_t ntime, uint32_t nonce, uint32_t version_bits)
{
    int i = (scoreboard->count < MAX_SCOREBOARD) ? scoreboard->count : MAX_SCOREBOARD - 1;

    if (scoreboard->count == MAX_SCOREBOARD && i >= 0 && difficulty <= scoreboard->entries[i].difficulty) {
        return;
    }

    ScoreboardEntry new_entry = {
        .difficulty = difficulty,
        .ntime = ntime,
        .nonce = nonce,
        .version_bits = version_bits,
    };
    strncpy(new_entry.job_id, job_id, sizeof(new_entry.job_id) - 1);
    new_entry.job_id[sizeof(new_entry.job_id) - 1] = '\0';
    strncpy(new_entry.extranonce2, extranonce2, sizeof(new_entry.extranonce2) - 1);
    new_entry.extranonce2[sizeof(new_entry.extranonce2) - 1] = '\0';
    snprintf(new_entry.nvs_entry, sizeof(new_entry.nvs_entry),
        "%.1f;%s;%s;%lu;%lu;%lu", 
        new_entry.difficulty, 
        new_entry.job_id, 
        new_entry.extranonce2,
        new_entry.ntime,
        new_entry.nonce,
        new_entry.version_bits);

    if (xSemaphoreTake(scoreboard->mutex, portMAX_DELAY) == pdTRUE) {
        while (i > 0 && difficulty > scoreboard->entries[i - 1].difficulty) {
            scoreboard->entries[i] = scoreboard->entries[i - 1];
            scoreboard_save(i, &scoreboard->entries[i]);
            i--;
        }
    
        scoreboard->entries[i] = new_entry;
        scoreboard_save(i, &new_entry);
        if (scoreboard->count < MAX_SCOREBOARD) {
            scoreboard->count++;
        }
        xSemaphoreGive(scoreboard->mutex);
    } else {
        ESP_LOGE(TAG, "Failed to take mutex");
        return;
    }
    
    ESP_LOGI(TAG, "New #%d: Difficulty: %.1f, Job ID: %s, extranonce2: %s, ntime: %d, nonce: %08X, version_bits: %08X",
        i+1, new_entry.difficulty, new_entry.job_id, new_entry.extranonce2, new_entry.ntime, (unsigned int)new_entry.nonce, (unsigned int)new_entry.version_bits);
}
