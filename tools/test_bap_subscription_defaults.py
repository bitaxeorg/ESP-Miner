#!/usr/bin/env python3
"""Compile the production initializer; not an end-to-end serial test."""
import re
import subprocess
import tempfile
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
source = (repo / "main/bap/bap_subscription.c").read_text()
start = source.index("esp_err_t BAP_subscription_init(void)")
end = source.index("void BAP_subscription_handle_subscribe", start)
function = source[start:end]
protocol = (repo / "main/bap/bap_protocol.h").read_text()
parameters = sorted(set(re.findall(r"\bBAP_PARAM_[A-Z0-9_]+\b", protocol)) - {"BAP_PARAM_UNKNOWN"})
parameters.append("BAP_PARAM_UNKNOWN")
expected = ["HASHRATE", "TEMPERATURE", "POWER", "FAN_SPEED", "SHARES", "BEST_DIFFICULTY", "BLOCK_HEIGHT"]
checks = "\n".join(f"assert(subscriptions[BAP_PARAM_{p}].active); assert(subscriptions[BAP_PARAM_{p}].update_interval_ms == 3000);" for p in expected)
harness = """
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_LOGI(...) ((void)0)
typedef enum { PARAMETERS } bap_parameter_t;
typedef struct { bool active; uint32_t last_response, update_interval_ms, last_subscribe; } bap_subscription_t;
static bap_subscription_t subscriptions[BAP_PARAM_UNKNOWN];
static char last_values[256];
static bool last_values_valid[BAP_PARAM_UNKNOWN];
FUNCTION
static void verify(void) {
assert(!subscriptions[BAP_PARAM_WIFI].active);
CHECKS
}
int main(void) {
memset(subscriptions, 0xA5, sizeof(subscriptions));
assert(BAP_subscription_init() == ESP_OK);
verify();
subscriptions[BAP_PARAM_WIFI].active = true;
last_values_valid[BAP_PARAM_WIFI] = true;
assert(BAP_subscription_init() == ESP_OK);
verify();
assert(!last_values_valid[BAP_PARAM_WIFI]);
return 0;
}
""".replace("PARAMETERS", ",".join(parameters)).replace("FUNCTION", function).replace("CHECKS", checks)
with tempfile.TemporaryDirectory() as folder:
    src = Path(folder) / "test.c"
    binary = Path(folder) / "test"
    src.write_text(harness)
    subprocess.run(["cc", "-std=c11", "-Wall", "-Werror", str(src), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("PASS: production defaults exclude WiFi, retain telemetry, and clear stale subscriptions")
