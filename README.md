# LEDMatrix

Wi-Fi-controlled RGB LED matrix firmware and web UI for HUB75 panels. The project supports Adafruit Matrix Portal S3, Matrix Portal M4, and a Raspberry Pi driver based on `hzeller/rpi-rgb-led-matrix`.

It provides a shared JSON API for text, color, brightness, panel objects, emoji sprites, background color, and display rotation.

## Features

- 64x64 HUB75 panel support, with support for horizontally chained panels.
- Matrix Portal S3 firmware with LittleFS-hosted web UI.
- Matrix Portal M4 firmware with an inline web UI and Wi-FiNINA AirLift support.
- Raspberry Pi implementation using `rpi-rgb-led-matrix`.
- Generated 48x48 RGB565 emoji sprites for microcontrollers.
- RGBA PNG emoji assets for the Raspberry Pi renderer and web previews.
- S3 BLE Wi-Fi provisioning and captive-portal setup mode.
- Static emoji objects with position and size controls.

## Hardware

Supported controller options:

- Adafruit Matrix Portal ESP32-S3
- Adafruit Matrix Portal M4
- Raspberry Pi with a compatible HUB75 interface/level-shifter board

You also need:

- One or more HUB75 RGB LED panels
- A regulated 5 V power supply sized for the panels
- HUB75 cabling and, for Raspberry Pi installations, a suitable level-shifter/interface board

Do not power an LED panel from the Raspberry Pi. Disconnect power before connecting or disconnecting HUB75 cables. Start with one 64x64 panel while validating wiring. Set the panel's Address E jumper as required by the controller and panel combination.

## Repository Layout

```text
src/main.cpp                         Matrix Portal S3 firmware
src/m4_test.cpp                      Matrix Portal M4 firmware
include/config.h                     S3 panel and Wi-Fi configuration
include/generated_emoji_sprites.h    Generated microcontroller sprites
web/                                 S3 LittleFS web UI and image assets
rpi/                                 Raspberry Pi server, UI, and installer
assets/                              Source custom emoji images
tools/generate_emoji_sprites.swift   Sprite and PNG generator
platformio.ini                       PlatformIO environments
```

## Build Environment

Install:

- [PlatformIO](https://platformio.org/)
- Xcode Command Line Tools, including Swift, for sprite generation

The firmware dependencies are declared in `platformio.ini` and are installed automatically by PlatformIO.

Before building the S3 firmware, edit `include/config.h` for the target panel count and local Wi-Fi credentials. Do not commit real Wi-Fi credentials to a public repository. The M4 test firmware has its Wi-Fi settings near the top of `src/m4_test.cpp`; treat those as local configuration as well.

## Matrix Portal S3

Build the application firmware:

```bash
pio run -e matrix-portal-s3
```

Upload firmware and the LittleFS web UI:

```bash
pio run -e matrix-portal-s3 --target upload
pio run -e matrix-portal-s3 --target uploadfs
```

For a fresh device, the S3 starts the `LEDMatrix-Setup` access point when it cannot connect to a saved network. Connect to it and open `http://192.168.4.1/` to configure Wi-Fi. Credentials are stored in ESP32 NVS.

The S3 also exposes BLE provisioning. The service and characteristics are documented in the source and support status, save, and setup commands.

## Matrix Portal M4

Build:

```bash
pio run -e matrix-portal-m4-test
```

Upload and monitor:

```bash
pio run -e matrix-portal-m4-test --target upload
pio device monitor -e matrix-portal-m4-test
```

The M4 uses its onboard ESP32 AirLift through WiFiNINA. It does not provide BLE. If station connection fails, the firmware falls back to:

- SSID: `LEDMatrix-M4`
- Password: `matrix-test`

Open the IP address printed by the firmware in a browser. The M4 web UI is compiled into the firmware because the M4 build does not use LittleFS.

## Raspberry Pi

The Pi implementation is documented in [`rpi/README.md`](rpi/README.md). The short version is:

```bash
chmod +x rpi/install.sh
sudo rpi/install.sh
```

The installer builds `rpi-rgb-led-matrix`, creates a Python virtual environment, and installs the `ledmatrix-rpi` systemd service. The default server port is `8080`.

For a hardware-free test:

```bash
python3 rpi/server.py --config rpi/config.example.json --dry-run
```

For manual hardware execution:

```bash
cp rpi/config.example.json rpi/config.json
rpi/.venv/bin/python rpi/server.py --config rpi/config.json
```

Useful service commands:

```bash
sudo systemctl status ledmatrix-rpi
sudo journalctl -u ledmatrix-rpi -f
sudo systemctl restart ledmatrix-rpi
```

## HTTP API

The S3, M4, and Pi implementations expose the following core endpoints:

```text
GET  /api/state
POST /api/display
```

Example command for text and panel appearance:

```json
{
  "text": "HELLO",
  "color": {"r": 255, "g": 80, "b": 0},
  "brightness": 96,
  "background": {"r": 0, "g": 0, "b": 0},
  "rotation": 0
}
```

Example emoji object command:

```json
{
  "objects": [
    {"emoji": "🔥", "x": 8, "y": 4, "size": 48},
    {"emoji": "fuck-afd", "x": 72, "y": 4, "size": 56}
  ]
}
```

Object coordinates use the full logical display. Supported object sizes are 8 through 64 pixels. `GET /api/state` returns the current panel configuration and object list.

## Custom Emoji Assets

Source images belong in `assets/`. The generator creates:

- RGB565 sprites in `include/generated_emoji_sprites.h`
- 48x48 RGBA PNGs in `rpi/assets/`

Run the generator directly on macOS with:

```bash
swift tools/generate_emoji_sprites.swift include/generated_emoji_sprites.h
```

The M4 PlatformIO environment runs the generator automatically through `tools/generate_emoji_sprites.py`. Copy any newly generated web preview PNGs into `web/` before building the S3 filesystem image.

The current custom asset identifiers include:

```text
fuck-off-smiley
fuck-you-smiley
fuck-you-double-text
fuck-you-double
fuck-afd
```

## Firmware Artifacts

Build artifacts are intentionally not committed to the repository. Build from source with PlatformIO for reproducible results. The S3 application image and LittleFS image must be flashed together when using locally generated binaries.

## Current Scope

The project currently renders static emoji and pixel-art objects. Animated emoji, such as a fire animation, is planned as a future local frame-based display mode. The intended approach is 6–8 RGB565 frames at roughly 10–12 FPS, stored in flash and advanced with a non-blocking `millis()` timer.

## Contributing

Issues and pull requests are welcome. When contributing:

- Keep controller-specific behavior in the appropriate firmware path.
- Regenerate sprites after changing source assets.
- Build both PlatformIO environments when changing shared sprites or API behavior.
- Do not include Wi-Fi passwords, private IP addresses, or deployment-specific configuration in commits.

## License

No license has been selected for this project yet. Until a license is added, all rights remain with the copyright holder.
