#!/bin/bash

# Binary file paths and addresses
BOOTLOADER_BIN="build/bootloader/bootloader.bin"
BOOTLOADER_BIN_ADDR=0x0
PARTITION_TABLE="build/partition_table/partition-table.bin"
PARTITION_TABLE_ADDR=0x8000
CONFIG_BIN="config.bin"
CONFIG_BIN_ADDR=0x9000
MINER_BIN="build/esp-miner.bin"
MINER_BIN_ADDR=0x10000
WWW_BIN="build/www.bin"
WWW_BIN_ADDR=0x410000
OTA_BIN="build/ota_data_initial.bin"
OTA_BIN_ADDR=0xf10000

BINS_DEFAULT=($BOOTLOADER_BIN $PARTITION_TABLE $MINER_BIN $WWW_BIN $OTA_BIN)
BINS_AND_ADDRS_DEFAULT=($BOOTLOADER_BIN_ADDR $BOOTLOADER_BIN $PARTITION_TABLE_ADDR $PARTITION_TABLE $MINER_BIN_ADDR $MINER_BIN $WWW_BIN_ADDR $WWW_BIN $OTA_BIN_ADDR $OTA_BIN)

BINS_WITH_CONFIG=($BOOTLOADER_BIN $PARTITION_TABLE $CONFIG_BIN $MINER_BIN $WWW_BIN $OTA_BIN)
BINS_AND_ADDRS_WITH_CONFIG=($BOOTLOADER_BIN_ADDR $BOOTLOADER_BIN $PARTITION_TABLE_ADDR $PARTITION_TABLE $CONFIG_BIN_ADDR $CONFIG_BIN $MINER_BIN_ADDR $MINER_BIN $WWW_BIN_ADDR $WWW_BIN $OTA_BIN_ADDR $OTA_BIN)

BINS_UPDATE=($MINER_BIN $WWW_BIN $OTA_BIN)
BINS_AND_ADDRS_UPDATE=($MINER_BIN_ADDR $MINER_BIN $WWW_BIN_ADDR $WWW_BIN $OTA_BIN_ADDR $OTA_BIN)


function show_help() {
    echo "Creates a combined binary using esptool's merge_bin command"
    echo "Usage: $0 [OPTION] output_file"
    echo "    output_file: The combined binary"
    echo "    Options:"
    echo "        -c: Include $CONFIG_BIN in addition to default binaries into output_file"
    echo "        -u: Only include the following binaries into output_file:"
    echo "            ${BINS_UPDATE[@]}"
    echo "    If no options are specified, by default the following binaries are included:"
    echo "        ${BINS_DEFAULT[@]}"
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
output_file=""
update_only=0
with_config=0

while getopts "huc" opt; do
    case "$opt" in
        h)
            show_help
            exit 0
            ;;
        c)  with_config=1
            ;;
        u)  update_only=1
            ;;
        *)
            show_help
            exit 1
     esac
done

shift $((OPTIND-1))

[ "${1:-}" = "--" ] && shift

# Extract output_file argument
output_file="$1"

if [ -z "$output_file" ]; then
    print_with_error_header "output_file missing"
    echo
    show_help
    exit 2
fi

if [ "$update_only" -eq 1 ] && [ "$with_config" -eq 1 ]; then
    print_with_error_header "Include only one of '-c' and '-u'"
    exit 3
fi

selected_bins=()
selected_bins_and_addrs=()
esptool_leading_args="--chip esp32s3 merge_bin --flash_mode dio --flash_size 16MB --flash_freq 80m"

if [ "$update_only" -eq 1 ]; then
    selected_bins+=(${BINS_UPDATE[@]})
    selected_bins_and_addrs+=(${BINS_AND_ADDRS_UPDATE[@]})
    esptool_leading_args+=" --format hex"
else
    if [ "$with_config" -eq 1 ]; then
        selected_bins+=(${BINS_WITH_CONFIG[@]})
        selected_bins_and_addrs+=(${BINS_AND_ADDRS_WITH_CONFIG[@]})
    else
        selected_bins+=(${BINS_DEFAULT[@]})
        selected_bins_and_addrs+=(${BINS_AND_ADDRS_DEFAULT[@]})
    fi
fi

for file in "${selected_bins[@]}"; do
    if [ ! -f "$file" ]; then
        print_with_error_header "Required file $file does not exist. Make sure to build first."
        echo "Exiting"
        exit 4
    fi
done

# Call esptool.py with the specified arguments
esptool.py $esptool_leading_args "${selected_bins_and_addrs[@]}" -o "$output_file"

# Check if esptool.py command was successful
if [ $? -eq 0 ]; then
    echo "Successfully created $output_file"
else
    print_with_error_header "Failed to create $output_file"
    exit 5
fi
