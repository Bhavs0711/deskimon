# Deskimon PC Simulator

A desktop simulator for the Deskimon firmware UI using LVGL 8.4.0 and SDL2. This allows you to run and test the complete Deskimon interface on your Mac without needing the physical hardware.

## Features

- ✅ Runs the **actual Deskimon firmware UI code** (no modifications to original ESP32 code)
- ✅ Full 360x360 display rendering with LVGL 8.4.0
- ✅ Mouse input emulates touchscreen
- ✅ **Manual emotion control via keyboard** (no auto-cycling by default)
- ✅ **HTTP API for external control** (Swift Agent, scripts, etc.)
- ✅ **Overlay system** for showing task completions, calendar events, etc.
- ✅ Face animations and overlay system fully functional
- ✅ Native macOS M1/M2 support

## Requirements

- macOS (tested on M1/M2)
- CMake 3.10+
- SDL2 (install via Homebrew: `brew install sdl2`)
- C compiler (clang/gcc)

## Building

```bash
cd simulator
mkdir -p build
cd build
cmake ..
make -j4
```

## Running

From the simulator directory:

```bash
./run.sh
```

Or directly from the build directory:

```bash
cd build
./deskimon_sim
```

## Keyboard Controls

| Key | Action |
|-----|--------|
| 0-9 | Set emotions directly (0=Neutral, 1=Happy, 2=Very Happy, 3=Excited, 4=Focused, 5=Thinking, 6=Listening, 7=Speaking, 8=Processing, 9=Confused) |
| ←/→ | Previous/Next emotion |
| T | Toggle auto-cycle emotions (default: OFF) |
| Space | Show "Task Completed" overlay + success emotion |
| C | Show "Calendar Event" overlay |
| M | Show "Email" overlay |
| P | Show "Processing" overlay |
| R | Show "Reminder" overlay |
| A | Show "Alert" overlay |
| ESC | Hide overlay |
| H | Show help |
| Ctrl+C | Exit |

## HTTP API (Port 8080)

The simulator runs an HTTP server on `localhost:8080` for external control. This allows your Swift Agent (or any script) to control the Deskimon UI.

### Set Emotion by Name

```bash
curl -X POST http://localhost:8080/emotion \
  -H "Content-Type: application/json" \
  -d '{"emotion":"happy"}'
```

Available emotions: `neutral`, `happy`, `very_happy`, `excited`, `focused`, `thinking`, `listening`, `speaking`, `processing`, `confused`, `concerned`, `worried`, `tired`, `sleepy`, `sleeping`, `surprised`, `proud`, `celebrating`, `curious`, `waiting`, `alert`, `deadline_warning`, `success`, `error`, `disappointed`, `motivating`, `busy`, `calm`, `disconnected`, `pairing`

### Set Emotion by ID

```bash
curl -X POST http://localhost:8080/emotion/id \
  -H "Content-Type: application/json" \
  -d '{"id":3}'
```

### Show Overlay

```bash
curl -X POST http://localhost:8080/overlay \
  -H "Content-Type: application/json" \
  -d '{"kind":"status","title":"Task Completed","subtitle":"Project review scheduled"}'
```

Overlay kinds: `info`, `meeting`, `reminder`, `alarm`, `email`, `status`, `processing`

### Get Current Status

```bash
curl http://localhost:8080/status
```

Returns: `{"emotion_id":1,"emotion_name":"happy"}`

## Swift Agent Integration Example

When your Swift Agent completes a task (like scheduling a calendar event), it can send HTTP requests to the simulator:

```swift
// Example: Show task completion on Deskimon
func showTaskCompleted(title: String, subtitle: String) {
    let url = URL(string: "http://localhost:8080/overlay")!
    var request = URLRequest(url: url)
    request.httpMethod = "POST"
    request.setValue("application/json", forHTTPHeaderField: "Content-Type")
    
    let body: [String: String] = [
        "kind": "status",
        "title": title,
        "subtitle": subtitle
    ]
    request.httpBody = try! JSONSerialization.data(withJSONObject: body)
    
    URLSession.shared.dataTask(with: request).resume()
}

// Example: Set emotion based on task result
func setEmotion(_ emotion: String) {
    let url = URL(string: "http://localhost:8080/emotion")!
    var request = URLRequest(url: url)
    request.httpMethod = "POST"
    request.setValue("application/json", forHTTPHeaderField: "Content-Type")
    
    let body: [String: String] = ["emotion": emotion]
    request.httpBody = try! JSONSerialization.data(withJSONObject: body)
    
    URLSession.shared.dataTask(with: request).resume()
}

// Usage:
showTaskCompleted(title: "Task Completed", subtitle: "✓ Project review scheduled for tomorrow")
setEmotion("success")
```

## Project Structure

```
simulator/
├── main.c                  # Entry point, keyboard handler, main loop
├── http_control.c/h        # HTTP API server for external control
├── hal_sim.c/h             # ESP32 HAL stubs for desktop
├── lv_conf.h               # LVGL configuration
├── sdkconfig.h             # ESP-IDF config stubs
├── esp_err.h               # ESP error codes
├── esp_random.h            # Random number stub
├── lv_drivers/sdl/         # SDL2 display/input driver
├── esp_stubs/              # Additional ESP-IDF stubs
├── run.sh                  # Launch script
└── CMakeLists.txt          # Build configuration
```

## How It Works

1. **HAL Abstraction**: ESP32-specific APIs (FreeRTOS, GPIO, I2C, IMU) are stubbed out with no-op implementations
2. **SDL2 Display**: LVGL display flush callback converts RGB565 → ARGB8888 for SDL2 rendering
3. **Input Emulation**: SDL2 mouse events are translated to LVGL touch input, keyboard events control emotions
4. **HTTP Server**: Background thread listens on port 8080 for external control commands
5. **Timing**: Uses `gettimeofday()` instead of FreeRTOS ticks for LVGL timer
6. **Original Code**: All Deskimon UI code (`dsk_face`, `dsk_overlay`, `dsk_emotion`) runs unmodified

## Troubleshooting

### "Failed to create SDL window"
- Ensure SDL2 is installed: `brew install sdl2`
- Check that you're running on a system with graphics support

### Build errors about missing headers
- Verify CMake found SDL2: check the CMake output for SDL2 paths
- Run `sdl2-config --cflags --libs` to verify SDL2 installation

### HTTP server not responding
- Check if port 8080 is already in use: `lsof -i :8080`
- The server starts automatically when the simulator launches

### Blank window or no rendering
- Check console output for LVGL or display driver errors
- Verify LVGL buffers are being allocated (check for malloc failures)
