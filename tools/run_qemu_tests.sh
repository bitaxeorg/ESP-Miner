#!/usr/bin/env bash

set -euo pipefail

# Resolve script directory to execute relative paths correctly
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

usage() {
    printf 'Usage: %s [--ubsan]\n' "$0"
}

UBSAN=OFF
BUILD_VARIANT=normal
if [ "$#" -gt 1 ]; then
    usage >&2
    exit 2
fi
if [ "$#" -eq 1 ]; then
    case "$1" in
        --ubsan) UBSAN=ON; BUILD_VARIANT=ubsan ;;
        --help|-h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
fi

BUILD_DIR="${ESP_QEMU_BUILD_DIR:-$ROOT_DIR/.cache/qemu-test-build}"
if [ "$UBSAN" = ON ]; then
    BUILD_DIR="${ESP_QEMU_BUILD_DIR:-$ROOT_DIR/.cache/qemu-ubsan-test-build}"
fi

echo "=== ESP-Miner QEMU Test Runner (UBSan: $UBSAN) ==="

# Check if ESP-IDF environment is already sourced
if ! command -v idf.py &> /dev/null; then
    echo "ESP-IDF environment not detected in PATH."
    # Try common local installation paths
    IDF_EXPORT_PATHS=(
        "$HOME/esp/v6.0.2/esp-idf/export.sh"
        "$HOME/esp/v5.5/esp-idf/export.sh"
        "$HOME/esp/esp-idf/export.sh"
    )
    
    SOURCED=false
    for export_path in "${IDF_EXPORT_PATHS[@]}"; do
        if [ -f "$export_path" ]; then
            echo "Found ESP-IDF export script at: $export_path"
            echo "Sourcing ESP-IDF environment..."
            . "$export_path"
            SOURCED=true
            break
        fi
    done
    
    if [ "$SOURCED" = false ]; then
        echo "ERROR: Could not locate ESP-IDF export script."
        echo "Please source it manually (e.g. '. ~/esp/v6.0.2/esp-idf/export.sh') before running this script."
        exit 1
    fi
fi

# Ensure qemu-system-xtensa is installed
if ! command -v qemu-system-xtensa &> /dev/null; then
    echo "ERROR: qemu-system-xtensa is not installed or not in PATH."
    exit 1
fi

echo "Building test-ci project..."
cd "$ROOT_DIR/test-ci"
CCACHE_DIR="${CCACHE_DIR:-$ROOT_DIR/.cache/qemu-test-ccache}" \
    IDF_TARGET=esp32s3 \
    idf.py -B "$BUILD_DIR" \
        -D "SDKCONFIG=$BUILD_DIR/sdkconfig-$BUILD_VARIANT" \
        -D "ENABLE_UBSAN=$UBSAN" build

echo "Merging binaries..."
cd "$BUILD_DIR"
esptool --chip esp32s3 merge-bin --pad-to-size 16MB -o flash_image.bin @flash_args

echo "Running tests in QEMU emulator..."
output_log="$PWD/output.log"
rm -f "$output_log"
qemu_status=0
timeout "${QEMU_TIMEOUT:-5m}" qemu-system-xtensa \
    -machine esp32s3 \
    -monitor none \
    -nographic \
    -no-reboot \
    -watchdog-action shutdown \
    -drive file=flash_image.bin,if=mtd,format=raw \
    -m 4 \
    -serial "file:$output_log" || qemu_status=$?

if [ -f "$output_log" ]; then
    cat "$output_log"
fi

if [ "$qemu_status" -ne 0 ]; then
    echo "ERROR: QEMU exited with status $qemu_status." >&2
    exit "$qemu_status"
fi
if [ ! -s "$output_log" ]; then
    echo "ERROR: QEMU did not produce serial output." >&2
    exit 1
fi
if grep -q 'Undefined behavior of type ' "$output_log"; then
    echo "ERROR: UBSan detected undefined behavior." >&2
    exit 1
fi

summary="$(tr -d '\r' < "$output_log" | grep -E '[[:digit:]]+ Tests [[:digit:]]+ Failures [[:digit:]]+ Ignored' | tail -n 1 || true)"
if [ -z "$summary" ]; then
    echo "ERROR: QEMU output did not contain a Unity test summary." >&2
    exit 1
fi

failures="$(printf '%s\n' "$summary" | sed -E 's/.* Tests ([0-9]+) Failures.*/\1/')"
printf '\nQEMU summary: %s\n' "$summary"
if [ "$failures" -ne 0 ]; then
    exit 1
fi
