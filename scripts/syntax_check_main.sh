#!/usr/bin/env sh
set -eu
PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CXX=${CXX:-g++}
"$CXX" -std=c++17 -Wall -Wextra -Werror -fsyntax-only \
  -I"$PROJECT_DIR/test/stubs" -I"$PROJECT_DIR/src" \
  "$PROJECT_DIR/src/main.cpp"
printf 'main.cpp: kontrola skladni zakonczona powodzeniem\n'
