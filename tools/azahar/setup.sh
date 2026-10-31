#!/bin/bash
# Restore the Azahar test environment on muze's VM after an environment reset.
#
# What does NOT survive a VM replacement: apt packages, /tmp, running
# processes. What survives: everything under ~ (including the extracted
# AppImage in ~/workspace/tools/azahar). This script reinstalls the
# packages and re-fetches Azahar only if the files are missing.
# Idempotent: safe to run repeatedly.
set -e

# 1) System packages
#    libopengl0/libegl1: Azahar (Qt/OpenGL) fails to start without them
#    xdotool: synthetic key/touch input; xvfb: virtual display; ffmpeg: screenshots
sudo apt-get update -qq
sudo apt-get install -y -qq libopengl0 libegl1 xdotool xvfb ffmpeg

# 2) Azahar 2126.2 (same version used on the Windows side)
AZ_DIR="$HOME/workspace/tools/azahar"
mkdir -p "$AZ_DIR/shots"
cd "$AZ_DIR"
if [ ! -f azahar.AppImage ]; then
  curl -sL -o azahar.AppImage \
    https://github.com/azahar-emu/azahar/releases/download/2126.2/azahar.AppImage
  chmod +x azahar.AppImage
fi
if [ ! -d squashfs-root ]; then
  ./azahar.AppImage --appimage-extract >/dev/null
fi

echo "Azahar test environment ready in $AZ_DIR"
echo "See tools/azahar/README.md in this repo for how to run and drive it."
