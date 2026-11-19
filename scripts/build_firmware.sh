#!/usr/bin/env bash
# Builds the Deskimon firmware and refreshes release/firmware/*.bin  (requires ESP-IDF 5.3.1+ exported)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
command -v idf.py >/dev/null || { echo "ESP-IDF not active. Run: . ~/esp/esp-idf/export.sh  (or scripts/setup_idf.sh first)"; exit 1; }
export IDF_COMPONENT_MANAGER=0     # all components are vendored in firmware/components
cd "$ROOT/firmware/source"
[ -f sdkconfig ] || idf.py set-target esp32s3
idf.py build
REL="$ROOT/release/firmware"; mkdir -p "$REL"
cp build/bootloader/bootloader.bin build/partition_table/partition-table.bin build/deskimon.bin "$REL/"
( cd build && python -m esptool --chip esp32s3 merge_bin -o "$REL/deskimon_merged_16MB.bin" --flash_mode dio --flash_freq 80m --flash_size 16MB @flash_args )
echo "Release binaries written to $REL"
