#!/usr/bin/env bash
# PlatformIO's ESP-IDF build produces .pio/build/esp32dev/flasher_args.json,
# but its flash_files paths (bootloader/bootloader.bin, dgt.bin,
# partition_table/partition-table.bin) are stale — they don't match the flat
# build output (bootloader.bin, firmware.bin, partitions.bin). wokwi-cli's
# native ESP-IDF upload path (triggered when `firmware` in wokwi.toml points
# at a file literally named flasher_args.json) resolves flash_files paths
# relative to this file's own directory, so we regenerate a corrected copy
# here with the right flat filenames. Run this after every
# `pio run -e wokwi` and before `wokwi-cli`.
set -euo pipefail
cd "$(dirname "$0")/.pio/build/wokwi"

flash_size=$(grep -oP '(?<="flash_size": ")[^"]+' flasher_args.json)

cat > flasher_args.json <<EOF
{
    "write_flash_args" : [ "--flash_mode", "dio",
                           "--flash_size", "$flash_size",
                           "--flash_freq", "40m" ],
    "flash_settings" : {
        "flash_mode": "dio",
        "flash_size": "$flash_size",
        "flash_freq": "40m"
    },
    "flash_files" : {
        "0x1000" : "bootloader.bin",
        "0x10000" : "firmware.bin",
        "0x8000" : "partitions.bin"
    },
    "bootloader" : { "offset" : "0x1000", "file" : "bootloader.bin", "encrypted" : "false" },
    "app" : { "offset" : "0x10000", "file" : "firmware.bin", "encrypted" : "false" },
    "partition-table" : { "offset" : "0x8000", "file" : "partitions.bin", "encrypted" : "false" },
    "extra_esptool_args" : {
        "after"  : "hard_reset",
        "before" : "default_reset",
        "stub"   : true,
        "chip"   : "esp32"
    }
}
EOF
