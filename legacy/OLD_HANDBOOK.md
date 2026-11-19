# Project Handbook

This document is the main collaborator guide for the Bhavya Desk Agent project.

It is written for teammates who need to:

- understand what the project is
- build and run it locally
- know where code lives
- debug failures quickly
- make changes without guessing which layer owns what

## 1. Project Summary

Bhavya Desk Agent is a two-part system:

1. A **TuyaOpen firmware app** running on a T5-E1 board with a round AMOLED display, BLE, IMU input, and audio support.
2. A **native macOS desktop app** that uses Groq for speech-to-text and LLM reasoning, then drives the board as an expressive body.

The board renders animated blue eyes and responds to BLE commands.
The Mac app handles the higher-level conversational loop and the desktop user experience.

## 2. Product Goals

The current product direction is:

- make the board feel like a tiny embodied desk companion
- keep the face cute, readable, and expressive
- keep the board logic simple and robust
- use the Mac for heavier AI, better microphone capture, and desktop UX
- preserve easy low-level debugging through simple BLE text commands

## 3. System Architecture

### 3.1 Runtime Components

```text
macOS App
  -> records audio from the Mac microphone
  -> sends audio to Groq STT
  -> sends conversation + instructions to Groq chat
  -> gets a short spoken reply + emotion plan
  -> speaks via Mac speakers or the T5-E1 speaker
  -> sends emotion commands to the board over BLE

T5-E1 Firmware
  -> advertises as a BLE peripheral
  -> parses plain-text commands
  -> renders eyes in LVGL
  -> applies motion input from the QMI8658 IMU
  -> plays short beep or streamed mu-law speech via the onboard speaker
  -> optionally records short mu-law audio from the onboard mic for Python tools
```

### 3.2 Main Interaction Modes

There are three primary ways to interact with the project:

- **Desktop product mode**
  - Use the SwiftUI macOS app in `mac-agent/`
  - This is the main user-facing product path

- **Low-level BLE control mode**
  - Use `tools/mac_ble_agent.py`
  - Best for debugging board state and manual emotion testing

- **Direct board voice mode**
  - Use `tools/groq_voice_companion.py`
  - Best for testing the board mic and board speaker directly without the desktop app

## 4. Ownership By Layer

### 4.1 Firmware Responsibilities

The firmware owns:

- eye rendering
- emotion state transitions
- BLE peripheral behavior
- IMU polling and motion-to-gaze mapping
- board speaker playback
- board microphone capture

The firmware does **not** own:

- Groq API calls
- desktop UI
- Mac microphone capture
- conversation history orchestration across turns

### 4.2 macOS App Responsibilities

The macOS app owns:

- main product UX
- Mac microphone capture
- Groq STT
- Groq chat planning
- mapping reply plans into board emotions
- conversation state and live loop
- choosing whether replies play from Mac speakers or the T5-E1 speaker

### 4.3 Python Tool Responsibilities

The Python tools exist for:

- debugging BLE outside the app
- directly testing board audio features
- validating the board’s raw protocol independently of the Swift app

## 5. Repository Tour

This repository is intentionally small. Below is the role of every tracked source file.

### 5.1 Top-Level Files

- `README.md`
  - Front door for the project

- `CMakeLists.txt`
  - Registers the firmware sources and include path with TuyaOpen

- `app_default.config`
  - Default TuyaOpen project config used for the board target

- `requirements.txt`
  - Python dependencies used by the tooling

### 5.2 Firmware Source Files

- `src/tuya_main.c`
  - Firmware startup
  - Initializes logging, KV, timers, hardware, LVGL, voice bridge, IMU thread, and BLE

- `src/eye_scene.c`
  - Main eye renderer
  - Emotion definitions
  - Natural blink and gaze behavior
  - Command parser for eye-related BLE commands

- `src/ble_agent.c`
  - Tuya BLE peripheral wrapper
  - Advertising data, scan response name, command receipt, and reply sending

- `src/motion_qmi8658.c`
  - QMI8658 IMU detection, configuration, polling thread, and motion forwarding

- `src/voice_bridge.c`
  - Board audio bridge
  - Speaker playback
  - Beep test
  - Board microphone capture with mu-law encoding
  - Voice-related command parsing

- `src/audio_test.c`
  - Thin wrapper for the speaker beep test

### 5.3 Firmware Headers

- `include/eye_scene.h`
  - Public eye renderer API and emotion enum

- `include/ble_agent.h`
  - BLE agent init declaration

- `include/motion_qmi8658.h`
  - IMU startup declaration

- `include/voice_bridge.h`
  - Voice bridge API and command hook

- `include/audio_test.h`
  - Speaker test declaration

### 5.4 Board Config

- `config/WAVESHARE_T5AI_TOUCH_AMOLED_1_75.config`
  - Board-target config for the Waveshare T5AI AMOLED board

### 5.5 macOS App Files

- `mac-agent/Package.swift`
  - Swift Package definition for the native app

- `mac-agent/Resources/Info.plist`
  - App metadata and permission usage strings

- `mac-agent/Sources/BhavyaDeskAgent/BhavyaDeskAgentApp.swift`
  - App entry point

- `mac-agent/Sources/BhavyaDeskAgent/AppModel.swift`
  - Main application state and orchestration layer
  - Live loop
  - Groq calls
  - Emotion planning
  - Board coordination

- `mac-agent/Sources/BhavyaDeskAgent/Models/AssistantModels.swift`
  - Shared models and enums for phases, routes, emotions, and errors

- `mac-agent/Sources/BhavyaDeskAgent/Services/MicrophoneRecorder.swift`
  - Mac microphone capture
  - Recording profiles
  - Auto-stop on silence

- `mac-agent/Sources/BhavyaDeskAgent/Services/GroqClient.swift`
  - Groq HTTP client for STT and chat completions

- `mac-agent/Sources/BhavyaDeskAgent/Services/MacSpeechPlayer.swift`
  - Native macOS speech generation
  - Mac audio playback
  - Audio conversion for board-speaker routing

- `mac-agent/Sources/BhavyaDeskAgent/Services/BoardBLEAgent.swift`
  - Native BLE client for the Mac app
  - Scanning, connect, probing unnamed devices, command sending, and board-speaker writes

- `mac-agent/Sources/BhavyaDeskAgent/Views/MainView.swift`
  - Main app UI layout

- `mac-agent/Sources/BhavyaDeskAgent/Views/GlassComponents.swift`
  - Shared glass cards, conversation timeline, and small UI components

### 5.6 Tooling Files

- `tools/build_mac_agent.sh`
  - Builds and packages the Swift app into `dist/BhavyaDeskAgent.app`

- `tools/run_mac_agent.sh`
  - Runs the Swift app from source during development

- `tools/mac_ble_agent.py`
  - Python BLE scanner and command sender

- `tools/groq_voice_companion.py`
  - Python direct board-voice loop using the board mic and board speaker

### 5.7 Existing Documentation

- `docs/build_notes.md`
- `docs/ble_protocol.md`
- `docs/mac_agent.md`
- `docs/groq_voice_companion.md`
- `docs/mac_ble_agent.md`

## 6. Detailed Runtime Flow

### 6.1 Desktop App Conversation Flow

1. User starts a turn or enables live loop.
2. `MicrophoneRecorder` records audio from the Mac mic.
3. `GroqClient.transcribe(...)` sends the clip to Groq STT.
4. `AppModel` builds a prompt that asks for:
   - a short spoken reply
   - a structured emotion timeline
5. `GroqClient.completeEmotionPlan(...)` sends the request.
6. `AppModel` normalizes the resulting beats so they stay readable on the physical display.
7. The app updates the board with speaking / emotion states over BLE.
8. `MacSpeechPlayer` speaks via:
   - Mac speakers, or
   - converted mu-law payload streamed to the T5-E1 speaker
9. The app returns to idle or re-enters listening in live loop mode.

### 6.2 Python Board Voice Flow

1. `tools/groq_voice_companion.py` asks the board to record with `VOICE REC`.
2. It polls `VOICE STATUS` until capture is ready.
3. It drains chunks using `VOICE READ`.
4. It converts mu-law to WAV locally.
5. Groq performs transcription and text generation.
6. The Python tool generates output speech with Edge, Groq TTS, or macOS speech.
7. The tool converts it back to mu-law.
8. It sends `VOICE PLAY BEGIN`, repeated `VOICE PLAY DATA`, and `VOICE PLAY END`.

### 6.3 Firmware Rendering Flow

1. LVGL timer ticks every `33 ms`.
2. The eye scene decides which emotion is active.
3. Motion, blink state, and natural gaze are blended in.
4. Target geometry is computed for:
   - eye openness
   - gaze offset
   - lid cuts
   - glow strength
   - iris style
5. LVGL objects are updated in place.

## 7. Emotion System

### 7.1 Available Board Emotions

The firmware supports:

- `IDLE`
- `HAPPY`
- `SAD`
- `ANGRY_SOFT`
- `ANGRY_HARD`
- `WOW`
- `SLEEPY`
- `CONFUSED`
- `DIZZY`
- `CALL`
- `SHAKE`
- `WINK`
- `XX`
- `GLITCH`
- `BORED`
- `FOCUSED`
- `HAPPY_TONGUE`
- `HOLLOW`
- `LISTENING`
- `THINKING`
- `CURIOUS`
- `DELIGHTED`
- `SHY`
- `SASSY`
- `PROUD`
- `SPEAKING`

### 7.2 Emotion Ownership

- The firmware knows how to **render** emotions.
- The macOS app knows when and how long to **schedule** emotions during AI conversations.
- Python tools can also schedule emotions manually for testing.

### 7.3 Current Visual Direction

The eyes are currently tuned to a cyan, glowing, cutout-heavy look inspired by MyDeskRobo-style expressions.

Important constraint:

- the current board orientation and current eye size are intentional and should not be changed casually

If you want to improve the face:

- prefer changing lid cuts, glow, timing, and motion style
- avoid rotating the UI unless specifically requested

## 8. BLE Protocol

The board exposes a simple text protocol over Tuya BLE.

Core examples:

```text
PING
STATUS
SET HAPPY
HOLD WINK 700
MOTION ON
VOICE STATUS
VOICE REC 2200
VOICE READ
VOICE PLAY BEGIN 16000
VOICE PLAY DATA <BASE64>
VOICE PLAY END
```

See [BLE Protocol Reference](ble_protocol.md) for the exact command list.

### 8.1 Advertising Identity

- BLE advertised service hint: `0xFDFD`
- Tuya service used by clients: `0xFD50`
- nominal peripheral name: `T5E1-EYES`

The macOS app also supports probing unnamed BLE devices because some board states do not expose the readable local name consistently.

## 9. Audio Subsystem

### 9.1 Board Audio

The board audio path is wrapped by `voice_bridge.c`.

It supports:

- a short speaker beep test
- mu-law playback at 8 kHz
- mu-law microphone capture at 8 kHz
- simple VAD-based early-stop during board capture

### 9.2 Mac Audio

The macOS app uses:

- `AVAudioRecorder` for microphone capture
- `NSSpeechSynthesizer` for native speech synthesis
- `AVAudioPlayer` for Mac-side playback

When routing speech to the board speaker, the Mac app converts synthesized audio to:

- mono
- 8 kHz
- 16-bit PCM
- mu-law encoded bytes

### 9.3 Speaker Routes

The Swift app supports two speaker routes:

- `Mac Speakers`
  - faster and cleaner for the main product experience

- `T5-E1 Speaker`
  - physically embodied output
  - requires a connected board

## 10. External Dependencies

### 10.1 Required

- macOS 15 or newer for the Swift app
- Python 3 with a local `.venv` for tooling and firmware workflows
- Groq API key
- TuyaOpen SDK clone for firmware builds
- T5-E1 / Waveshare T5AI board

### 10.2 Python Packages

Main Python dependencies include:

- `bleak`
- `groq`
- `edge-tts`
- `cmake`
- `ninja`
- Tuya build helper dependencies listed in `requirements.txt`

### 10.3 External SDK Assumption

The firmware build depends on an external TuyaOpen SDK checkout.
That SDK is not committed into this repository.

## 11. Build Workflows

### 11.1 Build the Swift App

Development build:

```bash
swift build --package-path mac-agent --disable-sandbox
```

Packaged app:

```bash
./tools/build_mac_agent.sh
```

Run from source:

```bash
./tools/run_mac_agent.sh
```

### 11.2 Build the Firmware

Recommended flow when the repo path contains spaces:

```bash
REPO_ROOT="$PWD"
WORKSPACE_LINK="/tmp/bhavya_copy_ws"
ln -sfn "$REPO_ROOT" "$WORKSPACE_LINK"

cd "$WORKSPACE_LINK"
env OPEN_COUNTRY_CODE=Other \
PATH="$REPO_ROOT/.venv/bin:$PATH" \
python /path/to/TuyaOpen/tos.py build
```

Main flashable artifact:

```text
dist/t5e1_eye_agent_0.1.0/t5e1_eye_agent_QIO_0.1.0.bin
```

## 12. Flashing Workflow

1. Put the board in a state where macOS exposes a serial device such as `/dev/cu.usbmodem...`
2. Flash with `tyutool_cli.py`

Example:

```bash
cd /tmp/bhavya_copy_ws
PATH="$PWD/.venv/bin:$PATH" \
python /path/to/TuyaOpen/tools/tyutool/tyutool_cli.py write \
  -d T5AI \
  -f /tmp/bhavya_copy_ws/dist/t5e1_eye_agent_0.1.0/t5e1_eye_agent_QIO_0.1.0.bin \
  -p /dev/cu.usbmodemXXXX
```

## 13. Local Development Workflows

### 13.1 If You Are Changing Eye Visuals

You will mostly work in:

- `src/eye_scene.c`
- `include/eye_scene.h`

Typical validation:

- rebuild firmware
- flash board
- test via:
  - Swift app manual emotion buttons, or
  - `tools/mac_ble_agent.py send ...`

### 13.2 If You Are Changing BLE Behavior

You will mostly work in:

- `src/ble_agent.c`
- `include/ble_agent.h`
- `docs/ble_protocol.md`
- `mac-agent/Sources/BhavyaDeskAgent/Services/BoardBLEAgent.swift`
- `tools/mac_ble_agent.py`

Typical validation:

- scan from the Python BLE tool
- connect from the Swift app
- confirm command / reply behavior

### 13.3 If You Are Changing AI Orchestration

You will mostly work in:

- `mac-agent/Sources/BhavyaDeskAgent/AppModel.swift`
- `mac-agent/Sources/BhavyaDeskAgent/Services/GroqClient.swift`
- `mac-agent/Sources/BhavyaDeskAgent/Models/AssistantModels.swift`

Typical validation:

- run the app
- test manual mode
- test live loop
- verify emotion timing feels readable on the board

### 13.4 If You Are Changing Desktop UI

You will mostly work in:

- `mac-agent/Sources/BhavyaDeskAgent/Views/MainView.swift`
- `mac-agent/Sources/BhavyaDeskAgent/Views/GlassComponents.swift`

Important recent lesson:

- keep glass effects attached to card surfaces
- do not wrap the entire content tree in a top-level glass container, because it can blur descendant content on some macOS builds

### 13.5 If You Are Changing Audio Routing

You will mostly work in:

- `mac-agent/Sources/BhavyaDeskAgent/Services/MacSpeechPlayer.swift`
- `mac-agent/Sources/BhavyaDeskAgent/Services/MicrophoneRecorder.swift`
- `src/voice_bridge.c`
- `tools/groq_voice_companion.py`

Typical validation:

- test Mac speaker route
- test T5-E1 speaker route
- test board beep
- test board mic capture if relevant

## 14. Testing And Verification

There is no formal automated test suite yet.

Current verification is manual:

- Swift package build must pass
- packaged app build must pass
- firmware build must pass
- board can be flashed
- BLE connection succeeds
- emotions render correctly
- audio path behaves as expected

Recommended minimum before merging:

- build the Swift package
- package the app
- build firmware if you touched firmware
- run one end-to-end board connection test if you touched BLE or audio

## 15. Troubleshooting

### 15.1 The app cannot hear the Mac microphone

Check:

- `System Settings -> Privacy & Security -> Microphone`
- app bundle has microphone permission strings in `Info.plist`
- app was reopened after rebuild

### 15.2 The app cannot find the board

Check:

- Bluetooth is on
- board is powered and running the BLE-enabled firmware
- board is nearby
- use `tools/mac_ble_agent.py scan`

The Swift app can probe unnamed BLE devices, but if the board is not exposing the Tuya service at all, the app still cannot connect.

### 15.3 The board speaker route does not work

Check:

- board is connected in the app
- app speaker route is set to `T5-E1 Speaker`
- latest firmware with voice bridge support is flashed

### 15.4 The firmware build fails on macOS

Common causes:

- missing or different TuyaOpen SDK clone
- toolchain path issues
- stale temp clone assumptions
- build launched directly from a path with spaces instead of a symlinked workspace

See [Build Notes](build_notes.md).

### 15.5 The UI looks blurry or smeared

Cause:

- global Liquid Glass composition can blur descendant content on some macOS builds

Fix:

- keep glass effects at the card level
- avoid wrapping the whole UI in a top-level glass container

### 15.6 The board is connected over USB but cannot be flashed from the Codex environment

This is expected in some restricted environments.
Use your own Terminal for serial flashing if sandboxed tooling cannot open the serial device.

## 16. Security And Team Hygiene

- Never commit real API keys.
- If a key is pasted into chat or logs, rotate it.
- Do not assume temp SDK clone paths are stable across machines.
- Prefer repo-relative instructions plus environment variables over hardcoded absolute paths in new docs or scripts.

## 17. Known Limitations

- No automated test suite yet
- Firmware build depends on an external TuyaOpen SDK checkout
- Board mic / board speaker paths are best for short utterances
- The live loop is not yet true full-duplex barge-in conversation
- Some vendor packaging steps may fail in restricted environments while still producing the main flashable firmware

## 18. Recommended Next Improvements

- add a lightweight automated verification checklist or script
- stabilize a documented local TuyaOpen SDK bootstrap process
- add screenshots and a short demo walkthrough to the docs
- add a release checklist for firmware + app pairs
- add true streaming / duplex voice mode if the product direction still wants it

## 19. One-Page Mental Model

If you only remember one thing, remember this:

- the **board** is the face, BLE peripheral, and audio edge device
- the **Mac app** is the main AI companion product
- the **Python tools** are for direct testing and debugging

When in doubt, ask:

- Is this change about how Bhavya **looks or receives hardware input**?
  - firmware

- Is this change about how Bhavya **thinks, speaks, or feels as a product**?
  - macOS app

- Is this change about **manual testing or protocol debugging**?
  - Python tools
