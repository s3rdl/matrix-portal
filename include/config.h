#pragma once

// Start with one panel. Set this to 4 after the four-panel chain is wired.
#define PANEL_COUNT 4
#define PANEL_WIDTH 64
#define PANEL_HEIGHT 64

// Wi-Fi credentials. Move these to a local, untracked file before deployment.
#define WIFI_SSID "YOUR_WIFI_SSID"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Matrix Portal S3 HUB75 pinout.
// The 64x64 panel requires five address lines, including E.
static uint8_t MATRIX_RGB_PINS[] = {42, 41, 40, 38, 39, 37};
static uint8_t MATRIX_ADDRESS_PINS[] = {45, 36, 48, 35, 21};
static constexpr uint8_t MATRIX_CLOCK_PIN = 2;
static constexpr uint8_t MATRIX_LATCH_PIN = 47;
static constexpr uint8_t MATRIX_OE_PIN = 14;
