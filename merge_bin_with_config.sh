#!/bin/bash

# Check if the number of arguments is correct
if [ "$#" -ne 1 ]; then
    echo "Usage: $0 output_file"
    exit 1
fi

# Extract output_file argument
output_file="$1"

# Check if esptool.py is installed and accessible
if ! command -v esptool.py &> /dev/null; then
    echo "esptool.py is not installed or not in PATH. Please install it first."
    echo "pip install esptool"
    exit 1
fi

# Check if the required bin files exist
required_files=("config.bin" "build/bootloader/bootloader.bin" "build/partition_table/partition-table.bin" "build/esp-miner.bin" "build/www.bin" "build/ota_data_initial.bin")

for file in "${required_files[@]}"; do
    if [ ! -f "$file" ]; then
        echo "Required file $file does not exist. Make sure to build first."
		echo "Exiting"
        exit 1
    fi
done

# Call esptool.py with the specified arguments
esptool.py --chip esp32s3 merge_bin --flash_mode dio --flash_size 8MB --flash_freq 80m 0x0 build/bootloader/bootloader.bin 0x8000 build/partition_table/partition-table.bin 0x9000 config.bin 0x10000 build/esp-miner.bin 0x410000 build/www.bin 0xf10000 build/ota_data_initial.bin -o "$output_file"

# Check if esptool.py command was successful
if [ $? -eq 0 ]; then
    echo "Successfully created $output_file"
else
    echo "Failed to create $output_file"
    exit 1
fi
