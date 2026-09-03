#!/usr/bin/env bash
set -euo pipefail

repo_dir="$(cd "$(dirname "$0")/.." && pwd)"
handlers_file="$repo_dir/main/bap/bap_handlers.c"

system_info_body="$(sed -n '/case BAP_PARAM_SYSTEM_INFO:/,/^[[:space:]]*break;/p' "$handlers_file")"

rg -Fq 'nvs_config_get_bool(NVS_CONFIG_AUTO_FAN_SPEED)' <<<"$system_info_body"
rg -Fq 'nvs_config_get_u16(NVS_CONFIG_MANUAL_FAN_SPEED)' <<<"$system_info_body"
rg -Fq 'BAP_send_message(BAP_CMD_RES, "auto_fan", auto_fan_str)' <<<"$system_info_body"
rg -Fq 'BAP_send_message(BAP_CMD_RES, "manual_fan_speed", manual_fan_speed_str)' <<<"$system_info_body"

echo "BAP fan settings contract tests passed"
