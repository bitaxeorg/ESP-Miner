#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
handlers_file="$repo_dir/main/bap/bap_handlers.c"
uart_file="$repo_dir/main/bap/bap_uart.c"
subscriptions_file="$repo_dir/main/bap/bap_subscription.c"

system_info_body="$(sed -n '/case BAP_PARAM_SYSTEM_INFO:/,/^[[:space:]]*break;/p' "$handlers_file")"

rg -Fq 'nvs_config_get_bool(NVS_CONFIG_AUTO_FAN_SPEED)' <<<"$system_info_body"
rg -Fq 'nvs_config_get_u16(NVS_CONFIG_MANUAL_FAN_SPEED)' <<<"$system_info_body"
rg -Fq 'BAP_send_message(BAP_CMD_RES, "auto_fan", auto_fan_str)' <<<"$system_info_body"
rg -Fq 'BAP_send_message(BAP_CMD_RES, "manual_fan_speed", manual_fan_speed_str)' <<<"$system_info_body"

rg -Fq 'BaseType_t task_result = xTaskCreateWithCaps(' "$uart_file"
rg -Fq 'task_result = xTaskCreate(' "$uart_file"
rg -Fq 'return ESP_ERR_NO_MEM;' "$uart_file"
rg -Fq 'ESP_LOGI(TAG, "UART receive task started");' "$uart_file"

rg -Fq 'const bap_parameter_t display_defaults[]' "$subscriptions_file"
rg -Fq 'subscriptions[param].last_subscribe = 0;' "$subscriptions_file"
rg -Fq 'subscriptions[i].last_subscribe != 0' "$subscriptions_file"
rg -Fq 'BAP_send_if_changed("auto_fan", auto_fan_str' "$subscriptions_file"
rg -Fq 'BAP_send_if_changed("manual_fan_speed", manual_fan_speed_str' "$subscriptions_file"

echo "BAP fan settings contract tests passed"
