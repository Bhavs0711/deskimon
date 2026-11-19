#!/usr/bin/env bash
# Builds (if needed) and launches the Mac Agent.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
APP="$ROOT/release/mac-agent/Deskimon Agent.app"
[ -d "$APP" ] || "$ROOT/scripts/build_mac_agent.sh"
open "$APP"
