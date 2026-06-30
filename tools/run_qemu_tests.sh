#!/usr/bin/env bash

set -e

# Resolve script directory to execute relative paths correctly
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"

echo "=== ESP-Miner QEMU Test Runner ==="

# Check if ESP-IDF environment is already sourced
if ! command -v idf.py &> /dev/null; then
    echo "ESP-IDF environment not detected in PATH."
    # Try common local installation paths
    IDF_EXPORT_PATHS=(
        "$HOME/esp/v5.5.1/esp-idf/export.sh"
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
        echo "Please source it manually (e.g. '. ~/esp/v5.5.1/esp-idf/export.sh') before running this script."
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
idf.py build

echo "Merging binaries..."
cd build
esptool.py --chip esp32s3 merge_bin --fill-flash-size 16MB -o flash_image.bin @flash_args

echo "Running tests in QEMU emulator..."
qemu-system-xtensa -machine esp32s3 -monitor none -nographic -no-reboot -watchdog-action shutdown -drive file=flash_image.bin,if=mtd,format=raw -m 4 -serial stdio
