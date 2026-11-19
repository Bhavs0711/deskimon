#!/usr/bin/env bash
# Host-side tests (no hardware): firmware emotion state machine (C) and Swift core logic (macOS).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
echo "== firmware: emotion FSM =="
( cd "$ROOT/firmware/components/dsk_face/tests" && gcc -Wall -Wextra -I../include test_fsm.c ../dsk_fsm.c ../dsk_emotion.c -o /tmp/test_fsm && /tmp/test_fsm )
if command -v swift >/dev/null; then echo "== mac agent: core tests =="; ( cd "$ROOT/mac-agent" && swift test ); fi
