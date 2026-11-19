#!/usr/bin/env bash
# Installs ESP-IDF v5.3.1 into ~/esp (macOS). Run once.
set -euo pipefail
command -v brew >/dev/null || { echo "Install Homebrew first: https://brew.sh"; exit 1; }
brew install cmake ninja dfu-util python@3.11 git ccache || true
mkdir -p ~/esp && cd ~/esp
[ -d esp-idf ] || git clone -b v5.3.1 --recursive --shallow-submodules https://github.com/espressif/esp-idf.git
cd esp-idf && ./install.sh esp32s3
echo
echo "Done. In each new terminal run:  . ~/esp/esp-idf/export.sh"
