#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
project_dir="$(dirname "$script_dir")"
compiler_name="${CC:-}"
compiler_name="${compiler_name##*/}"
compiler_name="${compiler_name//[^[:alnum:]_.-]/_}"
default_build_dir="$project_dir/build/host"
if [[ -n "$compiler_name" ]]; then
    default_build_dir="${default_build_dir}-${compiler_name}"
fi
build_dir="${ESP_MINER_HOST_BUILD_DIR:-$default_build_dir}"
sanitizers="${ESP_MINER_HOST_SANITIZERS:-ON}"
filter=""

if [[ "$sanitizers" == "ON" ]]; then
    export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}"
    export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"
fi

usage() {
    printf 'Usage: %s [--all | UNITY_NAME_OR_TAG_FILTER]\n' "$0"
}

if [[ $# -gt 1 ]]; then
    usage >&2
    exit 2
fi

if [[ $# -eq 1 ]]; then
    case "$1" in
        --all)
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            filter="$1"
            ;;
    esac
fi

python3 "$project_dir/tools/test_inventory.py" --check
python3 -m unittest discover \
    -s "$project_dir/tools/tests" \
    -p 'test_inventory_checks.py'

cmake \
    -S "$project_dir/host-tests" \
    -B "$build_dir" \
    -DCMAKE_BUILD_TYPE=Debug \
    -DESP_MINER_ENABLE_SANITIZERS="$sanitizers"
cmake --build "$build_dir" --target esp_miner_host_tests --parallel

if [[ -n "$filter" ]]; then
    "$build_dir/esp_miner_host_tests" "$filter"
else
    ctest --test-dir "$build_dir" --output-on-failure
fi
