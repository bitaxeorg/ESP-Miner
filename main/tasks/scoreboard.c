#include "scoreboard.h"
#include "nvs_config.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdbool.h>
#include <inttypes.h>

static const char * TAG = "scoreboard";

static int compare_scoreboard_entries(const void *a, const void *b)
{
    const ScoreboardEntry *ea = (const ScoreboardEntry *)a;
    const ScoreboardEntry *eb = (const ScoreboardEntry *)b;
    if (eb->difficulty > ea->difficulty) return 1;
    if (eb->difficulty < ea->difficulty) return -1;
    return 0;
}

esp_err_t scoreboard_init(Scoreboard *scoreboard)
{
    scoreboard->mutex = xSemaphoreCreateMutex();
    if (scoreboard->mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_FAIL;
    }

    memset(scoreboard->entries, 0, sizeof(scoreboard->entries));

    for (int i = 0; i < MAX_SCOREBOARD; i++) {
        scoreboard->entries[i].nvs_slot = i;

        char *entry_str = nvs_config_get_string_indexed(NVS_CONFIG_SCOREBOARD, i);
        if (entry_str == NULL || entry_str[0] == '\0') {
            free(entry_str);
            continue;
        }

        ScoreboardEntry entry;
        if (sscanf(entry_str, "%lf;%31[^;];%31[^;];%" SCNu32 ";%" SCNu32 ";%" SCNu32, 
                   &entry.difficulty, 
                   entry.job_id, 
                   entry.extranonce2, 
                   &entry.ntime, 
                   &entry.nonce, 
                   &entry.version_bits) == 6) {
            strncpy(entry.nvs_entry, entry_str, sizeof(entry.nvs_entry) - 1);
            entry.nvs_entry[sizeof(entry.nvs_entry) - 1] = '\0';
            entry.nvs_slot = i;
            scoreboard->entries[i] = entry;
        } else {
            ESP_LOGW(TAG, "Failed to parse scoreboard entry from NVS: %s", entry_str);
        }
        free(entry_str);
    }

    qsort(scoreboard->entries, MAX_SCOREBOARD, sizeof(ScoreboardEntry), compare_scoreboard_entries);

    return ESP_OK;
}

static void scoreboard_save(int i, ScoreboardEntry *entry)
{
    nvs_config_set_string_indexed(NVS_CONFIG_SCOREBOARD, i, entry->nvs_entry);
}

esp_err_t scoreboard_add(Scoreboard *scoreboard, double difficulty, const char *job_id, const char *extranonce2, uint32_t ntime, uint32_t nonce, uint32_t version_bits)
{
    if (scoreboard->mutex == NULL || difficulty <= 0.0) return ESP_OK;

    if (xSemaphoreTake(scoreboard->mutex, portMAX_DELAY) == pdTRUE) {
        if (difficulty <= scoreboard->entries[MAX_SCOREBOARD - 1].difficulty) {
            xSemaphoreGive(scoreboard->mutex);
            return ESP_OK;
        }

        ScoreboardEntry new_entry = {
            .difficulty = difficulty,
            .ntime = ntime,
            .nonce = nonce,
            .version_bits = version_bits,
            .nvs_slot = scoreboard->entries[MAX_SCOREBOARD - 1].nvs_slot,
        };
        strncpy(new_entry.job_id, job_id, sizeof(new_entry.job_id) - 1);
        new_entry.job_id[sizeof(new_entry.job_id) - 1] = '\0';
        strncpy(new_entry.extranonce2, extranonce2, sizeof(new_entry.extranonce2) - 1);
        new_entry.extranonce2[sizeof(new_entry.extranonce2) - 1] = '\0';
        snprintf(new_entry.nvs_entry, sizeof(new_entry.nvs_entry),
            "%.1f;%s;%s;%" PRIu32 ";%" PRIu32 ";%" PRIu32, 
            new_entry.difficulty, 
            new_entry.job_id, 
            new_entry.extranonce2,
            new_entry.ntime,
            new_entry.nonce,
            new_entry.version_bits);

        scoreboard->entries[MAX_SCOREBOARD - 1] = new_entry;

        qsort(scoreboard->entries, MAX_SCOREBOARD, sizeof(ScoreboardEntry), compare_scoreboard_entries);

        scoreboard_save(new_entry.nvs_slot, &new_entry);

        xSemaphoreGive(scoreboard->mutex);

        // Determine rank for log output
        int rank = 1;
        for (int i = 0; i < MAX_SCOREBOARD; i++) {
            if (scoreboard->entries[i].nvs_slot == new_entry.nvs_slot) {
                rank = i + 1;
                break;
            }
        }

        ESP_LOGI(TAG, "New #%d: Difficulty: %.1f, Job ID: %s, extranonce2: %s, ntime: %" PRIu32 ", nonce: %08" PRIX32 ", version_bits: %08" PRIX32,
            rank, new_entry.difficulty, new_entry.job_id, new_entry.extranonce2, new_entry.ntime, new_entry.nonce, new_entry.version_bits);
    } else {
        ESP_LOGE(TAG, "Failed to take mutex");
        return ESP_FAIL;
    }

    return ESP_OK;
}
