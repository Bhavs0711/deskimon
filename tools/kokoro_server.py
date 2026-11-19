"""Local Kokoro TTS server for Deskimon.  POST /tts {"text","voice","speed","format"}
format: "pcm16k" (Deskimon speaker), "pcm24k" (Mac).  Returns raw 16-bit mono PCM."""
import os, sys, urllib.request
import numpy as np
import scipy.signal
import uvicorn
from fastapi import FastAPI, Request
from fastapi.responses import StreamingResponse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MODEL_DIR = os.environ.get("DESKIMON_KOKORO_DIR", os.path.join(ROOT, "models", "kokoro"))
MODEL_PATH = os.path.join(MODEL_DIR, "kokoro-v1.0.onnx")
VOICES_PATH = os.path.join(MODEL_DIR, "voices-v1.0.bin")
BASE = "https://github.com/thewh1teagle/kokoro-onnx/releases/download/model-files-v1.0/"

def fetch(name, path):
    if os.path.exists(path):
        return
    print(f"{path} not found - downloading {name} ...")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    urllib.request.urlretrieve(BASE + name, path)

fetch("kokoro-v1.0.onnx", MODEL_PATH)
fetch("voices-v1.0.bin", VOICES_PATH)

from kokoro_onnx import Kokoro
print("Initializing Kokoro engine...")
kokoro = Kokoro(MODEL_PATH, VOICES_PATH)
print("Kokoro server ready on http://127.0.0.1:8000/tts")
app = FastAPI()

RATES = {"pcm16k": 16000, "pcm24k": 24000, "pcm8k": 8000}

@app.post("/tts")
async def tts(request: Request):
    data = await request.json()
    text = (data.get("text") or "").strip()[:1200]
    if not text:
        return {"error": "text required"}
    voice = data.get("voice", "af_bella")
    speed = float(data.get("speed", 1.0))
    out_rate = RATES.get(data.get("format", "pcm24k"), 24000)
    samples, sr = kokoro.create(text, voice=voice, speed=speed, lang="en-us")
    if sr != out_rate:
        g = np.gcd(int(sr), out_rate)
        samples = scipy.signal.resample_poly(samples, out_rate // g, int(sr) // g)
    pcm = (np.clip(samples, -1, 1) * 32767).astype(np.int16).tobytes()

    def chunks():
        for i in range(0, len(pcm), 4096):
            yield pcm[i:i + 4096]

    return StreamingResponse(chunks(), media_type="audio/pcm",
                             headers={"X-Sample-Rate": str(out_rate), "X-Audio-Length": str(len(pcm))})

if __name__ == "__main__":
    uvicorn.run(app, host="127.0.0.1", port=8000)
