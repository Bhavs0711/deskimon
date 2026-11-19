# What was verified, and what was not
Environment used to produce this ZIP: Linux x86_64 sandbox with ESP-IDF 5.3.1 (no board, no Mac, no Swift compiler).

## Verified (actually executed)
| Item | Result |
|---|---|
| Firmware compiles for `esp32s3` (ESP-IDF 5.3.1), clean build from an empty build dir with `IDF_COMPONENT_MANAGER=0` | PASS (1.46 MB app, 0 errors; 1 upstream deprecation warning) |
| Bootloader / partition table / app / merged 16 MB image generated | PASS (`release/firmware`, checksums in `SHA256SUMS.txt`) |
| Emotion state machine: 33 host-side checks (listening->thinking->success, error->confused chain, proud->celebrating chain, alert persistence + dismiss, idle ladder waiting->sleepy->sleeping, wake-on-touch, disconnect, pairing, 45 s watchdog, name/alias lookup, >=24 states) | PASS (`scripts/run_tests.sh`) |
| Kokoro server Python syntax | PASS (not run; model files absent) |

## Written but NOT compiled or run (no Swift toolchain / Mac in the build environment)
The whole `mac-agent/` package (about 2,500 lines of Swift) was written carefully against the macOS 14 SDK APIs but **has never been compiled**.
Expect to fix a handful of compile errors on first `swift build` (typically small type/optional issues). Please run `./scripts/build_mac_agent.sh` and `./scripts/run_tests.sh`;
`mac-agent/Tests/DeskimonCoreTests/CoreTests.swift` holds 14 tests of the parser, validator and protocol framing that are expected to pass but have not been run.
Not verified: EventKit create/move/delete/list, Reminders, permission prompts, notifications, NSWorkspace launching, Mail/Gmail sending, CoreBluetooth, Network.framework server, Speech recognition, Kokoro playback, Ollama/API calls.

## HARDWARE TEST REQUIRED
Everything that touches the physical board (see `docs/HARDWARE.md`), plus: the visual look/smoothness of the 30 emotions (they are LVGL objects and were never rendered on a screen or simulator),
BLE provisioning, Wi-Fi discovery/TCP link, microphone streaming and speaker playback end-to-end, IMU shake/tilt, backlight dimming when sleepy.

## Integration checklist to run once you have both
1. Flash, long-press screen -> *Pairing mode*. 2. Agent -> Pair -> header chips green, device shows "Paired". 3. Type "Schedule test tomorrow at 4 PM" in the Agent: event appears in Calendar, device shows Success + overlay.
4. Tap device, say the same -> same result (check the Activity log shows `[deskimon]`). 5. Quit the Agent -> device goes Concerned then Disconnected, clock keeps running. 6. "Draft an email..." -> draft in Agent, nothing sent until you approve.
7. "Set an alarm for <1 minute from now>" -> notification + device Alert until you tap it.
