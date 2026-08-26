#!/bin/sh
set -e

SOURCE=$0
while [ -L "$SOURCE" ]; do
  DIR=$(CDPATH= cd -- "$(dirname -- "$SOURCE")" && pwd)
  TARGET=$(readlink "$SOURCE")
  case "$TARGET" in
    /*) SOURCE=$TARGET ;;
    *) SOURCE=$DIR/$TARGET ;;
  esac
done

APP_DIR=$(CDPATH= cd -- "$(dirname -- "$SOURCE")" && pwd)
CALL_DIR=$(pwd)
APP_BIN="$APP_DIR/main"

if [ ! -f "$APP_DIR/main.c" ]; then
  APP_BIN="$APP_DIR/simple-txt"
  if [ ! -x "$APP_BIN" ]; then
    echo "st: installed Simple Txt binary not found at $APP_BIN" >&2
    echo "st: run ./install_cli.sh from the Simple Txt source folder to reinstall it" >&2
    exit 1
  fi
fi

ARGS=
for arg in "$@"; do
  case "$arg" in
    -*|/*) resolved=$arg ;;
    *) resolved=$CALL_DIR/$arg ;;
  esac
  ARGS="${ARGS}
$resolved"
done

cd "$APP_DIR"

if [ -f "$APP_DIR/main.c" ] && { [ ! -x "$APP_BIN" ] || [ "$APP_DIR/main.c" -nt "$APP_BIN" ]; }; then
  gcc main.c -o main \
    -I/opt/homebrew/include \
    -L/opt/homebrew/lib \
    -lraylib \
    -framework CoreServices
fi

if [ "$#" -eq 0 ]; then
  exec "$APP_BIN"
fi

set -- 
OLDIFS=$IFS
IFS='
'
for arg in $ARGS; do
  set -- "$@" "$arg"
done
IFS=$OLDIFS

exec "$APP_BIN" "$@"
