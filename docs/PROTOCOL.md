# Deskimon wire protocol
Newline-delimited JSON (one object per line, UTF-8). Same messages over **Wi-Fi TCP :8765** and **BLE** (GATT service
`6E5D0001-B5A3-F393-E0A9-E50E24DCCA9E`; RX `...0002` write Mac->device, TX `...0003` notify device->Mac, CFG `...0004` write, pairing mode only).
Discovery: device broadcasts `{"type":"discover","device":"deskimon"}` to UDP :8766; Mac answers `{"type":"agent","port":8765,"name":"..."}`.
Max line length: 4 KB (firmware), so TTS chunks are 1024 bytes of PCM.

## Handshake (mutual HMAC-SHA256 with the pairing token)
1. D->M `{"type":"hello","device":"deskimon","id":"dsk-xxxxxxxx","fw":"1.0.0","nonce":"<16B hex>","caps":{"mic":true,"speaker":true,"imu":true,...}}`
2. M->D `{"type":"challenge","nonce":"<16B hex>"}`
3. D->M `{"type":"auth","id":..,"hmac":hex(HMAC(token, macNonce))}`
4. M->D `{"type":"welcome","hmac":hex(HMAC(token, deviceNonce)),"epoch":..,"tz_offset_min":..,"name":..}` - the device verifies this too.
Anything else before authentication is ignored. Liveness: Mac `ping` every 5 s, device `hb` every 5 s; 15 s (device) / 20 s (Mac) of silence = link down -> reconnect.

## Device -> Mac
`{"type":"command","id":12,"source":"deskimon","text":"Schedule AI review tomorrow at 4 PM"}`
`audio_start {id,rate:16000,format:"pcm_s16le"}`, `audio {id,seq,data:<base64 PCM>}`..., `audio_end {id}` (Mac runs STT then the same command path)
`event {"event":"dismiss"}`, `hb`, `pong`

## Mac -> Device
`{"type":"result","id":12,"success":true,"message":"AI review scheduled tomorrow at 4:00 PM.","emotion":"success","intent":"create_event","title":"Event added","overlay":"meeting"}`
`emotion {emotion,hold_ms}` - one of the 30 names in `dsk_emotion.h` (`neutral happy very_happy excited focused thinking listening speaking processing confused concerned worried tired sleepy sleeping surprised proud celebrating curious waiting alert deadline_warning success error disappointed motivating busy calm disconnected pairing`)
`state {state: listening|thinking|processing|busy|speaking|idle}`, `overlay {kind,title,subtitle,duration_ms}`
`alert {kind: alarm|meeting|reminder|deadline, title, subtitle, hold_ms}` (stays until dismissed), `deadline_near`, `task_done`, `time`, `backlight`, `tts {rate,data,last}`, `unpair`, `ping`, `hb_ack`

## Provisioning (BLE CFG characteristic, pairing mode only)
`{"ssid":"..","pass":"..","token":"<32-64 chars>","host":"optional mac hostname"}`

## Intents (Mac side, `IntentName`)
create_event update_event delete_event list_events create_reminder create_alarm draft_email send_email open_app open_file open_url get_schedule general_work_query (+ confirm / cancel).
