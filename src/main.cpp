#include <Arduino.h>
#include <Adafruit_Protomatter.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>

#include "config.h"
#include "generated_emoji_sprites.h"

namespace {

constexpr uint16_t DISPLAY_WIDTH = PANEL_WIDTH * PANEL_COUNT;
constexpr uint16_t DISPLAY_HEIGHT = PANEL_HEIGHT;
constexpr char BLE_SERVICE_UUID[] = "7c9e0001-8b1e-4c2b-9f77-3f8c5b6f0001";
constexpr char BLE_COMMAND_UUID[] = "7c9e0002-8b1e-4c2b-9f77-3f8c5b6f0001";
constexpr char BLE_STATUS_UUID[] = "7c9e0003-8b1e-4c2b-9f77-3f8c5b6f0001";

Adafruit_Protomatter matrix(
    DISPLAY_WIDTH, 4, 1, MATRIX_RGB_PINS, 5, MATRIX_ADDRESS_PINS,
    MATRIX_CLOCK_PIN, MATRIX_LATCH_PIN, MATRIX_OE_PIN, true);
WebServer server(80);
DNSServer dnsServer;
Preferences wifiPreferences;
NimBLECharacteristic *bleStatusCharacteristic = nullptr;

String displayText = "HELLO";
struct PanelState {
  String text = "HELLO";
  uint8_t red = 255;
  uint8_t green = 80;
  uint8_t blue = 0;
  uint8_t brightness = 64;
};

PanelState panels[PANEL_COUNT];
uint8_t backgroundRed = 0, backgroundGreen = 0, backgroundBlue = 0;
uint16_t displayRotation = 0;
JsonDocument displayObjects;
bool redrawRequested = true;
String pendingCommand;
bool setupMode = false;
bool wifiReconnectRequested = false;
bool wifiSetupRequested = false;
unsigned long wifiAttemptStarted = 0;
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;
constexpr uint8_t MAX_WIFI_PROFILES = 8;
constexpr char SETUP_SSID[] = "LEDMatrix-Setup";
constexpr char SETUP_PASSWORD[] = "matrix-setup";
uint8_t wifiProfileIndex = 0;
bool mdnsStarted = false;

uint16_t displayColor(const PanelState &panel) {
  const auto scale = [&panel](uint8_t value) -> uint8_t {
    return static_cast<uint8_t>((static_cast<uint16_t>(value) * panel.brightness) / 255);
  };
  return matrix.color565(scale(panel.red), scale(panel.green), scale(panel.blue));
}

uint16_t displayBackgroundColor() {
  const uint8_t brightness = panels[0].brightness;
  return matrix.color565(
      static_cast<uint8_t>((static_cast<uint16_t>(backgroundRed) * brightness) / 255),
      static_cast<uint8_t>((static_cast<uint16_t>(backgroundGreen) * brightness) / 255),
      static_cast<uint8_t>((static_cast<uint16_t>(backgroundBlue) * brightness) / 255));
}

uint16_t scaleSpriteColor(uint16_t color565) {
  const uint8_t brightness = panels[0].brightness;
  const uint8_t red = ((color565 >> 11) & 0x1f) * brightness / 255;
  const uint8_t green = ((color565 >> 5) & 0x3f) * brightness / 255;
  const uint8_t blue = (color565 & 0x1f) * brightness / 255;
  return matrix.color565(red << 3, green << 2, blue << 3);
}

void mapPoint(int16_t x, int16_t y, int16_t &px, int16_t &py) {
  switch (displayRotation) {
    case 90: px = DISPLAY_WIDTH - 1 - y; py = x; break;
    case 180: px = DISPLAY_WIDTH - 1 - x; py = DISPLAY_HEIGHT - 1 - y; break;
    case 270: px = y; py = DISPLAY_HEIGHT - 1 - x; break;
    default: px = x; py = y; break;
  }
}

void drawPixelRotated(int16_t x, int16_t y, uint16_t pixelColor) {
  int16_t px, py;
  mapPoint(x, y, px, py);
  if (px >= 0 && px < DISPLAY_WIDTH && py >= 0 && py < DISPLAY_HEIGHT)
    matrix.drawPixel(px, py, pixelColor);
}

bool drawRgb565Asset(const String &emoji, int16_t x, int16_t y, int16_t size) {
  const uint16_t *sprite = nullptr;
  if (emoji == "😀" || emoji == "smile") sprite = SPRITE_SMILE;
  else if (emoji == "🙂") sprite = SPRITE_SLIGHT_SMILE;
  else if (emoji == "❤️" || emoji == "heart") sprite = SPRITE_HEART;
  else if (emoji == "⭐" || emoji == "star") sprite = SPRITE_STAR;
  else if (emoji == "👍") sprite = SPRITE_THUMBS_UP;
  else if (emoji == "⬆️" || emoji == "up" || emoji == "⬆") sprite = SPRITE_UP;
  else if (emoji == "⬇️" || emoji == "down" || emoji == "⬇") sprite = SPRITE_DOWN;
  else if (emoji == "🔥" || emoji == "fire") sprite = SPRITE_FIRE;
  else if (emoji == "💰" || emoji == "money") sprite = SPRITE_MONEY;
   else if (emoji == "🤑" || emoji == "money-face") sprite = SPRITE_MONEY_FACE;
   else if (emoji == "🖕" || emoji == "fuck-off-smiley") sprite = SPRITE_FUCK_OFF_SMILEY;
   else if (emoji == "fuck-you-smiley") sprite = SPRITE_FUCK_YOU_SMILEY;
   else if (emoji == "fuck-you-double-text") sprite = SPRITE_FUCK_YOU_DOUBLE_TEXT;
   else if (emoji == "fuck-you-double") sprite = SPRITE_FUCK_YOU_DOUBLE;
   else if (emoji == "fuck-afd") sprite = SPRITE_FUCK_AFD;
   else if (emoji == "🤬") sprite = SPRITE_ANGRY;
   else if (emoji == "🤮") sprite = SPRITE_VOMITING;
   else if (emoji == "👎") sprite = SPRITE_THUMBS_DOWN;
  if (!sprite) return false;

  const bool arrow = emoji == "⬆️" || emoji == "⬇️" || emoji == "up" ||
                     emoji == "down" || emoji == "⬆" || emoji == "⬇";
  const int16_t offset = (size - 48) / 2;
  const int16_t xShift = arrow ? 2 : 0;
  for (int16_t row = 0; row < 48; ++row)
    for (int16_t column = 0; column < 48; ++column) {
      const uint16_t pixelColor = pgm_read_word(&sprite[row * 48 + column]);
      if (pixelColor) {
        // Zero is transparent; one is the intentionally opaque black artwork.
        drawPixelRotated(x + offset + xShift + column, y + offset + row,
                          pixelColor == 1 ? matrix.color565(0, 0, 0) : scaleSpriteColor(pixelColor));
      }
    }
  return true;
}

void drawObject(JsonObject object) {
  const String emoji = object["emoji"] | "";
  const int16_t x = constrain((int)(object["x"] | 0), 0, DISPLAY_WIDTH - 1);
  const int16_t y = constrain((int)(object["y"] | 0), 0, DISPLAY_HEIGHT - 1);
  const int16_t size = constrain((int)(object["size"] | 48), 8, 64);
  drawRgb565Asset(emoji, x, y, size);
}

bool applyCommand(const String &payload) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) return false;

  auto updatePanel = [](PanelState &panel, JsonObject source) {
    if (source["text"].is<const char *>()) panel.text = source["text"].as<String>();
    if (source["color"]["r"].is<uint8_t>()) panel.red = source["color"]["r"];
    if (source["color"]["g"].is<uint8_t>()) panel.green = source["color"]["g"];
    if (source["color"]["b"].is<uint8_t>()) panel.blue = source["color"]["b"];
    if (source["brightness"].is<uint8_t>()) panel.brightness = source["brightness"];
  };

  if (doc["rotation"].is<int>())
    displayRotation = (((int)doc["rotation"] % 360) + 360) % 360;
  JsonObject background = doc["background"];
  if (background.isNull()) background = doc["color"];
  if (!background.isNull()) {
    backgroundRed = constrain((int)(background["r"] | 0), 0, 255);
    backgroundGreen = constrain((int)(background["g"] | 0), 0, 255);
    backgroundBlue = constrain((int)(background["b"] | 0), 0, 255);
  }
  if (doc["objects"].is<JsonArray>())
    displayObjects["objects"] = doc["objects"];

  if (doc["panel"].is<uint8_t>()) {
    const uint8_t index = doc["panel"];
    if (index >= PANEL_COUNT) return false;
    updatePanel(panels[index], doc.as<JsonObject>());
  } else if (doc["panels"].is<JsonArray>()) {
    uint8_t index = 0;
    for (JsonObject source : doc["panels"].as<JsonArray>()) {
      if (index >= PANEL_COUNT) break;
      updatePanel(panels[index++], source);
    }
  } else {
    // A command without a target updates every module.
    for (PanelState &panel : panels) updatePanel(panel, doc.as<JsonObject>());
  }

  redrawRequested = true;
  return true;
}

String stateJson() {
  JsonDocument doc;
  JsonArray panelList = doc["panels"].to<JsonArray>();
  for (const PanelState &panel : panels) {
    JsonObject output = panelList.add<JsonObject>();
    output["text"] = panel.text;
    output["color"]["r"] = panel.red;
    output["color"]["g"] = panel.green;
    output["color"]["b"] = panel.blue;
    output["brightness"] = panel.brightness;
  }
  doc["width"] = DISPLAY_WIDTH;
  doc["height"] = DISPLAY_HEIGHT;
  doc["panelCount"] = PANEL_COUNT;
  doc["rotation"] = displayRotation;
  doc["background"]["r"] = backgroundRed;
  doc["background"]["g"] = backgroundGreen;
  doc["background"]["b"] = backgroundBlue;
  doc["objects"] = displayObjects["objects"];
  String result;
  serializeJson(doc, result);
  return result;
}

void allowBrowserClient() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
}

String profileKey(const char *kind, uint8_t index) {
  return String(kind) + String(index);
}

uint8_t wifiProfileCount() {
  return min((int)wifiPreferences.getUChar("count", 0), (int)MAX_WIFI_PROFILES);
}

String profileSsid(uint8_t index) {
  return wifiPreferences.getString(profileKey("ssid", index).c_str(), "");
}

String profilePassword(uint8_t index) {
  return wifiPreferences.getString(profileKey("pass", index).c_str(), "");
}

void migrateLegacyWifiProfile() {
  if (wifiPreferences.getUChar("count", 0) == 0 && wifiPreferences.isKey("ssid")) {
    wifiPreferences.putString("ssid0", wifiPreferences.getString("ssid", ""));
    wifiPreferences.putString("pass0", wifiPreferences.getString("password", ""));
    wifiPreferences.putUChar("count", 1);
  }
}

String savedSsid() {
  return wifiProfileCount() ? profileSsid(0) : String(WIFI_SSID);
}

String savedPassword() {
  return wifiProfileCount() ? profilePassword(0) : String(WIFI_PASSWORD);
}

String wifiStatusJson() {
  JsonDocument doc;
  doc["mode"] = setupMode ? "setup" : "station";
  doc["connected"] = WiFi.status() == WL_CONNECTED;
  doc["ssid"] = WiFi.status() == WL_CONNECTED
      ? WiFi.SSID()
      : (setupMode ? SETUP_SSID : savedSsid());
  doc["profileCount"] = wifiProfileCount();
  JsonArray profiles = doc["profiles"].to<JsonArray>();
  for (uint8_t index = 0; index < wifiProfileCount(); ++index)
    profiles.add(profileSsid(index));
  doc["ip"] = WiFi.localIP().toString();
  doc["hostname"] = "ledmatrix.local";
  String result;
  serializeJson(doc, result);
  return result;
}

void publishBleStatus() {
  if (bleStatusCharacteristic) {
    bleStatusCharacteristic->setValue(wifiStatusJson().c_str());
    bleStatusCharacteristic->notify();
  }
}

void sendSetupPage() {
  static const char page[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name="viewport" content="width=device-width,initial-scale=1">
<title>LEDMatrix Wi-Fi Setup</title><style>body{font:16px system-ui;max-width:30rem;margin:2rem auto;padding:0 1rem}input,select,button{box-sizing:border-box;font:inherit;padding:.7rem;width:100%;margin:.35rem 0 1rem}#status{min-height:1.5rem;color:#064}</style></head>
<body><h1>LEDMatrix Wi-Fi Setup</h1><p>Select a network to add or update. The controller tries saved profiles in order.</p>
<p id="profiles">Loading saved profiles...</p><button id="scan">Scan networks</button><select id="net"><option value="">Choose a network</option></select>
<label>Password</label><input id="password" type="password" autocomplete="new-password"><button id="save">Save profile and connect</button><p id="status"></p>
<script>
const $=id=>document.getElementById(id), status=text=>$('status').textContent=text;
async function loadProfiles(){try{const r=await fetch('/api/wifi/status'),s=await r.json();const box=$('profiles');box.replaceChildren();if(!s.profileCount){box.textContent='No saved profiles';return}s.profiles.forEach((ssid,i)=>{const row=document.createElement('div'),label=document.createElement('span'),button=document.createElement('button');label.textContent=(i+1)+'. '+ssid;button.textContent='Remove';button.onclick=async()=>{await fetch('/api/wifi/remove',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({index:i})});loadProfiles()};row.append(label,button);box.append(row)})}catch(e){$('profiles').textContent='Unable to load saved profiles'}}
async function scan(){status('Scanning...');try{const r=await fetch('/api/wifi/scan');const n=await r.json();$('net').replaceChildren(...n.networks.map(x=>new Option(x.ssid+' ('+x.rssi+' dBm)',x.ssid)));status(n.networks.length+' network(s) found')}catch(e){status('Scan failed: '+e)}}
async function save(){if(!$('net').value){status('Choose a network first');return}status('Saving...');try{const r=await fetch('/api/wifi/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({ssid:$('net').value,password:$('password').value})});if(!r.ok)throw Error(await r.text());status('Saved. Connecting; this page will reload when available.');setTimeout(()=>location.reload(),5000)}catch(e){status('Save failed: '+e)}}
$('scan').onclick=scan;$('save').onclick=save;loadProfiles();scan();
</script></body></html>)HTML";
  server.send(200, "text/html; charset=utf-8", page);
}

void handleWifiStatus() {
  server.send(200, "application/json", wifiStatusJson());
}

void handleWifiScan() {
  const int count = WiFi.scanNetworks(false, true);
  JsonDocument doc;
  JsonArray networks = doc["networks"].to<JsonArray>();
  for (int index = 0; index < count; ++index) {
    JsonObject network = networks.add<JsonObject>();
    network["ssid"] = WiFi.SSID(index);
    network["rssi"] = WiFi.RSSI(index);
    network["secure"] = WiFi.encryptionType(index) != WIFI_AUTH_OPEN;
  }
  WiFi.scanDelete();
  String result;
  serializeJson(doc, result);
  server.send(200, "application/json", result);
}

int saveWifiProfile(const String &ssid, const String &password) {
  if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63) {
    return -1;
  }
  uint8_t count = wifiProfileCount();
  int profile = -1;
  for (uint8_t index = 0; index < count; ++index) {
    if (profileSsid(index) == ssid) {
      profile = index;
      break;
    }
  }
  if (profile < 0) {
    if (count >= MAX_WIFI_PROFILES) {
      return 1;
    }
    profile = count++;
  }
  wifiPreferences.putString(profileKey("ssid", profile).c_str(), ssid);
  wifiPreferences.putString(profileKey("pass", profile).c_str(), password);
  wifiPreferences.putUChar("count", count);
  wifiProfileIndex = profile;
  return 0;
}

void handleWifiSave() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"invalid JSON\"}");
    return;
  }
  const String ssid = doc["ssid"] | "";
  const String password = doc["password"] | "";
  const int result = saveWifiProfile(ssid, password);
  if (result < 0) {
    server.send(400, "application/json", "{\"error\":\"invalid Wi-Fi credentials\"}");
    return;
  }
  if (result > 0) {
    server.send(409, "application/json", "{\"error\":\"Wi-Fi profile limit reached\"}");
    return;
  }
  server.send(200, "application/json", "{\"ok\":true}");
  // Reconnect from loop() so the HTTP response is completely delivered first.
  wifiReconnectRequested = true;
}

void handleWifiRemove() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"invalid JSON\"}");
    return;
  }
  const int remove = doc["index"] | -1;
  const uint8_t count = wifiProfileCount();
  if (remove < 0 || remove >= count) {
    server.send(400, "application/json", "{\"error\":\"invalid profile index\"}");
    return;
  }
  for (uint8_t index = remove; index + 1 < count; ++index) {
    wifiPreferences.putString(profileKey("ssid", index).c_str(), profileSsid(index + 1));
    wifiPreferences.putString(profileKey("pass", index).c_str(), profilePassword(index + 1));
  }
  wifiPreferences.remove(profileKey("ssid", count - 1).c_str());
  wifiPreferences.remove(profileKey("pass", count - 1).c_str());
  wifiPreferences.putUChar("count", count - 1);
  server.send(200, "application/json", "{\"ok\":true}");
}

void handleWifiForget() {
  wifiPreferences.clear();
  server.send(200, "application/json", "{\"ok\":true}");
  wifiSetupRequested = true;
}

void handleWifiSetup() {
  server.send(200, "application/json", "{\"ok\":true}");
  wifiSetupRequested = true;
}

bool handleBleWifiCommand(const String &payload) {
  JsonDocument doc;
  if (deserializeJson(doc, payload)) return false;
  JsonObject wifi = doc["wifi"];
  if (wifi.isNull()) return false;
  const String action = wifi["action"] | "status";
  if (action == "status") {
    publishBleStatus();
  } else if (action == "save") {
    const int result = saveWifiProfile(wifi["ssid"] | "", wifi["password"] | "");
    if (result == 0) wifiReconnectRequested = true;
    publishBleStatus();
  } else if (action == "setup") {
    wifiSetupRequested = true;
    publishBleStatus();
  } else {
    return false;
  }
  return true;
}

void drawDisplay() {
  matrix.fillScreen(displayBackgroundColor());
  for (uint8_t index = 0; index < PANEL_COUNT; ++index) {
    matrix.setTextWrap(false);
    matrix.setTextSize(1);
    matrix.setTextColor(displayColor(panels[index]));
    if (displayRotation == 0) {
      matrix.setCursor(index * PANEL_WIDTH, (DISPLAY_HEIGHT - 8) / 2);
      matrix.print(panels[index].text);
    }
  }
  for (JsonObject object : displayObjects["objects"].as<JsonArray>()) drawObject(object);
  matrix.show();
  redrawRequested = false;
}

void enterSetupMode() {
  setupMode = true;
  if (mdnsStarted) {
    MDNS.end();
    mdnsStarted = false;
  }
  WiFi.disconnect(true, true);
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(SETUP_SSID, SETUP_PASSWORD);
  dnsServer.start(53, "*", WiFi.softAPIP());
  Serial.print("Wi-Fi setup AP: ");
  Serial.println(WiFi.softAPIP());
  publishBleStatus();
}

void beginStationConnection() {
  setupMode = false;
  dnsServer.stop();
  WiFi.mode(WIFI_STA);
  const bool hasProfiles = wifiProfileCount() > 0;
  const String ssid = hasProfiles ? profileSsid(wifiProfileIndex) : String(WIFI_SSID);
  const String password = hasProfiles ? profilePassword(wifiProfileIndex) : String(WIFI_PASSWORD);
  WiFi.setHostname("ledmatrix");
  WiFi.begin(ssid.c_str(), password.c_str());
  wifiAttemptStarted = millis();
  Serial.print("Connecting to Wi-Fi: ");
  Serial.println(ssid);
  publishBleStatus();
}

void handleWifiConnection() {
  if (wifiSetupRequested) {
    wifiSetupRequested = false;
    enterSetupMode();
  }
  if (wifiReconnectRequested) {
    wifiReconnectRequested = false;
    beginStationConnection();
  }
  if (!setupMode && WiFi.status() != WL_CONNECTED && mdnsStarted) {
    MDNS.end();
    mdnsStarted = false;
  }
  if (!setupMode && WiFi.status() == WL_CONNECTED && !mdnsStarted) {
    if (MDNS.begin("ledmatrix")) {
      MDNS.addService("http", "tcp", 80);
      mdnsStarted = true;
      Serial.print("Wi-Fi address: ");
      Serial.println(WiFi.localIP());
      Serial.println("mDNS address: http://ledmatrix.local/");
      publishBleStatus();
    }
  }
  if (!setupMode && WiFi.status() != WL_CONNECTED &&
      millis() - wifiAttemptStarted >= WIFI_CONNECT_TIMEOUT_MS) {
    if (wifiProfileIndex + 1 < wifiProfileCount()) {
      ++wifiProfileIndex;
      beginStationConnection();
    } else {
      Serial.println("Wi-Fi profiles exhausted; starting setup portal");
      enterSetupMode();
    }
  }
}

void handleState() {
  allowBrowserClient();
  server.send(200, "application/json", stateJson());
}

void handleDisplay() {
  allowBrowserClient();
  if (server.method() != HTTP_POST) {
    server.send(405, "application/json", "{\"error\":\"POST required\"}");
    return;
  }
  if (!applyCommand(server.arg("plain"))) {
    server.send(400, "application/json", "{\"error\":\"invalid command\"}");
    return;
  }
  server.send(200, "application/json", stateJson());
}

class CommandCallbacks final : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &) override {
    pendingCommand = characteristic->getValue().c_str();
  }
};

void startBle() {
  NimBLEDevice::init("LED Matrix");
  NimBLEServer *bleServer = NimBLEDevice::createServer();
  NimBLEService *service = bleServer->createService(BLE_SERVICE_UUID);
  NimBLECharacteristic *command = service->createCharacteristic(
      BLE_COMMAND_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  bleStatusCharacteristic = service->createCharacteristic(
      BLE_STATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  bleStatusCharacteristic->setValue("{\"status\":\"starting\"}");
  command->setCallbacks(new CommandCallbacks());
  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  advertising->addServiceUUID(BLE_SERVICE_UUID);
  advertising->start();
}

void startWifi() {
  wifiPreferences.begin("ledmatrix", false);
  migrateLegacyWifiProfile();
  wifiProfileIndex = 0;
  publishBleStatus();
  beginStationConnection();
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/display", HTTP_POST, handleDisplay);
  server.on("/api/wifi/status", HTTP_GET, handleWifiStatus);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/api/wifi/save", HTTP_POST, handleWifiSave);
  server.on("/api/wifi/remove", HTTP_POST, handleWifiRemove);
  server.on("/api/wifi/forget", HTTP_POST, handleWifiForget);
  server.on("/api/wifi/setup", HTTP_POST, handleWifiSetup);
  server.on("/api/display", HTTP_OPTIONS, [] {
    allowBrowserClient();
    server.send(204);
  });
  server.on("/setup", HTTP_GET, sendSetupPage);
  server.on("/fuck-off-smiley.png", HTTP_GET, [] {
    File image = LittleFS.open("/fuck-off-smiley.png", "r");
    if (!image) {
      server.send(404, "text/plain", "Image unavailable");
      return;
    }
    server.streamFile(image, "image/png");
    image.close();
  });
   server.on("/fuck-you-smiley.png", HTTP_GET, [] {
    File image = LittleFS.open("/fuck-you-smiley.png", "r");
    if (!image) {
      server.send(404, "text/plain", "Image unavailable");
      return;
    }
    server.streamFile(image, "image/png");
     image.close();
   });
   server.on("/fuck-you-double-text.png", HTTP_GET, [] {
     File image = LittleFS.open("/fuck-you-double-text.png", "r");
     if (!image) { server.send(404, "text/plain", "Image unavailable"); return; }
     server.streamFile(image, "image/png"); image.close();
   });
   server.on("/fuck-you-double.png", HTTP_GET, [] {
     File image = LittleFS.open("/fuck-you-double.png", "r");
     if (!image) { server.send(404, "text/plain", "Image unavailable"); return; }
     server.streamFile(image, "image/png"); image.close();
   });
   server.on("/fuck-afd.png", HTTP_GET, [] {
     File image = LittleFS.open("/fuck-afd.png", "r");
     if (!image) { server.send(404, "text/plain", "Image unavailable"); return; }
     server.streamFile(image, "image/png"); image.close();
   });
  server.onNotFound([] {
    if (setupMode) {
      server.sendHeader("Location", "/setup", true);
      server.send(302, "text/plain", "Redirecting to Wi-Fi setup");
    } else {
      server.send(404, "application/json", "{\"error\":\"not found\"}");
    }
  });
  server.on("/", HTTP_GET, [] {
    if (setupMode) {
      sendSetupPage();
      return;
    }
    File page = LittleFS.open("/index.html", "r");
    if (!page) {
      server.send(500, "text/plain", "Web UI unavailable");
      return;
    }
    server.streamFile(page, "text/html; charset=utf-8");
    page.close();
  });
  server.begin();
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  ProtomatterStatus status = matrix.begin();
  if (status != PROTOMATTER_OK) {
    Serial.printf("Protomatter initialization failed: %d\n", status);
    while (true) delay(1000);
  }

  startBle();
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS initialization failed; web UI unavailable");
  }
  JsonObject defaultObject = displayObjects["objects"].to<JsonArray>().add<JsonObject>();
  defaultObject["emoji"] = "😀";
  defaultObject["x"] = 4;
  defaultObject["y"] = 4;
  defaultObject["size"] = 56;
  startWifi();
  drawDisplay();
  Serial.println("LED Matrix controller ready");
}

void loop() {
  server.handleClient();
  if (setupMode) dnsServer.processNextRequest();
  handleWifiConnection();

  if (!pendingCommand.isEmpty()) {
    String command = pendingCommand;
    pendingCommand.clear();
    if (!handleBleWifiCommand(command)) applyCommand(command);
  }

  if (redrawRequested) drawDisplay();
  delay(2);
}
