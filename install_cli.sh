#!/bin/sh
set -e

PREFIX=${PREFIX:-/opt/homebrew}
BINDIR="$PREFIX/bin"
APP_BIN="$BINDIR/simple-txt"
CLI="$BINDIR/st"

gcc main.c -o "$APP_BIN" \
  -I"$PREFIX/include" \
  -L"$PREFIX/lib" \
  -lraylib \
  -framework CoreServices

rm -f "$CLI"
cp st "$CLI"
chmod +x "$APP_BIN" "$CLI"

echo "Installed $APP_BIN"
echo "Installed $CLI"
echo "You can now run: st README.md"
