#!/bin/bash

# Binary file paths and addresses
BOOTLOADER_BIN="build/bootloader/bootloader.bin"
BOOTLOADER_BIN_ADDR=0x0
PARTITION_TABLE="build/partition_table/partition-table.bin"
PARTITION_TABLE_ADDR=0x8000
CONFIG_BIN_ADDR=0x9000
MINER_BIN="build/esp-miner.bin"
MINER_BIN_ADDR=0x10000
WWW_BIN="build/www.bin"
WWW_BIN_ADDR=0x410000
OTA_BIN="build/ota_data_initial.bin"
OTA_BIN_ADDR=0xf10000

BINS_DEFAULT=($BOOTLOADER_BIN $PARTITION_TABLE $MINER_BIN $WWW_BIN $OTA_BIN)
BINS_AND_ADDRS_DEFAULT=($BOOTLOADER_BIN_ADDR $BOOTLOADER_BIN $PARTITION_TABLE_ADDR $PARTITION_TABLE $MINER_BIN_ADDR $MINER_BIN $WWW_BIN_ADDR $WWW_BIN $OTA_BIN_ADDR $OTA_BIN)

function show_help() {
    echo "Creates combined binaries using esptool's merge_bin command for multiple config files"
    echo "Usage: $0 [OPTION]"
    echo "    Options:"
    echo "        -c: Process all config-*.bin files in the current directory"
    echo "        -h: Show this help message"
    echo
}

function print_with_error_header() {
    echo "ERROR:" $1
}

#### MAIN ####

# Check if esptool.py is installed and accessible
if ! command -v esptool.py &> /dev/null; then
    echo "esptool.py is not installed or not in PATH. Please install it first."
    echo "pip install esptool"
    exit 1
fi

OPTIND=1  # Reset in case getops has been used previously

# default values
process_configs=0

while getopts "hc" opt; do
    case "$opt" in
        h)
            show_help
            exit 0
            ;;
        c)  process_configs=1
            ;;
        *)
            show_help
            exit 1
     esac
done

shift $((OPTIND-1))

if [ "$process_configs" -eq 0 ]; then
    print_with_error_header "No option specified. Use -c to process config files."
    show_help
    exit 2
fi

# Process all config-*.bin files
for config_file in config-*.bin; do
    if [ -f "$config_file" ]; then
        # Extract the number from the config filename
        config_number=$(echo $config_file | sed 's/config-\(.*\)\.bin/\1/')
        
        # Create the output filename
        output_file="esp-miner-factory-$config_number.bin"
        
        # Prepare the bins and addresses array with the current config file
        BINS_AND_ADDRS_WITH_CONFIG=(${BINS_AND_ADDRS_DEFAULT[@]} $CONFIG_BIN_ADDR $config_file)
        
        # Call esptool.py with the specified arguments
        esptool.py --chip esp32s3 merge_bin --flash_mode dio --flash_size 16MB --flash_freq 80m "${BINS_AND_ADDRS_WITH_CONFIG[@]}" -o "$output_file"
        
        # Check if esptool.py command was successful
        if [ $? -eq 0 ]; then
            echo "Successfully created $output_file"
        else
            print_with_error_header "Failed to create $output_file"
        fi
    fi
done

echo "Processing complete."