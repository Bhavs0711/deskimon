# Kokoro TTS model files
Place these two files in THIS folder (they were excluded from the ZIP because of size):

    models/kokoro/kokoro-v1.0.onnx
    models/kokoro/voices-v1.0.bin

If they are missing, `scripts/run_kokoro.sh` downloads them from the official kokoro-onnx release
(https://github.com/thewh1teagle/kokoro-onnx/releases/tag/model-files-v1.0). If the Kokoro server is not running the
Mac Agent falls back to the built-in macOS voice, and the Deskimon speaker stays silent (text still shows on screen).
