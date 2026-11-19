# Deskimon

An AI desk assistant: a **Waveshare ESP32-S3-Touch-LCD-1.85** round-display device (expressive LVGL face, 30 emotional
states) paired with a native **Swift macOS Agent** (Calendar, Reminders, Alarms, Email drafts, app/URL/folder launching,
local or API LLM). Every command - spoken or typed on the device **or** on the Mac - goes through the same
`Assistant -> CommandRouter` code path.

Status of every feature (built / unit-tested / needs hardware or a Mac) is in [`docs/TESTING.md`](docs/TESTING.md). Read it first.

```
Deskimon_Final/
  firmware/source/       ESP-IDF project (main/, sdkconfig.defaults, partitions.csv)
  firmware/components/   dsk_board (display/touch/IMU), dsk_face (emotions, LVGL), dsk_app (Wi-Fi, BLE, audio, protocol)
                         + vendored LVGL 8.4, esp_lcd_st77916, esp_lcd_touch(_cst816s)  -> builds offline
  release/firmware/      bootloader.bin, partition-table.bin, deskimon.bin, deskimon_merged_16MB.bin, SHA256SUMS.txt
  release/mac-agent/     "Deskimon Agent.app" appears here after scripts/build_mac_agent.sh
  mac-agent/             Swift package (DeskimonCore + DeskimonAgent app + tests)
  models/kokoro/         put kokoro-v1.0.onnx and voices-v1.0.bin here
  tools/ scripts/        Kokoro server, build / flash / run scripts
  docs/                  PROTOCOL.md, HARDWARE.md, TESTING.md
  legacy/                old Tuya firmware + Python helpers (reference only)
```

## 1. Requirements (macOS)
- macOS 14 Sonoma or newer, Xcode 15+ (Swift 5.9+) for the Mac Agent
- ESP-IDF **5.3.1** (the version used to build the shipped binaries; 5.3.x/5.4 should work) - `scripts/setup_idf.sh` installs it
- Python 3.9+ and `pip3 install esptool` (only for flashing the prebuilt binaries)
- Optional: [Ollama](https://ollama.com) for a local LLM, a Groq/OpenAI-compatible API key, Python 3.10+ for Kokoro TTS

**IMPORTANT**: No Tuya SDK required. This project uses **native ESP-IDF drivers only**. See [HARDWARE.md](docs/HARDWARE.md) for details.

## 2. Flash the prebuilt firmware (no toolchain needed)
```bash
pip3 install esptool
./scripts/flash_deskimon.sh                 # auto-detects the port
./scripts/flash_deskimon.sh /dev/cu.usbmodem1101   # or give it explicitly
./scripts/flash_deskimon.sh --erase         # wipe everything first (also forgets pairing)
```
Manual equivalent (single merged image):
```bash
python3 -m esptool --chip esp32s3 -p /dev/cu.usbmodem1101 -b 460800 --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB 0x0 release/firmware/deskimon_merged_16MB.bin
```
or separate files:
```bash
python3 -m esptool --chip esp32s3 -p PORT -b 460800 write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB \
  0x0 release/firmware/bootloader.bin 0x8000 release/firmware/partition-table.bin 0x10000 release/firmware/deskimon.bin
```
**Finding the serial port:** plug the board's USB-C into the Mac, then `ls /dev/cu.usbmodem* /dev/cu.usbserial*`.
If nothing appears: hold **BOOT**, tap **RESET**, release BOOT (download mode), or try another cable (many are charge-only).

## 3. Build the firmware yourself
```bash
./scripts/setup_idf.sh          # once
. ~/esp/esp-idf/export.sh       # every new terminal
./scripts/build_firmware.sh     # -> release/firmware/*.bin
```
Equivalent: `cd firmware/source && IDF_COMPONENT_MANAGER=0 idf.py set-target esp32s3 && idf.py build flash monitor -p PORT`.
All dependencies (LVGL 8.4, ST77916 driver, CST816S touch driver) are vendored in `firmware/components`, so the build needs no component-registry access.
Options: `idf.py menuconfig` -> *Deskimon board / face / application* (LCD clock, backlight, face FPS, default Wi-Fi, audio on/off).

## 4. Mac Agent
```bash
./scripts/build_mac_agent.sh     # -> release/mac-agent/Deskimon Agent.app  (ad-hoc signed)
./scripts/run_mac_agent.sh       # builds if needed, then opens the app
```
Run command (as requested): `./scripts/run_mac_agent.sh`. Run the tests: `./scripts/run_tests.sh`.
Run it as the **.app** (not a bare `swift run`): macOS only shows permission prompts and native notifications for a bundled app.

### Permissions
On first launch macOS asks for: **Calendars** (full access), **Reminders** (full access), **Notifications**, **Speech Recognition**, **Bluetooth**,
and later **Microphone**, **Contacts** (only to resolve "email Rahul"), **Automation -> Mail** (only when you send via Apple Mail), and **Local Network**.
If you clicked *Don't Allow*: System Settings -> Privacy & Security -> Calendars / Reminders / ... -> enable *Deskimon Agent*.
Deskimon never bypasses these prompts.

### Pair / connect Deskimon
1. Power the device. Unpaired devices enter **Pairing mode** automatically (blue orbiting eyes). Otherwise **long-press the screen**.
2. In the Agent window -> *Pair / connect*: enter your **2.4 GHz** Wi-Fi name and password (the same network as the Mac) -> **Pair**.
3. The Mac finds the device over BLE, creates a random 192-bit secret, stores it in the **Keychain**, and writes it plus the Wi-Fi credentials to the device (accepted only in pairing mode).
4. The device joins Wi-Fi, finds the Mac by UDP broadcast, connects over TCP and both sides authenticate with a mutual HMAC-SHA256 challenge. The header chips turn green.
If Wi-Fi drops, BLE carries commands as a fallback; if the Mac is gone the face switches to *disconnected* and keeps working (clock, idle animations).

### Using it
Tap the face and speak (needs Wi-Fi link) - swipe up = today's meetings, swipe down = next meeting, swipe sideways = clock,
long-press = pairing, tap while an alarm rings = dismiss. Or type/dictate in the Agent window. Examples: *"Schedule project review tomorrow at 4 PM"*,
*"Move my AI project meeting to 5 PM"*, *"Cancel tomorrow's meeting"*, *"What meetings do I have today?"*, *"Remind me every Monday at 9"*,
*"Set an alarm for 7 AM"*, *"Draft an email to my professor asking for tomorrow's meeting"*, *"Open VS Code"*, *"Open my project folder"* (set it in Settings).

### Email authorization
Drafts are generated (LLM or offline template) and shown in the Agent window with **To / Subject / Body**. Nothing is sent until you click **Send...** *and* confirm the
macOS dialog - even when the request came from the device. Two send methods (Settings -> Email):
- **Apple Mail** - uses the accounts already signed into Mail.app; macOS asks for Automation permission. No credentials are handled by Deskimon.
- **Gmail (OAuth)** - create a Google Cloud OAuth client of type *Desktop app*, enable the Gmail API, put the client ID (and secret) in Settings. Sign-in opens your browser (PKCE, loopback redirect); only the refresh token is kept, in the Keychain.
No passwords are stored anywhere; API keys and tokens live in the Keychain.

### Language model (optional)
Settings -> Language model: *Offline rules* (default; handles all the example phrases without any model), *Ollama* (`ollama pull llama3.2`), or an OpenAI-compatible API (Groq default).
The model only proposes structured JSON; `IntentValidator` and `CommandRouter` validate it, and there is no shell-execution path at all.

### Kokoro TTS
Put `kokoro-v1.0.onnx` and `voices-v1.0.bin` in `models/kokoro/` (see `models/kokoro/README.md`), then `./scripts/run_kokoro.sh`
(serves `http://127.0.0.1:8000/tts`; `pcm24k` for the Mac, `pcm16k` for the Deskimon speaker). Without it the Mac uses the system voice and the device speaker is silent.

## 5. Troubleshooting
| Symptom | Fix |
|---|---|
| Screen black after flashing | Wait 3 s; press RESET; try `--erase`. In `menuconfig` lower *LCD QSPI clock* to 40 MHz. Check serial log for `board ready`. |
| Colours wrong / inverted | `dsk_board.c`: toggle `esp_lcd_panel_invert_color`, or `CONFIG_LV_COLOR_16_SWAP` in `sdkconfig.defaults`. HARDWARE TEST REQUIRED |
| Touch does nothing | Log shows `touch init failed`? Check the touch cable; swap/mirror flags in `touch_init()`. |
| Agent says "Mac not connected" on the device | Same Wi-Fi? 2.4 GHz? macOS firewall -> allow *Deskimon Agent* incoming. Guest/"client isolation" networks block discovery. |
| Pairing rejected | Device must show *Pairing mode* (long-press). Bluetooth must be allowed for Deskimon Agent. |
| No calendar/reminder results | Privacy settings (above). Quit & relaunch the Agent after changing them. |
| No notifications | Launch the `.app` (not `swift run`); allow in System Settings -> Notifications. |
| No voice from device mic | Needs Wi-Fi link + Speech Recognition permission. Audio pins/format are HARDWARE TEST REQUIRED (`docs/HARDWARE.md`). |
| `idf.py` not found | `. ~/esp/esp-idf/export.sh` |
