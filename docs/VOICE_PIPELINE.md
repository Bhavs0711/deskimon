# Deskimon Hands-Free Voice Pipeline

## Architecture overview

```
ESP32-S3                               Mac Agent (Swift)
─────────────────────────────          ──────────────────────────────────────────
dsk_wakeword task                      DeviceHub.dispatch()
  │  reads I2S mic frames                │
  │  double-knock detector              │
  │  fires on_wake_trigger()            │
  ▼                                     │
app_task() picks up s_wake_pending      │
  │  beep (C6, 80 ms)                   │
  │  dsk_wakeword_suspend()             │
  │  start_voice()                      │
  ▼                                     │
audio_start JSON  ──── TCP ──────────► audio_start → audio[id] = Data()
audio[seq] JSON   ──── TCP ──────────► audio[id].append(chunk)
audio_end JSON    ──── TCP ──────────► runDeviceAudio(pcm, id)
                                         │
                                         ▼
                                       DeviceSTT.transcribe(pcm16:)
                                         │  Apple Speech framework
                                         ▼
                                       assistant.handle(text, origin: .deskimon)
                                         │  → IntentExtractor → CommandRouter
                                         │  → CalendarService / ReminderService / etc.
                                         ▼
                                       sendResult(r, id:)  ──── TCP ──────────► result JSON → emotion + overlay
                                       speakResult(r)
                                         │  sendState("speaking")  ── TCP ──► speaking state
                                         │  streamTTS(r.message)   ── TCP ──► tts chunks → dsk_audio_play()
                                         │  sendState("idle")      ── TCP ──► dsk_wakeword_resume()
```

---

## Wake-word detection (Phase 1)

### Current implementation: double-knock trigger

The wake-word module lives in `firmware/components/dsk_app/dsk_wakeword.c`.

**How it works:**  
1. A dedicated FreeRTOS task (priority 3, core 0, 3 KB stack) reads 20 ms frames from the I2S microphone.  
2. It computes the RMS energy of each frame.  
3. When two energy peaks above `DSK_WW_ENERGY_THRESHOLD` are detected with a gap of `DSK_WW_MIN_GAP_MS`..`DSK_WW_MAX_GAP_MS`, the trigger fires.  
4. A cooldown of `DSK_WW_COOLDOWN_MS` prevents double-firing.  
5. During recording or TTS playback, the task backs off entirely — no interference with the main audio pipeline.

**In practice:** clap once, pause, clap again → Deskimon beeps and starts listening.

**To adjust sensitivity** (`idf.py menuconfig` → *Deskimon application* → *Wake-word detection*):

| Kconfig | Default | Effect |
|---|---|---|
| `DSK_WW_ENERGY_THRESHOLD` | 1800 | Raise in noisy rooms (try 3000–4000) |
| `DSK_WW_MIN_GAP_MS` | 300 | Minimum gap between knocks |
| `DSK_WW_MAX_GAP_MS` | 1500 | Maximum gap |
| `DSK_WW_COOLDOWN_MS` | 3000 | Minimum time between triggers |

### Why not "Hey Deskimon" yet

"Hey Deskimon" is a **custom phrase**. ESP-SR/WakeNet (Espressif's on-device neural wake-word engine) ships with fixed phrases only ("Hi ESP", "Alexa", etc.). A custom phrase requires:

**Option A — Espressif commercial model training:**  
Contact sales@espressif.com. They produce a binary `.bin` model file you embed in flash. Turn-around: 2–4 weeks.

**Option B — Self-train with ESP-SR toolkit:**  
See https://github.com/espressif/esp-sr/blob/master/docs/wake_word_engine/README.md.  
Requires ~2 hours of labelled audio (50+ utterances of "Hey Deskimon") and the Python training toolkit.

**Option C — Picovoice Porcupine Micro:**  
Free tier supports one custom wake phrase. Compile the Cortex-M/RISC-V library for ESP32-S3 Xtensa using the Picovoice SDK. See https://picovoice.ai/platform/porcupine/.

**To upgrade:** replace the body of `process_frame()` in `dsk_wakeword.c` with your model's inference call. The surrounding task, suspend/resume, cooldown, and callback infrastructure remain unchanged.

---

## Recording and transport (Phase 2)

Existing `dsk_audio_record_start()` is reused without modification.  
`voice_chunk()` sends base64-encoded PCM16 chunks over authenticated TCP with:
- Sequence numbers on every chunk for gap detection on the Mac side
- A 20 s safety timeout on the Mac to drop incomplete streams
- `audio_end` sent exactly once per recording session
- `dsk_wakeword_resume()` called when recording finishes

---

## Speech processing (Phase 3)

`DeviceHub.dispatch()` handles `audio_start`, `audio`, `audio_end` as before, plus:
- Per-stream 20 s timeout task that cleans up stale buffers and sends `idle` to the device
- Sequence gap logging for dropped TCP chunks
- Full buffer cleanup when the TCP connection drops

`DeviceSTT.transcribe(pcm16:)` uses Apple Speech Recognition (existing, unchanged).

---

## Mac automation (Phase 4)

All natural-language calendar commands go through the existing `CommandRouter` → `CalendarService` → EventKit pipeline.  
The schedule-intent parsing bug ("`schedule a meeting` routing to `listEvents`") was fixed in a previous session. Full regression tests are in `Tests/DeskimonCoreTests/CoreTests.swift`.

---

## Device response / TTS (Phase 5)

`DeviceHub.speakResult()` now:
1. Sends `state: speaking` to the device (shows Speaking emotion)
2. Streams Kokoro TTS PCM16 over TCP in 1 KB chunks
3. Sends `state: idle` after the last chunk (triggers `dsk_wakeword_resume()` on the device)

The device's `on_message()` handler suspends wake detection when the first TTS chunk arrives and resumes it 400 ms after the last chunk — preventing the speaker output from re-triggering the detector.

---

## Build commands

### Firmware

```bash
# Source IDF environment (once per terminal session)
export IDF_PATH=$HOME/esp/esp-idf-v5.3.1
export IDF_PYTHON_ENV_PATH=$HOME/.espressif/python_env/idf5.3_py3.13_env
export PATH="$IDF_PYTHON_ENV_PATH/bin:$IDF_PATH/tools:$PATH"

cd ~/Downloads/Deskimon_Final/firmware/source

# (First time only, or after sdkconfig.defaults changes)
IDF_COMPONENT_MANAGER=0 idf.py set-target esp32s3

# Build
IDF_COMPONENT_MANAGER=0 idf.py build

# Flash (replace PORT with your device, e.g. /dev/cu.usbmodem1101)
IDF_COMPONENT_MANAGER=0 idf.py -p PORT flash

# Monitor
IDF_COMPONENT_MANAGER=0 idf.py -p PORT monitor

# Flash + monitor in one step
IDF_COMPONENT_MANAGER=0 idf.py -p PORT flash monitor
```

Or flash the prebuilt binaries without the toolchain:
```bash
pip3 install esptool
cd ~/Downloads/Deskimon_Final
./scripts/flash_deskimon.sh                       # auto-detects port
./scripts/flash_deskimon.sh /dev/cu.usbmodem1101  # explicit port
./scripts/flash_deskimon.sh --erase               # wipe NVS first
```

### Mac Agent

```bash
cd ~/Downloads/Deskimon_Final

# Start Kokoro TTS server (Terminal 1 — keep running)
bash scripts/run_kokoro.sh

# Build and launch Mac Agent (Terminal 2)
bash scripts/build_mac_agent.sh
open "release/mac-agent/Deskimon Agent.app"

# Or build+launch in one step
bash scripts/run_mac_agent.sh
```

### Tests

```bash
cd ~/Downloads/Deskimon_Final/mac-agent
swift test
# Expected: 29 tests, 0 failures
```

---

## End-to-end test procedure

1. Flash the firmware. Watch serial for:
   ```
   I (xxxx) dsk_audio: audio ready (mic I2S0, speaker I2S1)
   I (xxxx) dsk_ww: wakeword task started (double-knock detector, threshold=1800)
   I (xxxx) dsk_app: Deskimon 1.0.0 started, id=dsk-xxxxxx, paired=yes
   ```

2. Start Kokoro server: `bash scripts/run_kokoro.sh`

3. Launch the Mac Agent app.

4. Confirm Wi-Fi header chips go green (device authenticated over TCP).

5. **Wake trigger test:**  
   Clap once, pause ~0.5 s, clap again.  
   Expected on serial:
   ```
   I (xxxx) dsk_ww: WAKE DETECTED (gap=490ms, rms=4231)
   I (xxxx) dsk_app: wake-word trigger -> will start voice
   ```
   Expected on device: brief beep → Listening overlay → face shows Listening.

6. **Command test:**  
   After the beep, say: *"Schedule a project review tomorrow at 4 PM"*  
   Expected: device shows Thinking → Processing → Success overlay.  
   Expected in Calendar.app: new event "Project review" tomorrow at 4:00 PM.  
   Expected on device speaker: *"Project review scheduled tomorrow at 4:00 PM."*

7. **Availability query test:**  
   Clap-clap, say: *"What meetings do I have today?"*  
   Expected: list of today's events spoken back and shown in overlay.

8. **Re-trigger test:**  
   After TTS finishes, wait 3 s, clap-clap again.  
   Expected: new listening session starts (cooldown honoured, no immediate re-fire from speaker echo).

9. **Disconnect recovery:**  
   Quit the Mac Agent while device is idle.  
   Expected: device shows Concerned → Disconnected face.  
   Relaunch the app → device reconnects and shows Happy → Neutral.

---

## Known limitations

| Item | Status |
|---|---|
| "Hey Deskimon" neural wake phrase | **Not implemented** — requires custom WakeNet model (see above). Current trigger: double-knock. |
| Wake detection while device is sleeping | Works — the first knock fires `DSK_EV_TOUCH` which wakes the face before the second knock. |
| TTS on device speaker | Implemented; requires `CONFIG_DSK_ENABLE_AUDIO=y` and hardware verification. |
| Microphone + TTS on same board | I2S channel sharing is safe (mutex via `dsk_audio_recording()` check). Hardware echo cancellation is not implemented — the 400 ms mute window is the only protection. |
| BLE voice commands | Audio streaming deliberately uses TCP only. BLE is for pairing, provisioning, and text commands. |
| Calendar EventKit | Requires Calendar permission granted in System Settings. Grant on first launch. |
| Speech Recognition | Requires Speech permission. Granted on first launch. |
| Physical hardware testing | All paths above are **code-verified and build-verified**. End-to-end behaviour requires the physical Deskimon board. |
