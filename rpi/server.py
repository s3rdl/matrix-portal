#!/usr/bin/env python3
"""Small Raspberry Pi HUB75 test server for rpi-rgb-led-matrix."""

import argparse
import json
import logging
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parent
DEFAULT_CONFIG = ROOT / "config.json"


def emoji_key(text):
    return "".join(character for character in text.strip()
                   if character not in ("\ufe0e", "\ufe0f", "\u200d"))


def emoji_asset(text):
    assets = {
        "\U0001f600": ROOT / "assets" / "smile.png",
        "smile": ROOT / "assets" / "smile.png",
        "\U0001f642": ROOT / "assets" / "slight-smile.png",
        "slight_smile": ROOT / "assets" / "slight-smile.png",
        "\u2764": ROOT / "assets" / "heart.png",
        "heart": ROOT / "assets" / "heart.png",
        "\u2b50": ROOT / "assets" / "star.png",
        "star": ROOT / "assets" / "star.png",
        "\U0001f44d": ROOT / "assets" / "thumbs-up.png",
        "\u2b06": ROOT / "assets" / "up.png",
        "up": ROOT / "assets" / "up.png",
        "\u2b07": ROOT / "assets" / "down.png",
        "down": ROOT / "assets" / "down.png",
        "\U0001f525": ROOT / "assets" / "fire.png",
        "fire": ROOT / "assets" / "fire.png",
        "\U0001f4b0": ROOT / "assets" / "money-bag.png",
        "money": ROOT / "assets" / "money-bag.png",
        "\U0001f911": ROOT / "assets" / "money-face.png",
        "money-face": ROOT / "assets" / "money-face.png",
        "\U0001f595": ROOT / "assets" / "fuck-off-smiley.png",
        "fuck-off-smiley": ROOT / "assets" / "fuck-off-smiley.png",
        "fuck-you-smiley": ROOT / "assets" / "fuck-you-smiley.png",
        "fuck-you-double-text": ROOT / "assets" / "fuck-you-double-text.png",
        "fuck-you-double": ROOT / "assets" / "fuck-you-double.png",
        "fuck-afd": ROOT / "assets" / "fuck-afd.png",
        "\U0001f92c": ROOT / "assets" / "angry.png",
        "\U0001f92e": ROOT / "assets" / "vomiting.png",
        "\U0001f44e": ROOT / "assets" / "thumbs-down.png",
    }
    path = assets.get(emoji_key(text))
    return path if path and path.exists() else None


def draw_emoji(draw, text, x, y, size):
    """Draw a few legible 64x64 emoji without relying on an emoji font."""
    emoji = emoji_key(text)
    cx, cy = x + size // 2, y + size // 2
    if emoji in ("\U0001f600", "\U0001f642"):
        face = (255, 210, 0)
        draw.ellipse((cx - size // 2, cy - size // 2, cx + size // 2, cy + size // 2), fill=face)
        eye_y = cy - size // 6
        for eye_x in (cx - size // 5, cx + size // 5):
            draw.ellipse((eye_x - 3, eye_y - 4, eye_x + 3, eye_y + 4), fill=(0, 0, 0))
        mouth = (cx - size // 4, cy + size // 8, cx + size // 4, cy + size // 3)
        draw.arc(mouth, 0 if emoji == "\U0001f642" else 15, 180, fill=(0, 0, 0), width=3)
        return True
    if "\u2764" in emoji:
        import math
        points = []
        for index in range(41):
            angle = 2 * math.pi * index / 40
            x = 16 * math.sin(angle) ** 3
            y = -(13 * math.cos(angle) - 5 * math.cos(2 * angle)
                  - 2 * math.cos(3 * angle) - math.cos(4 * angle))
            points.append((cx + round(x * size / 34), cy + round(y * size / 34)))
        draw.polygon(points, fill=(255, 30, 70))
        return True
    if emoji in ("\u2b06", "\u2b07"):
        direction = emoji == "\u2b06"
        if direction:
            points = [(cx, cy - size // 2), (cx - size // 4, cy - size // 5),
                      (cx - size // 10, cy - size // 5), (cx - size // 10, cy + size // 2),
                      (cx + size // 10, cy + size // 2), (cx + size // 10, cy - size // 5),
                      (cx + size // 4, cy - size // 5)]
        else:
            points = [(cx, cy + size // 2), (cx - size // 4, cy + size // 5),
                      (cx - size // 10, cy + size // 5), (cx - size // 10, cy - size // 2),
                      (cx + size // 10, cy - size // 2), (cx + size // 10, cy + size // 5),
                      (cx + size // 4, cy + size // 5)]
        draw.polygon(points, fill=(255, 255, 255))
        return True
    if emoji == "\U0001f525":
        outer = [(cx, cy - size // 2), (cx - size // 8, cy - size // 4),
                 (cx - size // 7, cy - size // 10), (cx - size // 3, cy - size // 4),
                 (cx - size // 3, cy + size // 4), (cx - size // 5, cy + size // 2),
                 (cx + size // 5, cy + size // 2), (cx + size // 3, cy + size // 4),
                 (cx + size // 3, cy - size // 12), (cx + size // 5, cy - size // 5),
                 (cx + size // 8, cy - size // 8)]
        inner = [(cx, cy - size // 5), (cx - size // 10, cy),
                 (cx - size // 6, cy + size // 5), (cx, cy + size // 3),
                 (cx + size // 6, cy + size // 5), (cx + size // 10, cy)]
        draw.polygon(outer, fill=(220, 35, 0))
        draw.polygon([(x, y + size // 20) for x, y in outer], fill=(255, 90, 0))
        draw.polygon(inner, fill=(255, 220, 0))
        return True
    if emoji == "\U0001f4b0":
        bag = (cx - size // 3, cy - size // 5, cx + size // 3, cy + size // 2)
        draw.rounded_rectangle(bag, radius=size // 8, fill=(35, 155, 65), outline=(10, 75, 35), width=2)
        draw.ellipse((cx - size // 5, cy - size // 3, cx + size // 5, cy), fill=(45, 180, 75), outline=(10, 75, 35), width=2)
        draw.polygon([(cx - size // 5, cy - size // 6), (cx, cy - size // 8),
                      (cx + size // 5, cy - size // 6), (cx + size // 8, cy + size // 12),
                      (cx - size // 8, cy + size // 12)], fill=(255, 210, 40))
        draw.line((cx - 2, cy - size // 10, cx - 2, cy + size // 3), fill=(255, 235, 80), width=2)
        draw.arc((cx - size // 8, cy, cx + size // 8, cy + size // 5), 90, 270, fill=(255, 235, 80), width=2)
        draw.arc((cx - size // 8, cy + size // 8, cx + size // 8, cy + size // 3), 270, 90, fill=(255, 235, 80), width=2)
        return True
    if emoji == "\U0001f911":
        draw.ellipse((cx - size // 2, cy - size // 2, cx + size // 2, cy + size // 2), fill=(255, 205, 0))
        draw.text((cx - size // 4, cy - size // 6), "$  $", fill=(0, 150, 70), font=ImageFont.load_default())
        draw.arc((cx - size // 4, cy, cx + size // 4, cy + size // 3), 0, 180, fill=(0, 120, 60), width=3)
        return True
    if emoji == "\u2b50":
        import math
        points = []
        for index in range(10):
            angle = -math.pi / 2 + index * math.pi / 5
            radius = size // 2 if index % 2 == 0 else size // 5
            points.append((cx + round(math.cos(angle) * radius), cy + round(math.sin(angle) * radius)))
        draw.polygon(points, fill=(255, 220, 0))
        return True
    if emoji == "\U0001f44d":
        draw.ellipse((cx - size // 4, cy - size // 3, cx + size // 4, cy + size // 3), fill=(255, 205, 120))
        draw.rounded_rectangle((cx - size // 5, cy - size // 2, cx + size // 6, cy), radius=5, fill=(255, 205, 120))
        return True
    return False


class MatrixDisplay:
    def __init__(self, config, dry_run=False):
        self.config = config
        self.lock = threading.Lock()
        self.state = {
            "text": "",
            "color": {"r": 255, "g": 255, "b": 255},
            "brightness": config["brightness"],
            "objects": [{"emoji": "\U0001f600", "x": 4, "y": 4, "size": 56}],
        }
        self.dry_run = dry_run
        self.canvas = None
        self.matrix = None

        if not dry_run:
            from rgbmatrix import RGBMatrix, RGBMatrixOptions

            options = RGBMatrixOptions()
            options.rows = config["rows"]
            options.cols = config["cols"]
            options.chain_length = config["chain_length"]
            options.parallel = config["parallel"]
            options.hardware_mapping = config["hardware_mapping"]
            options.gpio_slowdown = config["gpio_slowdown"]
            options.disable_hardware_pulsing = config.get("disable_hardware_pulsing", False)
            self.matrix = RGBMatrix(options=options)
            self.canvas = self.matrix.CreateFrameCanvas()

        self.render()

    def render(self):
        with self.lock:
            color = self.state["color"]
            brightness = self.state["brightness"] / 255
            rgb = tuple(round(color[channel] * brightness) for channel in ("r", "g", "b"))
            if self.dry_run:
                logging.info("render objects=%d color=%s brightness=%d", len(self.state["objects"]), rgb, self.state["brightness"])
                return

            image = Image.new("RGB", (self.matrix.width, self.matrix.height), rgb)
            draw = ImageDraw.Draw(image)
            for item in self.state["objects"]:
                asset = emoji_asset(item["emoji"])
                if asset:
                    emoji_image = Image.open(asset).convert("RGBA").resize(
                        (item["size"], item["size"]), Image.Resampling.LANCZOS)
                    image.paste(emoji_image, (item["x"], item["y"]), emoji_image)
                    continue
                if not draw_emoji(draw, item["emoji"], item["x"], item["y"], item["size"]):
                    try:
                        font = ImageFont.truetype(self.config["font"], self.config["font_size"])
                    except OSError:
                        logging.warning("Font not found at %s; using Pillow's built-in font", self.config["font"])
                        font = ImageFont.load_default()
                    draw.text((item["x"], item["y"]), item["emoji"], fill=(255, 255, 255), font=font)
            rotation = self.config.get("rotation", 0) % 360
            if rotation:
                image = image.rotate(rotation, expand=False)
            self.canvas.SetImage(image)
            self.canvas = self.matrix.SwapOnVSync(self.canvas)

    def update(self, payload):
        width = self.matrix.width if self.matrix else self.config["cols"] * self.config["chain_length"]
        height = self.matrix.height if self.matrix else self.config["rows"] * self.config["parallel"]
        with self.lock:
            if "text" in payload:
                self.state["text"] = str(payload["text"])[:120]
            if "objects" in payload:
                self.state["objects"] = [
                    {
                        "emoji": str(item.get("emoji", ""))[:32],
                        "x": max(0, min(width - 1, int(item.get("x", 0)))),
                        "y": max(0, min(height - 1, int(item.get("y", 0)))),
                        "size": max(8, min(64, int(item.get("size", 56)))),
                    }
                    for item in payload["objects"][:32]
                ]
            if "brightness" in payload:
                self.state["brightness"] = max(0, min(255, int(payload["brightness"])))
            if "color" in payload:
                color = payload["color"]
                self.state["color"] = {
                    channel: max(0, min(255, int(color[channel])))
                    for channel in ("r", "g", "b")
                }
        self.render()


def load_config(path):
    with path.open(encoding="utf-8") as stream:
        config = json.load(stream)
    required = ("host", "port", "cols", "rows", "chain_length", "parallel",
                "hardware_mapping", "gpio_slowdown", "brightness", "font", "font_size")
    missing = [key for key in required if key not in config]
    if missing:
        raise ValueError("Missing config keys: " + ", ".join(missing))
    return config


def make_handler(display):
    class Handler(BaseHTTPRequestHandler):
        def send_json(self, payload, status=200):
            body = json.dumps(payload).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if urlparse(self.path).path == "/api/state":
                self.send_json({"panelCount": 1, "panels": [display.state]})
                return
            if urlparse(self.path).path == "/fuck-off-smiley.png":
                body = (ROOT / "assets" / "fuck-off-smiley.png").read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            if urlparse(self.path).path == "/fuck-you-smiley.png":
                body = (ROOT / "assets" / "fuck-you-smiley.png").read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            image_name = urlparse(self.path).path.lstrip("/")
            if image_name in ("fuck-you-double-text.png", "fuck-you-double.png", "fuck-afd.png"):
                body = (ROOT / "assets" / image_name).read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            if urlparse(self.path).path in ("/", "/index.html"):
                body = (ROOT / "web" / "index.html").read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
                return
            self.send_error(404)

        def do_POST(self):
            if urlparse(self.path).path != "/api/display":
                self.send_error(404)
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                display.update(json.loads(self.rfile.read(length)))
            except (ValueError, KeyError, TypeError, json.JSONDecodeError) as error:
                self.send_json({"error": str(error)}, 400)
                return
            self.send_json({"ok": True, "panel": display.state})

        def log_message(self, format, *args):
            logging.info("%s - %s", self.address_string(), format % args)

    return Handler


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", type=Path, default=DEFAULT_CONFIG)
    parser.add_argument("--dry-run", action="store_true", help="run without LED hardware")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(message)s")
    config = load_config(args.config)
    display = MatrixDisplay(config, dry_run=args.dry_run)
    server = ThreadingHTTPServer((config["host"], config["port"]), make_handler(display))
    logging.info("LEDMatrix Pi server listening on %s:%d", config["host"], config["port"])
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
