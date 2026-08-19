#!/bin/sh
set -e

gcc main.c -o main \
  -I/opt/homebrew/include \
  -L/opt/homebrew/lib \
  -lraylib

./main "$@"
