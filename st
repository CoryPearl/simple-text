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

if [ ! -x ./main ] || [ main.c -nt main ]; then
  gcc main.c -o main \
    -I/opt/homebrew/include \
    -L/opt/homebrew/lib \
    -lraylib \
    -framework CoreServices
fi

if [ "$#" -eq 0 ]; then
  exec ./main
fi

set -- 
OLDIFS=$IFS
IFS='
'
for arg in $ARGS; do
  set -- "$@" "$arg"
done
IFS=$OLDIFS

exec ./main "$@"
