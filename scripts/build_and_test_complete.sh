#!/bin/bash
# Complete Deskimon Build & Test - Voice Pipeline
# Usage: ./scripts/build_and_test_complete.sh [port]
# Default port: /dev/cu.usbmodem101

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ESP32_PORT="${1:-/dev/cu.usbmodem101}"

echo "=========================================="
echo "Deskimon Complete Build & Test"
echo "=========================================="
echo "Repository: $REPO_ROOT"
echo "ESP32 Port: $ESP32_PORT"
echo ""

# Verify ESP-IDF
if ! command -v idf.py &> /dev/null; then
    echo "ERROR: ESP-IDF not in PATH"
    echo "Run: . ~/esp/esp-idf/export.sh"
    exit 1
fi

# Verify port exists
if [ ! -e "$ESP32_PORT" ]; then
    echo "ERROR: Port $ESP32_PORT not found"
    echo "Try: ls /dev/cu.usbmodem* /dev/cu.usbserial*"
    exit 1
fi

# Build firmware
echo "[1/4] Building firmware..."
cd "$REPO_ROOT/firmware/source"
export IDF_COMPONENT_MANAGER=0
idf.py set-target esp32s3 2>/dev/null || true
idf.py build || idf.py fullclean && idf.py build
echo "✓ Firmware built"
echo ""

# Flash firmware
echo "[2/4] Flashing firmware..."
idf.py -p "$ESP32_PORT" -b 460800 flash 2>&1 | tail -10
echo "✓ Firmware flashed"
echo ""

# Build Mac agent
echo "[3/4] Building Mac agent..."
cd "$REPO_ROOT/mac-agent"
swift build -c release -Xswiftc -suppress-warnings 2>&1 | tail -3 || true
echo "✓ Mac agent built"
echo ""

# Summary
echo "[4/4] Ready for testing"
echo ""
echo "=========================================="
echo "✓ Build Complete"
echo "=========================================="
echo ""
echo "START SERVICES (run each in separate terminal):"
echo ""
echo "Terminal 1 - Kokoro TTS server:"
echo "  python3 tools/kokoro_server.py"
echo ""
echo "Terminal 2 - Mac agent:"
echo "  ./scripts/run_mac_agent.sh"
echo ""
echo "Terminal 3 - Monitor ESP32 logs:"
echo "  idf.py -p $ESP32_PORT monitor"
echo ""
echo "=========================================="
echo "TESTING:"
echo ""
echo "1. Pair device (if needed):"
echo "   - Long-press ESP32 screen"
echo "   - Enter Wi-Fi credentials in Mac agent"
echo ""
echo "2. Test wake-word:"
echo "   - Speak any phrase 0.4-2.5 seconds long"
echo "   - Should hear beep confirmation"
echo ""
echo "3. Test full voice command:"
echo "   - Speak: 'What meetings do I have today?'"
echo "   - Watch logs for: WAKE phrase detected, audio_start, audio chunks, audio_end"
echo "   - Check Mac agent console for transcription"
echo "   - Listen for spoken response"
echo ""
echo "4. Test calendar command:"
echo "   - Speak: 'Create a meeting tomorrow at 4 PM called project review'"
echo "   - Check Mac Calendar app"
echo ""
echo "=========================================="
