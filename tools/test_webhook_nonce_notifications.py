#!/usr/bin/env python3
"""Exercise production nonce notifications using upstream's exact block verdict."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / "main/system.c").read_text()
start = source.index("void SYSTEM_notify_found_nonce(")
end = source.index("static esp_err_t ensure_overheat_mode_config", start)
harness = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define DIFF_STRING_SIZE 32
#define NVS_CONFIG_BEST_DIFF 0
#define ESP_LOGI(...) ((void)0)
static const char *TAG __attribute__((unused)) = "test";
typedef struct {
    unsigned block_found;
    bool show_new_block;
    uint64_t best_session_nonce_diff, best_nonce_diff;
    char best_session_diff_string[DIFF_STRING_SIZE], best_diff_string[DIFF_STRING_SIZE];
} SystemModule;
typedef struct { SystemModule SYSTEM_MODULE; uint64_t network_nonce_diff; } GlobalState;
static unsigned block_alerts, best_alerts, saves;
static void suffixString(uint64_t v, char *b, int n, int p) {
    (void)v; (void)b; (void)n; (void)p;
}
static void nvs_config_set_u64(int key, uint64_t v) { (void)key; (void)v; saves++; }
static void WEBHOOK_ALERTS_notify_block_found(double diff, double network_diff) {
    assert(diff > 0 && network_diff == 100); block_alerts++;
}
static void WEBHOOK_ALERTS_notify_best_difficulty(double diff, double network_diff) {
    assert(diff > 0 && network_diff == 100); best_alerts++;
}
'''
cases = r'''
int main(void) {
    GlobalState state = {.network_nonce_diff = 100};
    // A floating/display difficulty above the network value is not proof of a block.
    SYSTEM_notify_found_nonce(&state, 150, false);
    assert(block_alerts == 0 && best_alerts == 1 && saves == 1);
    assert(state.SYSTEM_MODULE.block_found == 0);
    // Honor the bit-exact verdict even below the displayed network difficulty.
    SYSTEM_notify_found_nonce(&state, 90, true);
    assert(block_alerts == 1 && best_alerts == 1 && saves == 1);
    assert(state.SYSTEM_MODULE.block_found == 1 && state.SYSTEM_MODULE.show_new_block);
    // A block that is also a new record produces one block alert, not a second best alert.
    SYSTEM_notify_found_nonce(&state, 200, true);
    assert(block_alerts == 2 && best_alerts == 1 && saves == 2);
    assert(state.SYSTEM_MODULE.block_found == 2);
    SYSTEM_notify_found_nonce(&state, 180, false);
    assert(block_alerts == 2 && best_alerts == 1 && saves == 2);
    SYSTEM_notify_found_nonce(&state, 250, false);
    assert(block_alerts == 2 && best_alerts == 2 && saves == 3);
    assert(state.SYSTEM_MODULE.best_session_nonce_diff == 250);
    assert(state.SYSTEM_MODULE.best_nonce_diff == 250);
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="webhook-nonce-test-") as folder:
    binary = Path(folder) / "test"
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-x", "c", "-",
                    "-o", str(binary)], input=harness + source[start:end] + cases,
                   text=True, check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: bit-exact block verdict, single block counting, and best-difficulty notifications")
