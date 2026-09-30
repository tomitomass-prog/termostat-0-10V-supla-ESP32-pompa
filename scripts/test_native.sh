#!/usr/bin/env sh
set -eu
PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CXX=${CXX:-g++}
"$CXX" -std=c++17 -Wall -Wextra -Werror \
  "$PROJECT_DIR/test/native/test_heating_core.cpp" \
  -o "$PROJECT_DIR/test/native/test_heating_core"
"$PROJECT_DIR/test/native/test_heating_core"
