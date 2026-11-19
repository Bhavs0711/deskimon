#!/usr/bin/env bash
# Builds the Swift Mac Agent and wraps it into release/mac-agent/Deskimon Agent.app  (macOS 14+, Xcode 15+ / Swift 5.9+)
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/mac-agent"
swift build -c release
BIN="$(swift build -c release --show-bin-path)/DeskimonAgent"
APP="$ROOT/release/mac-agent/Deskimon Agent.app"
rm -rf "$APP"; mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp "$BIN" "$APP/Contents/MacOS/DeskimonAgent"
cp Resources/Info.plist "$APP/Contents/Info.plist"
codesign --force --deep --sign - --entitlements Resources/DeskimonAgent.entitlements "$APP"
echo "Built: $APP"
echo "Run:   open \"$APP\""
