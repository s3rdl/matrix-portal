# Raspberry Pi Test Package

This package drives one HUB75 panel through the Raspberry Pi GPIO header and the ElectroDragon-style level-shifter board. It uses [hzeller/rpi-rgb-led-matrix](https://github.com/hzeller/rpi-rgb-led-matrix) and exposes the same basic API as the Matrix Portal firmware.

## Safety and wiring

- Disconnect power before plugging or unplugging HUB75 cables.
- Feed the driver board and panels from a regulated 5 V supply. Do not power a panel from the Pi.
- Connect the Pi ground and the driver-board ground.
- Seat the driver board on the Pi 40-pin header according to its pinout. Do not guess if the header orientation is unclear.
- Start with one 64x64 panel and one HUB75 cable.
- The panel's Address E setting and the driver-board's 3V3/5V selection must match the panel and adapter documentation.

## Install on Raspberry Pi OS

Copy or clone this project onto the Pi, then run from the project root:

```bash
cd LEDMatrix
chmod +x rpi/install.sh
sudo rpi/install.sh
```

The installer builds `rpi-rgb-led-matrix`, creates a Python virtual environment, and starts the `ledmatrix-rpi` systemd service.

The test server listens on port `8080`. Find the Pi address with:

```bash
hostname -I
```

Open `http://PI_IP:8080/` in a browser.

## Manual run and dry run

On the Pi, copy `config.example.json` to `config.json` and adjust the panel options if required:

```bash
cp rpi/config.example.json rpi/config.json
rpi/.venv/bin/python rpi/server.py --config rpi/config.json
```

To test the HTTP server without LED hardware or the `rgbmatrix` Python binding:

```bash
python3 rpi/server.py --config rpi/config.example.json --dry-run
```

The default driver options are one 64x64 panel, `regular` GPIO mapping, chain length 1, parallel 1, rotation 0, and GPIO slowdown 2. For four panels connected in one daisy-chain, set `chain_length` to `4`; the renderer automatically uses the resulting 256x64 canvas. Set `rotation` to `180` to turn the complete wall upside down in software; `90` and `270` are also supported for rotated layouts. The UI supports multiple pixel-art objects positioned with full-wall top-left coordinates (`x` 0-255, `y` 0-63) and sizes from 8 to 64 pixels. An object can be clipped if its position plus size reaches beyond the wall edge. Hardware pulsing is configurable with `disable_hardware_pulsing`; leave it `false` after disabling onboard audio for the least flicker. All picker emoji use the same 48x48 Apple Color Emoji source sprites as the M4 and are resized with Lanczos on the Pi. The generated PNG assets are RGBA and retain antialiased edges.

All picker emoji use the same 48x48 Apple Color Emoji source sprites as the M4 and are resized with Lanczos on the Pi. The generated PNG assets are RGBA and retain antialiased edges.

## Service commands

```bash
sudo systemctl status ledmatrix-rpi
sudo journalctl -u ledmatrix-rpi -f
sudo systemctl restart ledmatrix-rpi
```
