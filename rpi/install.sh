#!/usr/bin/env bash
set -euo pipefail

INSTALL_DIR=/opt/ledmatrix
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
if [ -f "$SCRIPT_DIR/server.py" ]; then
  SOURCE_DIR="$SCRIPT_DIR"
else
  SOURCE_DIR="$SCRIPT_DIR/rpi"
fi
TARGET_DIR="$INSTALL_DIR/rpi"
MATRIX_DIR="$INSTALL_DIR/rpi-rgb-led-matrix"

sudo apt-get update
sudo apt-get install -y git build-essential python3-dev python3-venv python3-pil cython3 fonts-dejavu
sudo mkdir -p "$INSTALL_DIR"
sudo mkdir -p "$TARGET_DIR"
if [ "$SOURCE_DIR" != "$TARGET_DIR" ]; then
  sudo cp "$SOURCE_DIR/server.py" "$SOURCE_DIR/requirements.txt" \
    "$SOURCE_DIR/config.example.json" "$SOURCE_DIR/ledmatrix-rpi.service" "$TARGET_DIR/"
  sudo cp -R "$SOURCE_DIR/web" "$TARGET_DIR/"
  sudo cp -R "$SOURCE_DIR/assets" "$TARGET_DIR/"
  if [ -f "$SOURCE_DIR/config.json" ]; then
    sudo cp "$SOURCE_DIR/config.json" "$TARGET_DIR/config.json"
  fi
fi

if [ ! -d "$MATRIX_DIR/.git" ]; then
  sudo git clone https://github.com/hzeller/rpi-rgb-led-matrix.git "$MATRIX_DIR"
fi
sudo make -C "$MATRIX_DIR"

if [ ! -d "$TARGET_DIR/.venv" ]; then
  sudo python3 -m venv --system-site-packages "$TARGET_DIR/.venv"
fi
sudo "$TARGET_DIR/.venv/bin/python" -m pip install --upgrade pip
sudo "$TARGET_DIR/.venv/bin/python" -m pip install -r "$TARGET_DIR/requirements.txt"
sudo "$TARGET_DIR/.venv/bin/python" -m pip install "$MATRIX_DIR"

if [ ! -f "$TARGET_DIR/config.json" ]; then
  sudo cp "$TARGET_DIR/config.example.json" "$TARGET_DIR/config.json"
fi
sudo install -m 0644 "$TARGET_DIR/ledmatrix-rpi.service" /etc/systemd/system/ledmatrix-rpi.service
sudo systemctl daemon-reload
sudo systemctl enable --now ledmatrix-rpi.service
sudo systemctl --no-pager --full status ledmatrix-rpi.service
