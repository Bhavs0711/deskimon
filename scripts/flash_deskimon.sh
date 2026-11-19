#!/usr/bin/env bash
# One-command flasher for the Waveshare ESP32-S3-Touch-LCD-1.85.
#   ./scripts/flash_deskimon.sh                 # auto-detect serial port
#   ./scripts/flash_deskimon.sh /dev/cu.usbmodem1101
#   ./scripts/flash_deskimon.sh --erase         # erase whole flash first (this UNPAIRS the device)
#   ./scripts/flash_deskimon.sh --merged        # write the single merged image at 0x0
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FW="$ROOT/release/firmware"
PORT=""; ERASE=0; MERGED=0
for a in "$@"; do
  case "$a" in
    --erase) ERASE=1 ;;
    --merged) MERGED=1 ;;
    -h|--help) sed -n '2,7p' "$0"; exit 0 ;;
    *) PORT="$a" ;;
  esac
done
if [ -z "$PORT" ]; then
  PORT="$(ls /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/ttyACM* /dev/ttyUSB* 2>/dev/null | head -n1 || true)"
fi
[ -n "$PORT" ] || { echo "No serial port found. Plug in the board (USB-C port labelled USB), or hold BOOT while connecting, then retry or pass the port explicitly."; exit 1; }
echo "Using port: $PORT"
if   command -v esptool.py >/dev/null; then ESPTOOL=(esptool.py)
elif command -v esptool    >/dev/null; then ESPTOOL=(esptool)
elif python3 -m esptool version >/dev/null 2>&1; then ESPTOOL=(python3 -m esptool)
else echo "esptool not found. Install it:  pip3 install esptool"; exit 1; fi
[ -f "$FW/deskimon.bin" ] || { echo "Missing $FW/deskimon.bin - run scripts/build_firmware.sh"; exit 1; }
if [ "$ERASE" = 1 ]; then "${ESPTOOL[@]}" --chip esp32s3 -p "$PORT" erase_flash; fi
if [ "$MERGED" = 1 ]; then
  "${ESPTOOL[@]}" --chip esp32s3 -p "$PORT" -b 460800 --before default_reset --after hard_reset \
    write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 "$FW/deskimon_merged_16MB.bin"
else
  "${ESPTOOL[@]}" --chip esp32s3 -p "$PORT" -b 460800 --before default_reset --after hard_reset \
    write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
    0x0 "$FW/bootloader.bin" 0x8000 "$FW/partition-table.bin" 0x10000 "$FW/deskimon.bin"
fi
echo "Flashed. The board resets automatically; Deskimon's face should appear within a second or two."
echo "Serial log:  python3 -m serial.tools.miniterm $PORT 115200   (Ctrl+] to quit)"
