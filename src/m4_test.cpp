#include <Arduino.h>
#include <Adafruit_Protomatter.h>
#include <ArduinoJson.h>
#include <WiFiNINA.h>

#include "generated_emoji_sprites.h"

// Matrix Portal M4 HUB75 pinout. One 64x64 panel.
const char WIFI_SSID[] = "YOUR_WIFI_SSID";
const char WIFI_PASSWORD[] = "YOUR_WIFI_PASSWORD";
const char AP_SSID[] = "LEDMatrix-M4";
const char AP_PASSWORD[] = "matrix-test";
uint8_t rgbPins[] = {7, 8, 9, 10, 11, 12};
uint8_t addressPins[] = {17, 18, 19, 20, 21};
Adafruit_Protomatter matrix(64, 4, 1, rgbPins, 5, addressPins, 14, 15, 16, false);
WiFiServer webServer(80);

constexpr int16_t WIDTH = 64, HEIGHT = 64;
constexpr uint8_t MAX_BRIGHTNESS = 24;
// Indexed-color asset masks are fast; set false to restore the detailed renderer.
constexpr bool USE_PRE_RENDERED_ASSETS = true;
constexpr bool USE_RGB565_ASSETS = true;
constexpr bool SCALE_RGB565_ASSETS = false;
struct DisplayState {
  uint8_t brightness = 24, bgR = 0, bgG = 0, bgB = 0;
  uint16_t rotation = 0;
  String text;
  JsonDocument objects;
} state;
bool redrawRequested = true;
bool wifiApMode = false;
uint8_t wifiRetryCount = 0;
unsigned long wifiLastAttempt = 0;
constexpr unsigned long WIFI_RETRY_INTERVAL_MS = 5000;
constexpr unsigned long WIFI_AP_RETRY_INTERVAL_MS = 60000;
constexpr uint8_t WIFI_RETRIES_BEFORE_AP = 2;
constexpr uint8_t AIRLIFT_RESET_PIN = 30;
bool wifiStationRequested = false;
unsigned long wifiLastApRetry = 0;
int lastWifiStatus = -1;
String activeHttpRequest;
unsigned long activeHttpStarted = 0;

uint16_t color(uint8_t r, uint8_t g, uint8_t b) {
  r = (uint16_t)r * state.brightness / 255;
  g = (uint16_t)g * state.brightness / 255;
  b = (uint16_t)b * state.brightness / 255;
  return matrix.color565(r, g, b);
}

void mapPoint(int16_t x, int16_t y, int16_t &px, int16_t &py) {
  switch (state.rotation) {
    case 90: px = WIDTH - 1 - y; py = x; break;
    case 180: px = WIDTH - 1 - x; py = HEIGHT - 1 - y; break;
    case 270: px = y; py = HEIGHT - 1 - x; break;
    default: px = x; py = y; break;
  }
}

void pixel(int16_t x, int16_t y, uint16_t c) {
  if (state.rotation == 0) {
    if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT) matrix.drawPixel(x, y, c);
    return;
  }
  int16_t px, py; mapPoint(x, y, px, py);
  if (px >= 0 && px < WIDTH && py >= 0 && py < HEIGHT) matrix.drawPixel(px, py, c);
}

void circle(int16_t cx, int16_t cy, int16_t radius, uint16_t c) {
  if (state.rotation == 0) {
    matrix.fillCircle(cx, cy, radius, c);
    return;
  }
  for (int16_t y = -radius; y <= radius; ++y)
    for (int16_t x = -radius; x <= radius; ++x)
      if (x * x + y * y <= radius * radius) pixel(cx + x, cy + y, c);
}

void bitmap(int16_t x, int16_t y, int16_t size, const uint8_t *rows, uint16_t c) {
  const int16_t scale = max(1, size / 8);
  for (uint8_t row = 0; row < 8; ++row)
    for (uint8_t column = 0; column < 8; ++column)
      if (rows[row] & (1 << (7 - column)))
        for (int16_t yy = 0; yy < scale; ++yy)
          for (int16_t xx = 0; xx < scale; ++xx)
            pixel(x + column * scale + xx, y + row * scale + yy, c);
}

void bitmap16(int16_t x, int16_t y, int16_t size, const uint16_t *rows, uint16_t c) {
  const int16_t scale = max(1, size / 16);
  const int16_t offset = (size - 16 * scale) / 2;
  for (uint8_t row = 0; row < 16; ++row)
    for (uint8_t column = 0; column < 16; ++column)
      if (rows[row] & (1 << (15 - column)))
        for (int16_t yy = 0; yy < scale; ++yy)
          for (int16_t xx = 0; xx < scale; ++xx)
            pixel(x + offset + column * scale + xx, y + offset + row * scale + yy, c);
}

bool drawRgb565Asset(const String &emoji, int16_t x, int16_t y, int16_t size) {
  if (!USE_RGB565_ASSETS) return false;
  const uint16_t *sprite = nullptr;
  if (emoji == "😀" || emoji == "smile") sprite = SPRITE_SMILE;
  else if (emoji == "🙂") sprite = SPRITE_SLIGHT_SMILE;
  else if (emoji == "❤️") sprite = SPRITE_HEART;
  else if (emoji == "⭐") sprite = SPRITE_STAR;
  else if (emoji == "👍") sprite = SPRITE_THUMBS_UP;
  else if (emoji == "⬆️" || emoji == "up" || emoji == "⬆") sprite = SPRITE_UP;
  else if (emoji == "⬇️" || emoji == "down" || emoji == "⬇") sprite = SPRITE_DOWN;
  else if (emoji == "🔥") sprite = SPRITE_FIRE;
  else if (emoji == "💰") sprite = SPRITE_MONEY;
  else if (emoji == "🤑") sprite = SPRITE_MONEY_FACE;
  else if (emoji == "🖕" || emoji == "fuck-off-smiley") sprite = SPRITE_FUCK_OFF_SMILEY;
  else if (emoji == "fuck-you-smiley") sprite = SPRITE_FUCK_YOU_SMILEY;
  else if (emoji == "fuck-you-double-text") sprite = SPRITE_FUCK_YOU_DOUBLE_TEXT;
  else if (emoji == "fuck-you-double") sprite = SPRITE_FUCK_YOU_DOUBLE;
  else if (emoji == "fuck-afd") sprite = SPRITE_FUCK_AFD;
  else if (emoji == "🤬") sprite = SPRITE_ANGRY;
  else if (emoji == "🤮") sprite = SPRITE_VOMITING;
  else if (emoji == "👎") sprite = SPRITE_THUMBS_DOWN;
  if (!sprite) return false;

  const bool arrow = emoji == "⬆️" || emoji == "⬇️" || emoji == "up" || emoji == "down" || emoji == "⬆" || emoji == "⬇";
  const int16_t xShift = arrow ? 2 : 0;
  const int16_t renderSize = SCALE_RGB565_ASSETS ? size : 48;
  const int16_t offset = SCALE_RGB565_ASSETS ? 0 : (size - 48) / 2;
  for (int16_t row = 0; row < renderSize; ++row)
    for (int16_t column = 0; column < renderSize; ++column) {
      const int16_t sourceRow = SCALE_RGB565_ASSETS ? row * 48 / renderSize : row;
      const int16_t sourceColumn = SCALE_RGB565_ASSETS ? column * 48 / renderSize : column;
      const uint16_t color565 = pgm_read_word(&sprite[sourceRow * 48 + sourceColumn]);
      if (color565) pixel(x + offset + xShift + column, y + offset + row, color565 == 1 ? color(0, 0, 0) : color565);
    }
  return true;
}

const uint8_t HEART_BITMAP[] = {0x66, 0xff, 0xff, 0xff, 0x7e, 0x3c, 0x18, 0x00};
const uint8_t STAR_BITMAP[] = {0x10, 0x10, 0x54, 0x38, 0xfe, 0x38, 0x54, 0x10};
const uint8_t UP_BITMAP[] = {0x10, 0x38, 0x7c, 0xfe, 0x10, 0x10, 0x10, 0x10};
const uint8_t DOWN_BITMAP[] = {0x10, 0x10, 0x10, 0x10, 0xfe, 0x7c, 0x38, 0x10};
const uint16_t HEART16_BITMAP[] = {
    0x0660, 0x1ff8, 0x3ffc, 0x7ffe, 0x7ffe, 0x7ffe, 0x3ffc, 0x1ff8,
    0x0ff0, 0x07e0, 0x03c0, 0x0180, 0x0000, 0x0000, 0x0000, 0x0000};
const uint16_t STAR16_BITMAP[] = {
    0x0180, 0x0180, 0x0990, 0x0ff0, 0x7ffe, 0x3ffc, 0x1ff8, 0x0ff0,
    0x0ff0, 0x1ff8, 0x3ffc, 0x7ffe, 0x0ff0, 0x0ff0, 0x1c38, 0x1818};
const uint16_t UP16_BITMAP[] = {
    0x0180, 0x03c0, 0x07e0, 0x0ff0, 0x1ff8, 0x3ffc, 0x7ffe, 0x0180,
    0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180};
const uint16_t DOWN16_BITMAP[] = {
    0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180, 0x0180,
    0x0180, 0x0180, 0x7ffe, 0x3ffc, 0x1ff8, 0x0ff0, 0x07e0, 0x03c0};
const uint16_t THUMBS16_BITMAP[] = {
    0x0180, 0x03c0, 0x07e0, 0x07e0, 0x0ff0, 0x0ff0, 0x1ff8, 0x1ff8,
    0x3ffc, 0x3ffc, 0x3ffc, 0x1ffc, 0x0ffc, 0x07f8, 0x03f0, 0x01e0};
const uint16_t SMILE_BITMAP[] = {
    0x03c0, 0x0ff0, 0x1ff8, 0x3ffc, 0x7ffe, 0x7ffe, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0x7ffe, 0x7ffe, 0x3ffc, 0x0ff0};
const uint16_t SMILE_EYES_BITMAP[] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0660, 0x0660, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000};
const uint16_t SMILE_MOUTH_BITMAP[] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x03c0, 0x0ff0, 0x07e0, 0x0000, 0x0000};
const uint16_t FIRE_BITMAP[] = {
    0x0000, 0x0180, 0x03c0, 0x03e0, 0x07f0, 0x0ff8, 0x1ffc, 0x3ffe,
    0x3ffe, 0x7fff, 0x7fff, 0xffff, 0xffff, 0x7ffe, 0x3ffc, 0x0ff0};
const uint16_t FIRE_CORE_BITMAP[] = {
    0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0180, 0x03c0, 0x07e0,
    0x0ff0, 0x0ff0, 0x07e0, 0x03c0, 0x0180, 0x0000, 0x0000, 0x0000};

bool drawPreRenderedAsset(const String &emoji, int16_t x, int16_t y, int16_t size) {
  if (!USE_PRE_RENDERED_ASSETS) return false;
  if (emoji == "😀" || emoji == "🙂" || emoji == "smile") {
    bitmap16(x, y, size, SMILE_BITMAP, color(255, 190, 0));
    bitmap16(x, y, size, SMILE_EYES_BITMAP, color(45, 25, 0));
    bitmap16(x, y, size, SMILE_MOUTH_BITMAP, color(45, 25, 0));
    return true;
  }
  if (emoji == "🔥" || emoji == "fire") {
    bitmap16(x, y, size, FIRE_BITMAP, color(210, 30, 0));
    bitmap16(x, y, size, FIRE_CORE_BITMAP, color(255, 190, 20));
    return true;
  }
  if (emoji == "❤️" || emoji == "heart") {
    bitmap16(x, y, size, HEART16_BITMAP, color(235, 20, 55));
    return true;
  }
  if (emoji == "⭐" || emoji == "star") {
    bitmap16(x, y, size, STAR16_BITMAP, color(255, 190, 0));
    return true;
  }
  if (emoji == "⬆️" || emoji == "up" || emoji == "⬆") {
    bitmap16(x, y, size, UP16_BITMAP, color(20, 115, 255));
    return true;
  }
  if (emoji == "⬇️" || emoji == "down" || emoji == "⬇") {
    bitmap16(x, y, size, DOWN16_BITMAP, color(20, 115, 255));
    return true;
  }
  if (emoji == "👍") {
    bitmap16(x, y, size, THUMBS16_BITMAP, color(255, 190, 105));
    return true;
  }
  return false;
}

void drawObject(JsonObject object) {
  String emoji = object["emoji"] | "";
  int16_t x = constrain((int)(object["x"] | 0), 0, WIDTH - 1);
  int16_t y = constrain((int)(object["y"] | 0), 0, HEIGHT - 1);
  int16_t size = constrain((int)(object["size"] | 16), 4, 64);
  int16_t cx = x + size / 2, cy = y + size / 2;
  if (drawRgb565Asset(emoji, x, y, size)) return;
  if (drawPreRenderedAsset(emoji, x, y, size)) return;
  if (emoji == "fire" || emoji == "🔥") {
    circle(cx, cy, size / 2, color(240, 45, 0));
    circle(cx, cy + size / 8, size / 3, color(255, 170, 0));
    circle(cx, cy + size / 5, size / 6, color(255, 245, 80));
  } else if (emoji == "heart" || emoji == "❤️") {
    circle(x + size / 3, y + size / 3, size / 3, color(255, 25, 70));
    circle(x + 2 * size / 3, y + size / 3, size / 3, color(255, 25, 70));
    for (int16_t yy = size / 3; yy < size; ++yy)
      for (int16_t xx = size / 6; xx < 5 * size / 6; ++xx)
        if (yy > size / 2 || abs(xx - size / 2) < yy / 2) pixel(x + xx, y + yy, color(255, 25, 70));
  } else if (emoji == "star" || emoji == "⭐") {
    for (int16_t yy = -size / 2; yy <= size / 2; ++yy)
      for (int16_t xx = -size / 2; xx <= size / 2; ++xx)
        if (abs(xx) < size / 8 || abs(yy) < size / 8 || abs(xx) + abs(yy) < size / 2) pixel(cx + xx, cy + yy, color(255, 220, 0));
  } else if (emoji == "up" || emoji == "⬆" || emoji == "⬆️") {
    for (int16_t yy = 0; yy < size; ++yy)
      for (int16_t xx = 0; xx < size; ++xx)
        if ((yy < size / 3 && abs(xx - size / 2) < yy + size / 8) ||
            (yy >= size / 3 && abs(xx - size / 2) < size / 8)) pixel(x + xx, y + yy, color(255, 255, 255));
  } else if (emoji == "down" || emoji == "⬇" || emoji == "⬇️") {
    for (int16_t yy = 0; yy < size; ++yy)
      for (int16_t xx = 0; xx < size; ++xx)
        if ((yy > 2 * size / 3 && abs(xx - size / 2) < size / 2 - (yy - 2 * size / 3)) ||
            (yy <= 2 * size / 3 && abs(xx - size / 2) < size / 8)) pixel(x + xx, y + yy, color(255, 255, 255));
  } else if (emoji == "money" || emoji == "💰") {
    circle(cx, cy + size / 8, size / 3, color(45, 180, 75));
    for (int16_t yy = size / 4; yy < 3 * size / 4; ++yy)
      for (int16_t xx = size / 4; xx < 3 * size / 4; ++xx) pixel(x + xx, y + yy, color(45, 180, 75));
    matrix.setTextColor(color(255, 235, 70)); matrix.setTextSize(1); matrix.setCursor(x + size / 2 - 3, y + size / 2 + 4); matrix.print('$');
  } else if (emoji == "money-face" || emoji == "🤑") {
    circle(cx, cy, size / 2, color(255, 205, 0));
    matrix.setTextColor(color(0, 150, 70)); matrix.setTextSize(1); matrix.setCursor(x + size / 5, y + size / 2); matrix.print('$');
    matrix.setCursor(x + 3 * size / 5, y + size / 2); matrix.print('$');
    for (int16_t xx = -size / 5; xx <= size / 5; ++xx) pixel(cx + xx, cy + size / 4, color(0, 120, 60));
  } else if (emoji == "👍") {
    circle(cx, cy, size / 3, color(255, 205, 120));
    for (int16_t yy = size / 4; yy < 3 * size / 4; ++yy)
      for (int16_t xx = size / 3; xx < 2 * size / 3; ++xx) pixel(x + xx, y + yy, color(255, 205, 120));
  } else {
    circle(cx, cy, size / 2, color(255, 205, 0));
    circle(cx - size / 5, cy - size / 8, max(1, size / 16), color(0, 0, 0));
    circle(cx + size / 5, cy - size / 8, max(1, size / 16), color(0, 0, 0));
    for (int16_t xx = -size / 5; xx <= size / 5; ++xx) pixel(cx + xx, cy + size / 5, color(0, 0, 0));
  }
}

void drawDisplay() {
  matrix.fillScreen(color(state.bgR, state.bgG, state.bgB));
  if (state.rotation == 0) {
    matrix.setTextWrap(false); matrix.setTextColor(color(255, 255, 255)); matrix.setCursor(2, 2); matrix.print(state.text);
  }
  for (JsonObject object : state.objects["objects"].as<JsonArray>()) drawObject(object);
  matrix.show(); redrawRequested = false;
}

bool applyCommand(const String &payload) {
  JsonDocument incoming;
  if (deserializeJson(incoming, payload)) return false;
  if (incoming["brightness"].is<int>()) state.brightness = constrain((int)incoming["brightness"], 0, MAX_BRIGHTNESS);
  if (incoming["rotation"].is<int>()) state.rotation = (((int)incoming["rotation"] % 360) + 360) % 360;
  if (incoming["text"].is<const char *>()) state.text = incoming["text"].as<String>().substring(0, 120);
  JsonObject bg = incoming["background"];
  if (bg.isNull()) bg = incoming["color"];
  if (!bg.isNull()) { state.bgR = constrain((int)(bg["r"] | 0), 0, 255); state.bgG = constrain((int)(bg["g"] | 0), 0, 255); state.bgB = constrain((int)(bg["b"] | 0), 0, 255); }
  if (incoming["objects"].is<JsonArray>()) state.objects["objects"] = incoming["objects"];
  redrawRequested = true; return true;
}

String stateJson() {
  JsonDocument output;
  output["width"] = WIDTH; output["height"] = HEIGHT; output["brightness"] = state.brightness; output["rotation"] = state.rotation; output["text"] = state.text; output["objects"] = state.objects["objects"];
  JsonObject bg = output["background"].to<JsonObject>(); bg["r"] = state.bgR; bg["g"] = state.bgG; bg["b"] = state.bgB;
  String result; serializeJson(output, result); return result;
}

void selectEmoji(const char *emoji) {
  JsonArray objects = state.objects["objects"].to<JsonArray>();
  JsonObject object = objects.size() ? objects[0].to<JsonObject>() : objects.add<JsonObject>();
  object["emoji"] = emoji; object["x"] = 4; object["y"] = 4; object["size"] = 56;
  redrawRequested = true;
}

const char UI[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<title>LED Matrix</title>
<style>
  :root { color-scheme: dark; font: 16px system-ui, sans-serif; background: #090b10; }
  * { box-sizing: border-box; }
  body { max-width: 34rem; margin: 0 auto; padding: 1.5rem 1rem 2rem; color: #f6f7fb; }
  h1 { margin: 0; font-size: 1.65rem; letter-spacing: -.03em; }
  .intro { color: #9ba3b5; margin: .35rem 0 1.4rem; }
  .stage { display: grid; place-items: center; min-height: 9rem; border: 1px solid #292e3b; border-radius: 1.25rem; background: radial-gradient(circle at 50% 35%, #252b3b, #11141c 70%); box-shadow: 0 .8rem 2rem #0008; }
  #preview { display: grid; place-items: center; font-size: 5rem; line-height: 1; filter: drop-shadow(0 .2rem .5rem #000); }
  #preview img { width: 5rem; height: 5rem; object-fit: contain; }
  h2 { margin: 1.6rem 0 .65rem; font-size: 1rem; color: #aeb7c9; }
  .picker { display: grid; grid-template-columns: repeat(5, 1fr); gap: .55rem; }
  .emoji { display: grid; place-items: center; aspect-ratio: 1; padding: .35rem; border: 1px solid #2d3443; border-radius: 1rem; background: #171b25; font-size: 2rem; cursor: pointer; touch-action: manipulation; transition: transform .12s, background .12s, border-color .12s; }
  .emoji img { width: 100%; height: 100%; object-fit: contain; }
  .emoji:active { transform: scale(.92); }
  .emoji.selected { border-color: #9b8cff; background: #302a56; box-shadow: 0 0 0 2px #9b8cff44; }
  .controls { display: grid; grid-template-columns: 1fr 1fr; gap: .8rem; margin-top: 1.4rem; }
  label { display: grid; gap: .35rem; color: #aeb7c9; font-size: .85rem; }
  input, select { width: 100%; font: inherit; color: inherit; background: #171b25; border: 1px solid #2d3443; border-radius: .65rem; padding: .55rem; }
  input[type=color] { min-height: 2.5rem; padding: .2rem; cursor: pointer; }
  input[type=range] { grid-column: 1 / -1; accent-color: #9b8cff; padding: 0; }
  #status { min-height: 1.5rem; margin-top: 1rem; color: #aeb7c9; text-align: center; }
  @media (min-width: 28rem) { .picker { grid-template-columns: repeat(5, 1fr); } .emoji { font-size: 1.8rem; } }
</style>
<body>
  <h1>Choose a picture</h1>
  <p class="intro">Tap an emoji to put it on the LED panel.</p>
  <div class="stage"><div id="preview" aria-live="polite">😀</div></div>
  <h2>Pictures</h2>
  <div class="picker" id="picker" role="group" aria-label="Pictures">
    <a class="emoji" data-emoji="😀" href="/api/select/smile" target="command" onclick="choose(this); return false" aria-label="Display 😀">😀</a>
    <a class="emoji" data-emoji="🙂" href="/api/select/slight-smile" target="command" onclick="choose(this); return false" aria-label="Display 🙂">🙂</a>
    <a class="emoji" data-emoji="❤️" href="/api/select/heart" target="command" onclick="choose(this); return false" aria-label="Display heart">❤️</a>
    <a class="emoji" data-emoji="⭐" href="/api/select/star" target="command" onclick="choose(this); return false" aria-label="Display star">⭐</a>
    <a class="emoji" data-emoji="👍" href="/api/select/thumbs-up" target="command" onclick="choose(this); return false" aria-label="Display thumbs up">👍</a>
    <a class="emoji" data-emoji="⬆️" href="/api/select/up" target="command" onclick="choose(this); return false" aria-label="Display up arrow">⬆️</a>
    <a class="emoji" data-emoji="⬇️" href="/api/select/down" target="command" onclick="choose(this); return false" aria-label="Display down arrow">⬇️</a>
    <a class="emoji" data-emoji="🔥" href="/api/select/fire" target="command" onclick="choose(this); return false" aria-label="Display fire">🔥</a>
    <a class="emoji" data-emoji="💰" href="/api/select/money" target="command" onclick="choose(this); return false" aria-label="Display money">💰</a>
    <a class="emoji" data-emoji="🤑" href="/api/select/money-face" target="command" onclick="choose(this); return false" aria-label="Display money face">🤑</a>
    <a class="emoji" data-emoji="fuck-off-smiley" href="/api/select/fuck-off-smiley" target="command" onclick="choose(this); return false" aria-label="Display custom smiley"><img src="/fuck-off-smiley.png" alt="Custom smiley"></a>
    <a class="emoji" data-emoji="fuck-you-smiley" href="/api/select/fuck-you-smiley" target="command" onclick="choose(this); return false" aria-label="Display second custom smiley"><img src="/fuck-you-smiley.png" alt="Second custom smiley"></a>
    <a class="emoji" data-emoji="🤬" href="/api/select/angry" target="command" onclick="choose(this); return false" aria-label="Display angry face">🤬</a>
    <a class="emoji" data-emoji="🤮" href="/api/select/vomiting" target="command" onclick="choose(this); return false" aria-label="Display vomiting face">🤮</a>
    <a class="emoji" data-emoji="👎" href="/api/select/thumbs-down" target="command" onclick="choose(this); return false" aria-label="Display thumbs down">👎</a>
    <a class="emoji" data-emoji="fuck-you-double-text" href="/api/select/fuck-you-double-text" target="command" onclick="choose(this); return false" aria-label="Display double text custom image"><img src="/fuck-you-double-text.png?v=3" alt="Double text custom image"></a>
    <a class="emoji" data-emoji="fuck-you-double" href="/api/select/fuck-you-double" target="command" onclick="choose(this); return false" aria-label="Display double custom image"><img src="/fuck-you-double.png?v=3" alt="Double custom image"></a>
    <a class="emoji" data-emoji="fuck-afd" href="/api/select/fuck-afd" target="command" onclick="choose(this); return false" aria-label="Display custom sign"><img src="/fuck-afd.png?v=2" alt="Custom sign"></a>
  </div>
  <iframe name="command" hidden title="Display command"></iframe>
  <div class="controls">
    <label>Background <input id="background" type="color" value="#000000"></label>
    <label>Rotation <select id="rotation"><option value="0">Normal</option><option value="180">Upside down</option></select></label>
  <label>Brightness <span id="brightnessValue">24</span><input id="brightness" type="range" min="0" max="255" value="24"></label>
  </div>
  <div id="status" role="status">Ready</div>
<script>
  const emojis = ['😀', '🙂', '❤️', '⭐', '👍', '⬆️', '⬇️', '🔥', '💰', '🤑'];
  const $ = id => document.getElementById(id);
  let selected = '😀';
  const customSmiley = 'fuck-off-smiley';
  const customYouSmiley = 'fuck-you-smiley';
  const customDoubleText = 'fuck-you-double-text';
  const customDouble = 'fuck-you-double';
  const customAfd = 'fuck-afd';
  const rgb = hex => ({r: parseInt(hex.slice(1, 3), 16), g: parseInt(hex.slice(3, 5), 16), b: parseInt(hex.slice(5, 7), 16)});
  const hex = color => '#' + ['r', 'g', 'b'].map(key => (color[key] || 0).toString(16).padStart(2, '0')).join('');
  function choose(button) {
    selected = button.dataset.emoji;
    showPreview(selected);
    $('status').textContent = selected + ' selected; sending...';
    document.querySelectorAll('.emoji').forEach(item => item.classList.toggle('selected', item === button));
    send();
  }
  function showPreview(value) {
    if (value === customSmiley || value === '🖕') $('preview').innerHTML = '<img src="/fuck-off-smiley.png" alt="Custom smiley">';
    else if (value === customYouSmiley) $('preview').innerHTML = '<img src="/fuck-you-smiley.png" alt="Second custom smiley">';
    else if (value === customDoubleText) $('preview').innerHTML = '<img src="/fuck-you-double-text.png?v=3" alt="Double text custom image">';
    else if (value === customDouble) $('preview').innerHTML = '<img src="/fuck-you-double.png?v=3" alt="Double custom image">';
    else if (value === customAfd) $('preview').innerHTML = '<img src="/fuck-afd.png?v=2" alt="Custom sign">';
    else $('preview').textContent = value;
  }
  window.choose = choose;
  function drawPicker() {
    // Emoji links work without JavaScript; state loading only marks the active one.
  }
  async function send() {
    $('status').textContent = 'Sending...';
    const body = {objects: [{emoji: selected, x: 4, y: 4, size: 56}], background: rgb($('background').value), brightness: +$('brightness').value, rotation: +$('rotation').value};
    try { const response = await fetch('/api/display', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify(body)}); if (!response.ok) throw new Error(await response.text()); $('status').textContent = selected + ' is on the panel'; }
    catch (error) { $('status').textContent = 'Could not update panel'; }
  }
  async function load() {
    drawPicker();
    try { const state = await (await fetch('/api/state')).json(); const item = (state.objects || [])[0]; if (item) { selected = item.emoji === '🖕' ? customSmiley : item.emoji; showPreview(selected); } $('brightness').value = state.brightness ?? 24; $('brightnessValue').textContent = $('brightness').value; $('rotation').value = state.rotation ?? 0; $('background').value = hex(state.background || {}); document.querySelectorAll('.emoji').forEach(button => button.classList.toggle('selected', button.dataset.emoji === selected)); }
    catch (error) { $('status').textContent = 'Panel is offline'; }
  }
  $('brightness').oninput = () => $('brightnessValue').textContent = $('brightness').value;
  $('brightness').onchange = send; $('background').onchange = send; $('rotation').onchange = send; load();
</script>
</body>
</html>
)HTML";

void sendResponse(WiFiClient &client, int code, const char *type, const String &body) {
  client.print("HTTP/1.1 "); client.print(code); client.println(code == 200 ? " OK" : " Error");
  client.print("Content-Type: "); client.println(type);
  client.print("Content-Length: "); client.println(body.length());
  client.println("Cache-Control: no-store");
  client.println("Access-Control-Allow-Origin: *"); client.println("Connection: close"); client.println();
  // Keep writes below the AirLift/WiFiNINA send buffer size.
  bool bodySent = true;
  for (size_t offset = 0; offset < body.length(); offset += 128) {
    const size_t chunk = min((size_t)128, body.length() - offset);
    if (client.write((const uint8_t *)body.c_str() + offset, chunk) != chunk) {
      bodySent = false;
      break;
    }
    delay(1);
  }
  Serial.print("HTTP response code=");
  Serial.print(code);
  Serial.print(" bytes=");
  Serial.print(body.length());
  Serial.print(" elapsed_ms=");
  Serial.print(millis() - activeHttpStarted);
  Serial.print(" request=");
  Serial.print(activeHttpRequest);
  Serial.print(" body_sent=");
  Serial.println(bodySent ? "yes" : "no");
}

void sendCustomSmiley(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: image/png");
  client.print("Content-Length: "); client.println(CUSTOM_SMILEY_PNG_SIZE);
  client.println("Cache-Control: max-age=3600");
  client.println("Connection: close"); client.println();
  uint8_t chunk[64];
  bool bodySent = true;
  for (size_t offset = 0; offset < CUSTOM_SMILEY_PNG_SIZE; offset += sizeof(chunk)) {
    const size_t length = min(sizeof(chunk), CUSTOM_SMILEY_PNG_SIZE - offset);
    for (size_t index = 0; index < length; ++index)
      chunk[index] = pgm_read_byte(&CUSTOM_SMILEY_PNG[offset + index]);
    if (client.write(chunk, length) != length) {
      bodySent = false;
      break;
    }
    delay(1);
  }
  Serial.print("HTTP image response bytes=");
  Serial.print(CUSTOM_SMILEY_PNG_SIZE);
  Serial.print(" body_sent=");
  Serial.println(bodySent ? "yes" : "no");
}

void sendCustomYouSmiley(WiFiClient &client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: image/png");
  client.print("Content-Length: "); client.println(CUSTOM_YOU_SMILEY_PNG_SIZE);
  client.println("Cache-Control: max-age=3600");
  client.println("Connection: close"); client.println();
  uint8_t chunk[64];
  bool bodySent = true;
  for (size_t offset = 0; offset < CUSTOM_YOU_SMILEY_PNG_SIZE; offset += sizeof(chunk)) {
    const size_t length = min(sizeof(chunk), CUSTOM_YOU_SMILEY_PNG_SIZE - offset);
    for (size_t index = 0; index < length; ++index)
      chunk[index] = pgm_read_byte(&CUSTOM_YOU_SMILEY_PNG[offset + index]);
    if (client.write(chunk, length) != length) {
      bodySent = false;
      break;
    }
    delay(1);
  }
  Serial.print("HTTP image response bytes=");
  Serial.print(CUSTOM_YOU_SMILEY_PNG_SIZE);
  Serial.print(" body_sent=");
  Serial.println(bodySent ? "yes" : "no");
}

void sendCustomAsset(WiFiClient &client, const uint8_t *data, size_t dataSize) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: image/png");
  client.print("Content-Length: "); client.println(dataSize);
  client.println("Cache-Control: max-age=3600");
  client.println("Connection: close"); client.println();
  uint8_t chunk[64];
  for (size_t offset = 0; offset < dataSize; offset += sizeof(chunk)) {
    const size_t length = min(sizeof(chunk), dataSize - offset);
    for (size_t index = 0; index < length; ++index)
      chunk[index] = pgm_read_byte(&data[offset + index]);
    if (client.write(chunk, length) != length) break;
    delay(1);
  }
}

void sendRedirect(WiFiClient &client) {
  client.println("HTTP/1.1 303 See Other");
  client.println("Location: /");
  client.println("Content-Length: 0");
  client.println("Connection: close");
  client.println();
}

void handleClient() {
  WiFiClient client = webServer.available(); if (!client) return; client.setTimeout(50);
  const unsigned long headerDeadline = millis() + 250;
  while (!client.available() && client.connected() && millis() < headerDeadline) delay(1);
  if (!client.available()) { client.stop(); return; }
  String request = client.readStringUntil('\n'), line; int length = 0;
  activeHttpRequest = request;
  activeHttpRequest.trim();
  activeHttpStarted = millis();
  Serial.print("HTTP request: ");
  Serial.println(activeHttpRequest);
  while (client.connected() && millis() < headerDeadline) {
    while (!client.available() && client.connected() && millis() < headerDeadline) delay(1);
    if (!client.available()) break;
    line = client.readStringUntil('\n');
    if (line.length() <= 2) break;
    if (line.startsWith("Content-Length:")) length = line.substring(15).toInt();
  }
  if (length < 0 || length > 4096) { client.stop(); return; }
  String body; if (length > 0) {
    body.reserve(length);
    const unsigned long deadline = millis() + 250;
    while ((int)body.length() < length && client.connected() && millis() < deadline) {
      if (client.available()) body += (char)client.read(); else delay(1);
    }
    if ((int)body.length() != length) { client.stop(); return; }
  }
  if (request.startsWith("GET /api/state")) sendResponse(client, 200, "application/json", stateJson());
  else if (request.startsWith("GET /api/select/")) {
    const String path = request.substring(16);
    if (path.startsWith("smile")) selectEmoji("😀");
    else if (path.startsWith("slight-smile")) selectEmoji("🙂");
    else if (path.startsWith("heart")) selectEmoji("❤️");
    else if (path.startsWith("star")) selectEmoji("⭐");
    else if (path.startsWith("thumbs-up")) selectEmoji("👍");
    else if (path.startsWith("up")) selectEmoji("⬆️");
    else if (path.startsWith("down")) selectEmoji("⬇️");
    else if (path.startsWith("fire")) selectEmoji("🔥");
    else if (path.startsWith("money-face")) selectEmoji("🤑");
    else if (path.startsWith("money")) selectEmoji("💰");
    else if (path.startsWith("fuck-off-smiley")) selectEmoji("fuck-off-smiley");
    else if (path.startsWith("fuck-you-smiley")) selectEmoji("fuck-you-smiley");
    else if (path.startsWith("fuck-you-double-text")) selectEmoji("fuck-you-double-text");
    else if (path.startsWith("fuck-you-double")) selectEmoji("fuck-you-double");
    else if (path.startsWith("fuck-afd")) selectEmoji("fuck-afd");
    else if (path.startsWith("angry")) selectEmoji("🤬");
    else if (path.startsWith("vomiting")) selectEmoji("🤮");
    else if (path.startsWith("thumbs-down")) selectEmoji("👎");
    sendResponse(client, 200, "application/json", stateJson());
  }
  else if (request.startsWith("POST /api/display")) { bool valid = applyCommand(body); sendResponse(client, valid ? 200 : 400, "application/json", valid ? stateJson() : "{\"error\":\"invalid command\"}"); }
  else if (request.startsWith("GET /api/reconnect")) { wifiStationRequested = true; sendResponse(client, 200, "application/json", "{\"ok\":true,\"message\":\"station reconnect scheduled\"}"); }
  else if (request.startsWith("GET /fuck-off-smiley.png")) sendCustomSmiley(client);
  else if (request.startsWith("GET /fuck-you-smiley.png")) sendCustomYouSmiley(client);
  else if (request.startsWith("GET /fuck-you-double-text.png")) sendCustomAsset(client, CUSTOM_DOUBLE_TEXT_PNG, CUSTOM_DOUBLE_TEXT_PNG_SIZE);
  else if (request.startsWith("GET /fuck-you-double.png")) sendCustomAsset(client, CUSTOM_DOUBLE_PNG, CUSTOM_DOUBLE_PNG_SIZE);
  else if (request.startsWith("GET /fuck-afd.png")) sendCustomAsset(client, CUSTOM_AFD_PNG, CUSTOM_AFD_PNG_SIZE);
  else if (request.startsWith("GET / ") || request.startsWith("GET /index.html")) sendResponse(client, 200, "text/html; charset=utf-8", UI);
  else sendResponse(client, 404, "application/json", "{\"error\":\"not found\"}");
  client.stop();
}

void resetAirLift();

void beginStationConnection() {
  if (wifiApMode) {
    WiFi.end();
    delay(100);
    resetAirLift();
  }
  wifiApMode = false;
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiLastAttempt = millis();
  Serial.print("M4 connecting to Wi-Fi: ");
  Serial.println(WIFI_SSID);
}

void resetAirLift() {
  Serial.println("M4 resetting AirLift Wi-Fi module");
  pinMode(AIRLIFT_RESET_PIN, OUTPUT);
  digitalWrite(AIRLIFT_RESET_PIN, LOW);
  delay(20);
  digitalWrite(AIRLIFT_RESET_PIN, HIGH);
  delay(750);
}

void beginFallbackAccessPoint() {
  wifiApMode = true;
  wifiLastApRetry = millis();
  WiFi.end();
  delay(100);
  const uint8_t status = WiFi.beginAP(AP_SSID, AP_PASSWORD);
  delay(250);
  Serial.print("M4 fallback AP status: ");
  Serial.println(status);
  Serial.print("Connect to Wi-Fi ");
  Serial.print(AP_SSID);
  Serial.print(" (password ");
  Serial.print(AP_PASSWORD);
  Serial.println(")");
  Serial.print("Then open http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");
}

void serviceWifi() {
  if (wifiApMode) {
    if (wifiStationRequested || millis() - wifiLastApRetry >= WIFI_AP_RETRY_INTERVAL_MS) {
      wifiStationRequested = false;
      wifiRetryCount = 0;
      beginStationConnection();
    }
    return;
  }
  const int status = WiFi.status();
  if (status != lastWifiStatus) {
    lastWifiStatus = status;
    Serial.print("M4 Wi-Fi status changed: ");
    Serial.print(status);
    if (status == WL_CONNECTED) {
      Serial.print(" connected ip=");
      Serial.print(WiFi.localIP());
    }
    Serial.println();
  }
  if (status == WL_CONNECTED) {
    wifiRetryCount = 0;
    return;
  }
  if (millis() - wifiLastAttempt < WIFI_RETRY_INTERVAL_MS) return;
  if (wifiRetryCount >= WIFI_RETRIES_BEFORE_AP) {
    Serial.print("M4 Wi-Fi unavailable, status: ");
    Serial.println(status);
    beginFallbackAccessPoint();
    return;
  }
  ++wifiRetryCount;
  Serial.print("M4 Wi-Fi retry ");
  Serial.println(wifiRetryCount);
  if (status == WL_NO_MODULE) resetAirLift();
  else WiFi.disconnect();
  beginStationConnection();
}

void setup() {
  Serial.begin(115200); delay(500); if (matrix.begin() != PROTOMATTER_OK) while (true) delay(1000);
  JsonObject first = state.objects["objects"].to<JsonArray>().add<JsonObject>(); first["emoji"] = "😀"; first["x"] = 4; first["y"] = 4; first["size"] = 56;
  WiFi.setTimeout(3000);
  beginStationConnection();
  webServer.begin(); drawDisplay(); Serial.println("M4 ready; BLE unavailable through AirLift");
}

void loop() {
  serviceWifi();
  handleClient();
  if (redrawRequested) {
    const unsigned long redrawStarted = millis();
    drawDisplay();
    Serial.print("Display redraw elapsed_ms=");
    Serial.println(millis() - redrawStarted);
  }
  delay(2);
}
