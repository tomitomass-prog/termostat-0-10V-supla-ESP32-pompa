#!/usr/bin/env sh
set -eu
PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$PROJECT_DIR"
python3 -m platformio run -e esp32dev
printf '\nPlik aplikacji: %s\n' "$PROJECT_DIR/.pio/build/esp32dev/firmware.bin"
printf 'Do pierwszego programowania uzyj pliku scalonego tworzonego przez workflow GitHub Actions.\n'
