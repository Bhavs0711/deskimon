# Deskimon Voice Pipeline - Complete Testing Guide

This document walks through testing the complete end-to-end voice pipeline: microphone → ESP32 → Mac → speaker.

## Prerequisites

1. **Hardware**: Waveshare ESP32-S3-LCD-1.85 with:
   - Firmware built and flashed (see BUILD section below)
   - Connected via USB to your Mac (`/dev/cu.usbmodem*` or similar)
   - On the same 2.4 GHz Wi-Fi network as your Mac

2. **Software**:
   - ESP-IDF 5.3.1+ (run: `. ~/esp/esp-idf/export.sh`)
   - Swift 5.9+ (Xcode 15+)
   - Python 3.9+ (for Kokoro TTS server)
   - Mac Agent built (see BUILD section)

3. **Pairing**: Device must be paired with Mac and authenticated over TCP/BLE.

## BUILD

### 1. Build and Flash Firmware

```bash
cd firmware/source
. ~/esp/esp-idf/export.sh
export IDF_COMPONENT_MANAGER=0
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/cu.usbmodem101 flash monitor
```

**Verify in logs:**
- `[dsk_audio] audio ready (mic I2S0, speaker I2S1)`
- `[dsk_wakeword] wakeword task started (speech-energy detector, ...)`
- `[dsk_board] display ready`

If you see `[dsk_audio] audio unavailable`, the I2S initialization failed. Check GPIO wiring.

### 2. Build Mac Agent

```bash
cd mac-agent
swift build -c release
cd ..
```

### 3. Start Services (3 terminals)

**Terminal 1: Kokoro TTS Server**
```bash
python3 tools/kokoro_server.py
```
Expected: `* Running on http://127.0.0.1:8000`

**Terminal 2: Mac Agent**
```bash
./scripts/run_mac_agent.sh
```
Or manually:
```bash
open release/mac-agent/"Deskimon Agent.app"
```

**Terminal 3: ESP32 Monitor (optional but recommended)**
```bash
idf.py -p /dev/cu.usbmodem101 monitor
```

## PAIRING (one-time)

1. **Enter pairing mode on device:**
   - Long-press the ESP32 screen for 3 seconds
   - Face should show blue orbiting eyes
   - Serial log: `[dsk_ble] pairing mode enabled`

2. **Pair in Mac agent:**
   - Click "Pair / Connect" button
   - Enter 2.4 GHz Wi-Fi SSID and password (same network as Mac)
   - Click "Pair"

3. **Verify pairing:**
   - Device shows "Paired, Connected to your Mac"
   - Header lights turn green (Wi-Fi + authentication)
   - Mac agent shows: `Paired and connected (wifi)`
   - Serial log: `TCP connected to Mac Agent` → `authenticated on TCP`

## TESTING STAGES

### Stage 1: Wake-Word Detection (Local Device, No Mac)

**Goal:** Verify the ESP32 microphone is capturing audio and wake-word detection fires.

**Test:**
1. Keep device powered on, Mac agent NOT running
2. Speak near the device: any phrase 0.4–2.5 seconds long (e.g., "Hello", "Hey there", a hand clap)
3. Listen for a beep (confirmation tone)

**Logs to watch** (run `idf.py monitor`):
```
[dsk_ww] [MIC DIAG] rms=542.3 peak=15678 samples=16000 change=8.5% => LOUD (speech detected)
[dsk_ww] speech start rms=542.3
[dsk_ww] WAKE phrase detected (voiced=850ms rms=542.3)
[dsk_app] wake-word trigger -> will start voice
```

**If you don't see the wake log:**
- Check microphone wiring (GPIO 2, 15, 39)
- Check I2S initialization: `[dsk_audio] audio ready`
- Adjust wake-word thresholds: `idf.py menuconfig` → Deskimon → Wake-word → Energy threshold (default 1800)
- Speak louder and longer (400–2500 ms)

**If you see the wake log but NO beep:**
- Check speaker wiring (GPIO 47, 38, 48)
- Verify speaker is powered (may need external power or enable on TCA9554 expander)
- Try manual beep test: uncomment `dsk_audio_beep()` in `dsk_app.c` initialization

### Stage 2: Wake-Word + TCP Connection (Device → Mac TCP)

**Goal:** Verify wake-word fires, device connects to Mac, and TCP is authenticated.

**Test:**
1. Mac agent running and paired
2. Speak on device: "Hello Deskimon"
3. Watch serial logs
4. Check Mac agent console

**Logs on ESP32** (should see in order):
```
[dsk_ww] WAKE phrase detected (voiced=450ms rms=600.0)
[dsk_app] wake-word trigger -> will start voice
[dsk_app] audio_start id=1 sent=1 (tcp_up=1 authed=1)
[dsk_app] Listening...
[dsk_app] audio chunk seq=0 samples=512 bytes_b64=something sent
[dsk_app] audio chunk seq=1 samples=512 bytes_b64=... sent
...
[dsk_app] audio_end id=1 sent=1 total_seq=X
```

**If you see `audio_start sent=0`:**
- TCP not connected: `tcp_up=0` → check network, firewall, Mac agent running
- TCP not authenticated: `authed=0` → device token mismatch, re-pair
- TCP send failed: log will show error code

**Logs on Mac agent console:**
```
audio stream 1 initialized
audio id=1: seq=0 (512 bytes)
audio id=1: seq=1 (512 bytes)
...
audio stream 1 complete (X bytes)
```

If you don't see these, the TCP connection is down. Check:
- Network: ping Mac from device terminal on the same Wi-Fi
- Firewall: System Settings → Security & Privacy → Firewall (disable or add Deskimon Agent.app)
- Port: Mac agent should be listening on :8765 (check logs: `TCP listener ready on port 8765`)

### Stage 3: Audio Capture + Speech-to-Text (STT)

**Goal:** Verify audio reaches Mac and is transcribed by Apple Speech framework.

**Test:**
1. Speak clearly: "What's on my calendar today?"
2. Check Mac agent console for transcription

**Logs on Mac agent:**
```
[DeviceHub] audio stream 1 complete (12288 bytes)
heard: "What's on my calendar today?"
```

**If transcription is empty:**
- Audio may be corrupted: check audio_end id and total bytes in device logs
- Microphone sample rate mismatch: device sends 16kHz, Mac expects 16kHz
- Audio too short: STT requires ≥100ms (1600 samples at 16kHz)
- Audio too quiet: raise microphone volume or lower CONFIG_DSK_WW_ENERGY_THRESHOLD

**If you see timeout instead of transcription:**
- Speech was too long (max ~15 seconds)
- Silence before end of phrase was too short (see Stage 1 for pause duration)
- Kokoro TTS might be interfering: check if speaker is playing

### Stage 4: Command Routing + Execution

**Goal:** Verify intent extraction, routing, and Mac-side action.

**Test:**
1. Speak: "Create a meeting tomorrow at 3 PM called engineering sync"
2. Check Mac Calendar app for new event
3. Listen for confirmation from device speaker

**Logs on Mac agent:**
```
heard: "Create a meeting tomorrow at 3 PM called engineering sync"
[Assistant] intent=createEvent usedLLM=false note="...details..."
[CommandRouter] executing createEvent
[EventKit] creating event: "engineering sync" tomorrow 3 PM ...
```

**Result on device speaker:**
- If successful: "Event added to your calendar: engineering sync tomorrow at 3 PM"
- If failed: "Sorry, something went wrong"

**If no event appears in Calendar:**
- Permission denied: System Settings → Privacy & Security → Calendars → Deskimon Agent (enable)
- Time parsing failed: try "Create a calendar event tomorrow at 3 PM" or be explicit: "July 15 at 3 PM"
- Intent extraction failed: LLM not available or rules parser rejected command

**If speaker doesn't respond:**
- Kokoro TTS server not running: `python3 tools/kokoro_server.py`
- Speaker settings in Mac agent: check "Speak on Device" toggle
- Device speaker not working: test with Stage 1 beep

### Stage 5: Complete Voice Loop (Microphone → Command → Speaker)

**Goal:** Verify the entire pipeline end-to-end.

**Test Script:**
```
1. Say: "Hello Deskimon"
   → Listen for beep (wake confirmation)
   
2. Say: "What time is my next meeting?"
   → Deskimon replies aloud with next meeting time/title
   
3. Say: "Send me an email to Alice about the project status"
   → Deskimon confirms email drafted and ready to send
   → Check Mac Mail for draft
   
4. Say: "Yes send it"
   → Deskimon confirms email sent
   → Check Mail sent folder
```

**If any step fails, trace back through Stages 1–4 above.**

## DIAGNOSTICS

### Enable Microphone Diagnostic Logging

Edit `firmware/source/sdkconfig.defaults`:
```
CONFIG_DSK_MIC_DIAG=y
```

Rebuild and flash. Every 1 second in the logs:
```
[dsk_ww] [MIC DIAG] rms=542.1 peak=8765 samples=16000 change=8.3% => LOUD (speech detected)
```

Verdict meanings:
- `NO SAMPLES` → I2S clock not running (check SCK GPIO15)
- `FLAT (check I2S wiring)` → Data line frozen or not connected
- `FROZEN (check clock lines)` → WS or SCK not toggling
- `silent (mic working, room quiet)` → Healthy, room is quiet
- `ambient noise (mic working)` → Healthy, background noise present
- `LOUD (speech detected)` → Speech detected, ready to wake

### Manual Audio Capture Test

In `firmware/source/main/dsk_speaker_test.c`, there's a manual speaker test. Uncomment and rebuild to test speaker output directly.

### TCP Connection Debug

On Mac, check TCP listener is running:
```bash
lsof -i :8765
```

Should show a process listening. If not, Mac agent not running or port blocked.

On ESP32, force reconnection:
```c
dsk_net_force_reconnect();
```

Watch logs for discovery and connection attempts.

### BLE as Fallback

If TCP fails but BLE works, commands will route over BLE instead. The Device → Mac path is identical, just transported over BLE GATT characteristics. If you see commands executing but "TCP not up", BLE is likely handling it.

## EXPECTED END-TO-END FLOW

```
You speak: "Create an event tomorrow at 4 PM"
    ↓
ESP32 microphone (I2S0) captures audio at 16 kHz
    ↓
Wake-word detector fires after ~500ms of speech
    ↓
Device beeps (confirmation) and shows "Listening..." overlay
    ↓
Device sends: audio_start message (TCP)
    ↓
Device streams audio chunks (audio messages, 500+ samples each)
    ↓
Device sends: audio_end message
    ↓
Mac receives all audio, combines chunks
    ↓
Mac transcribes with Apple Speech framework
    ↓
Mac extracts intent: "createEvent", date="tomorrow", time="4 PM", title="event"
    ↓
Mac creates calendar event via EventKit
    ↓
Mac sends result back to device: success, message "Event added...", emotion "success"
    ↓
Device displays emotion, shows overlay "Event added"
    ↓
Mac generates TTS via Kokoro: "Event added to your calendar"
    ↓
Mac streams TTS PCM to device (tts messages)
    ↓
Device plays audio on speaker (I2S1 → PCM5101 DAC)
    ↓
You hear: "Event added to your calendar tomorrow at 4 PM"
    ↓
Calendar event appears on your Mac
```

**Total time:** ~2 seconds (speech capture) + 1–3 seconds (transcription + command execution) + 2–3 seconds (TTS playback) ≈ 5–8 seconds end-to-end.

## TROUBLESHOOTING SUMMARY

| Symptom | Check |
|---------|-------|
| No beep on wake-word | Speaker wiring, speaker power, I2S1 initialization |
| Wake-word doesn't fire | Microphone wiring, I2S0 initialization, MIC_DIAG logs, speech duration/volume |
| Audio chunks not sent | TCP not up, TCP not authenticated, TCP send failures in logs |
| Mac doesn't receive audio | TCP connection down, firewall blocking, Mac agent not running |
| No transcription | Audio too short/quiet, STT timeout, Apple Speech permission |
| Command not executed | Intent extraction failed, missing EventKit permission, command syntax wrong |
| Speaker doesn't respond | Kokoro server not running, "Speak on Device" toggle off, speaker audio not streaming |

## Additional Commands to Try

Once the pipeline is working, test these voice commands:

**Calendar:**
- "What meetings do I have today?"
- "When is my next meeting?"
- "Create a meeting tomorrow at 2 PM called standup"
- "Schedule an event next Monday at 10 AM for one hour"

**Reminders & Alarms:**
- "Remind me to buy groceries tomorrow"
- "Set an alarm for 7 AM"

**Apps & Files:**
- "Open Safari"
- "Open Finder"

**Email:**
- "Draft an email to alice@company.com saying hello"
- "Send that email"

**General:**
- "Say hello"
- "Tell me the time"

---

**If you get stuck, check the logs in this order:**
1. ESP32 serial monitor: `idf.py monitor` (audio capture, TCP, wake-word)
2. Mac agent console: (TCP auth, audio receipt, transcription, commands)
3. Mac Kokoro server console: (TTS generation)
4. System Events / Mail / Calendar apps: verify side effects

