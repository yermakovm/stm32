#!/bin/sh
# Render the Casio face on the host with the real drawing code and dump
# ASCII frames, so font/layout/animation can be eyeballed without a
# panel.  Usage: tools/face_preview/run.sh
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/face_preview
cc -std=c11 -Wall -Wextra -I Core/Inc -I tools/face_preview \
   Core/Src/gfx.c Core/Src/font5x7.c Core/Src/seg7.c Core/Src/face_casio.c \
   tools/face_preview/fake_display.c tools/face_preview/render.c \
   -o build/face_preview/render
exec build/face_preview/render
