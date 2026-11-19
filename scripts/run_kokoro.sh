#!/usr/bin/env bash
# Starts the local Kokoro TTS server (http://127.0.0.1:8000/tts).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
[ -d .venv-kokoro ] || { python3 -m venv .venv-kokoro; . .venv-kokoro/bin/activate; pip install -r requirements-kokoro.txt; }
. .venv-kokoro/bin/activate
exec python tools/kokoro_server.py
