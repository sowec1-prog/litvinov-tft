#include <Arduino.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Update.h>
#include <WiFiClientSecure.h>
#include <mbedtls/sha256.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <SPI.h>
#include <time.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Adafruit_NeoPixel.h>
#include <XPT2046_Touchscreen.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <ArduinoJson.h>
#include "preview_icons.h"
#include "hockey_team_logos.h"
#include "football_team_logos.h"
#include "competition_logos.h"
#include "remote_ota_ca.h"
#include "config.h"

#ifndef TFT_SETUP_AP_SSID
#define TFT_SETUP_AP_SSID "Litvinov-TFT-Setup"
#endif
#ifndef TFT_SETUP_AP_PASSWORD
#define TFT_SETUP_AP_PASSWORD "litvinov"
#endif
#ifndef TFT_RESET_WIFI_ON_BOOT
#define TFT_RESET_WIFI_ON_BOOT 0
#endif
#ifndef TFT_FOOTBALL_TEAM_CODE
#define TFT_FOOTBALL_TEAM_CODE "SPA"
#endif
#ifndef TFT_FOOTBALL_TEAM_LABEL
#define TFT_FOOTBALL_TEAM_LABEL "SPARTA"
#endif
#ifndef TFT_RGB_LED_PIN
#define TFT_RGB_LED_PIN 48
#endif
#ifndef TFT_RGB_LED_COUNT
#define TFT_RGB_LED_COUNT 1
#endif
#ifndef TFT_REMOTE_UPDATE_CHANNEL
#define TFT_REMOTE_UPDATE_CHANNEL "sparta"
#endif
#ifndef TFT_FIRMWARE_VERSION
#define TFT_FIRMWARE_VERSION "1.3.0"
#endif
#ifndef TFT_ANALOG_CLOCK_SCREENSAVER
#define TFT_ANALOG_CLOCK_SCREENSAVER 0
#endif

static constexpr uint32_t POLL_INTERVAL_MS = 30000;
static constexpr uint32_t IDLE_CLOCK_TIMEOUT_MS = 5UL * 60UL * 1000UL;
// V hodinovém spořiči je podsvit hlavní odběr; live obrazovka se vždy vrátí na uživatelský jas.
static constexpr uint8_t CLOCK_SAVER_BRIGHTNESS = 8;
// TFT backlight is ramped after boot so it does not overlap a full inrush with Wi-Fi startup.
static constexpr uint32_t BOOT_BACKLIGHT_RAMP_MS = 1600;
// HTTPS OTA stačí ověřit při startu a pak jednou za 24 hodin.
static constexpr uint32_t REMOTE_UPDATE_CHECK_MS = 24UL * 60UL * 60UL * 1000UL;
static constexpr uint16_t TOP_BAR_H = 28;
static constexpr uint16_t BACKGROUND = ILI9341_BLACK;
static constexpr uint16_t PANEL = 0x10C4;  // preview CSS #101823
static constexpr uint16_t LINE = 0x29A5;
static constexpr uint16_t LIME = 0xD7E0;
static constexpr uint16_t LIT_RED = 0xC800;
static constexpr bool TOUCH_DIAGNOSTIC = false;
static constexpr uint32_t WIFI_SETUP_DELAY_MS = 120000;
static constexpr byte DNS_PORT = 53;

WebServer wifiSetupServer(80);
DNSServer wifiSetupDns;
Preferences wifiPreferences;
bool wifiSetupPortalActive = false;
bool wifiSetupStarted = false;
uint32_t wifiConnectStarted = 0;
bool forceWifiSetup = false;
String savedWifiSsid;
String savedWifiPassword;

static_assert(TFT_SCK_PIN >= 0 && TFT_MOSI_PIN >= 0 && TFT_MISO_PIN >= 0, "Set TFT SPI pins in local config.h.");
static_assert(TFT_CS_PIN >= 0 && TFT_DC_PIN >= 0 && TFT_RST_PIN >= 0, "Set TFT control pins in local config.h.");

Adafruit_ILI9341 tft(TFT_CS_PIN, TFT_DC_PIN, TFT_RST_PIN);
XPT2046_Touchscreen touch(TOUCH_CS_PIN, TOUCH_IRQ_PIN);
U8G2_FOR_ADAFRUIT_GFX u8g2;
// RGB test confirmed the TFT WS2812B data line is GPIO48.
Adafruit_NeoPixel goalLed(TFT_RGB_LED_COUNT, TFT_RGB_LED_PIN, NEO_GRB + NEO_KHZ800);

enum Screen : uint8_t { HOME, HOCKEY, FOOTBALL };
Screen screen = HOME;

struct MatchData {
  String state = "scheduled";
  String homeCode = "LIT";
  String awayCode = "KOM";
  String homeLogo = "litvinov";
  String awayLogo = "kometa";
  String eventId;
  String audioCue;
  String goalSide;
  String liveEvent;
  String liveEventCode;
  String penaltyIndicator;
  String penaltyText;
  String statusText = "25.10. 17:30";
  String competition;
  int scoreHome = 0;
  int scoreAway = 0;
  int tablePosition = 0;
  bool cached = false;
};
MatchData match;

uint32_t lastPoll = 0;
uint32_t lastRemoteUpdateCheck = 0;
uint32_t lastTouch = 0;
uint32_t lastUserActivity = 0;
uint32_t lastClockRedraw = 0;
bool analogClockActive = false;
bool analogClockFrameDrawn = false;
bool analogClockHandsDrawn = false;
int previousHourHandX = 0, previousHourHandY = 0;
int previousMinuteHandX = 0, previousMinuteHandY = 0;
int previousSecondHandX = 0, previousSecondHandY = 0;
bool gameFrameDrawn = false;
bool needsRedraw = true;
bool otaReady = false;
bool otaConfigReported = false;
uint8_t brightness = 82;
bool bootBacklightRampActive = false;
uint32_t bootBacklightRampStarted = 0;
int debugRawX = 0, debugRawY = 0, debugMapX = 0, debugMapY = 0;
bool debugTouchSeen = false;
String previousGoalEventId;
String lastTopBarClock;
uint32_t lastTopBarClockCheck = 0;
bool goalBaselineKnown = false;
bool goalAnimationActive = false;
bool goalByHome = true;
uint32_t goalAnimationStarted = 0;
uint32_t lastGoalFrame = 0;
bool goalLedFlashActive = false;
uint32_t goalLedFlashStarted = 0;
uint32_t goalLedFlashColor = 0;
bool footballScoreBaselineKnown = false;
int previousFootballFavoriteScore = 0;
int previousFootballOpponentScore = 0;

int clampMap(int value, int fromLow, int fromHigh, int toLow, int toHigh) {
  const long mapped = map(value, fromLow, fromHigh, toLow, toHigh);
  return constrain(mapped, min(toLow, toHigh), max(toLow, toHigh));
}

bool otaConfigured() {
  return strlen(OTA_PASSWORD) >= 12 && strncmp(OTA_PASSWORD, "CHANGE_ME_", 10) != 0;
}

void writeBacklight(uint8_t percent) {
  ledcWrite(0, map(constrain(percent, 0, 100), 0, 100, 0, 255));
}

void setBrightness(uint8_t percent) {
  brightness = constrain(percent, 20, 100);
  if (!analogClockActive && !bootBacklightRampActive) writeBacklight(brightness);
}

void updateBootBacklightRamp() {
  if (!bootBacklightRampActive) return;
  const uint32_t elapsed = millis() - bootBacklightRampStarted;
  if (elapsed >= BOOT_BACKLIGHT_RAMP_MS) {
    bootBacklightRampActive = false;
    writeBacklight(brightness);
    return;
  }
  writeBacklight(map(elapsed, 0, BOOT_BACKLIGHT_RAMP_MS, 0, brightness));
}

void startOta() {
  if (!otaConfigured()) {
    if (!otaConfigReported) {
      Serial.println("OTA disabled: configure local OTA_PASSWORD");
      otaConfigReported = true;
    }
    return;
  }
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.begin();
  otaReady = true;
  Serial.printf("OTA ready as %s\n", OTA_HOSTNAME);
}

void startWifi() {
  wifiPreferences.begin("wifi", false);
#if TFT_RESET_WIFI_ON_BOOT
  if (!wifiPreferences.getBool("reset_done", false)) {
    wifiPreferences.clear();
    wifiPreferences.putBool("reset_done", true);
    wifiPreferences.putBool("setup_mode", true);
  }
#endif
  forceWifiSetup = wifiPreferences.getBool("setup_mode", false);
  savedWifiSsid = wifiPreferences.getString("ssid", "");
  savedWifiPassword = wifiPreferences.getString("pass", "");
  wifiPreferences.end();
  WiFi.mode(WIFI_STA);
  // Modem sleep keeps the association but reduces idle radio consumption.
  WiFi.setSleep(true);
  if (forceWifiSetup) {
    wifiConnectStarted = millis() - WIFI_SETUP_DELAY_MS;
    return;
  }
  if (!savedWifiSsid.isEmpty()) WiFi.begin(savedWifiSsid.c_str(), savedWifiPassword.c_str());
  else WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  wifiConnectStarted = millis();
}

void showWifiSetupPage() {
  const int count = WiFi.scanNetworks();
  String options;
  for (int i = 0; i < count; ++i) {
    const String ssid = WiFi.SSID(i);
    if (!ssid.isEmpty()) options += "<option value='" + ssid + "'>";
  }
  WiFi.scanDelete();
  const String page = "<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
    "<title>Litvinov TFT Wi-Fi</title><style>body{font-family:sans-serif;max-width:32rem;margin:2rem auto;padding:0 1rem}input,button{box-sizing:border-box;width:100%;font-size:1rem;padding:.7rem;margin:.4rem 0}button{background:#bd0000;color:white;border:0;border-radius:.3rem}</style>"
    "<h2>Litvinov TFT - Wi-Fi</h2><p>Vyber nebo napiš 2,4 GHz Wi-Fi a heslo. Po uložení se TFT připojí a hotspot zmizí.</p>"
    "<form method=post action=/save><label>Wi-Fi síť</label><input name=ssid list=ssids required><datalist id=ssids>" + options + "</datalist>"
    "<label>Heslo Wi-Fi</label><input name=pass type=password required><button type=submit>Uložit a připojit</button></form>";
  wifiSetupServer.send(200, "text/html; charset=utf-8", page);
}

void saveWifiSetup() {
  const String ssid = wifiSetupServer.arg("ssid");
  const String password = wifiSetupServer.arg("pass");
  if (ssid.isEmpty() || password.isEmpty()) {
    wifiSetupServer.send(400, "text/plain; charset=utf-8", "Chybí název sítě nebo heslo.");
    return;
  }
  wifiPreferences.begin("wifi", false);
  wifiPreferences.putString("ssid", ssid);
  wifiPreferences.putString("pass", password);
  wifiPreferences.putBool("setup_mode", false);
  wifiPreferences.end();
  forceWifiSetup = false;
  savedWifiSsid = ssid;
  savedWifiPassword = password;
  wifiSetupServer.send(200, "text/html; charset=utf-8", "<meta name=viewport content='width=device-width'><h2>Uloženo</h2><p>TFT se připojuje. Hotspot za chvíli zmizí.</p>");
  WiFi.begin(savedWifiSsid.c_str(), savedWifiPassword.c_str());
  wifiConnectStarted = millis();
}

void startWifiSetupPortal() {
  if (wifiSetupPortalActive) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(TFT_SETUP_AP_SSID, TFT_SETUP_AP_PASSWORD);
  wifiSetupDns.start(DNS_PORT, "*", WiFi.softAPIP());
  wifiSetupServer.on("/", HTTP_GET, showWifiSetupPage);
  wifiSetupServer.on("/save", HTTP_POST, saveWifiSetup);
  wifiSetupServer.on("/generate_204", HTTP_GET, showWifiSetupPage);
  wifiSetupServer.onNotFound(showWifiSetupPage);
  wifiSetupServer.begin();
  wifiSetupPortalActive = true;
  match.statusText = "WIFI: 192.168.4.1";
  needsRedraw = true;
  Serial.printf("Wi-Fi setup portal: %s / 192.168.4.1\n", TFT_SETUP_AP_SSID);
}

void stopWifiSetupPortal() {
  if (!wifiSetupPortalActive) return;
  wifiSetupDns.stop();
  wifiSetupServer.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  wifiSetupPortalActive = false;
  needsRedraw = true;
}

void handleWifiSetup() {
  if (wifiSetupPortalActive) {
    wifiSetupDns.processNextRequest();
    wifiSetupServer.handleClient();
  }
  if (WiFi.status() == WL_CONNECTED) {
    stopWifiSetupPortal();
    return;
  }
  if (millis() - wifiConnectStarted >= WIFI_SETUP_DELAY_MS) startWifiSetupPortal();
}

void drawAsciiCentered(const String &text, int y, uint8_t size, uint16_t color) {
  tft.setTextSize(size);
  tft.setTextColor(color);
  const int x = max(0, (320 - int(text.length()) * 6 * size) / 2);
  tft.setCursor(x, y);
  tft.print(text);
}

void drawUtf8Centered(const char *text, int baseline, uint16_t color) {
  u8g2.setFont(u8g2_font_unifont_t_latin);
  u8g2.setFontMode(1);
  u8g2.setForegroundColor(color);
  const int x = max(0, (320 - u8g2.getUTF8Width(text)) / 2);
  u8g2.drawUTF8(x, baseline, text);
}

String clockText() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 5)) return "--.-- --:--";
  char buffer[18];
  strftime(buffer, sizeof(buffer), "%d.%m. %H:%M", &timeinfo);
  return String(buffer);
}

void drawWifiIcon();

bool liveMatchScreenActive() {
  return screen != HOME && match.state == "live";
}

bool shouldShowAnalogClock() {
#if TFT_ANALOG_CLOCK_SCREENSAVER
  return !wifiSetupPortalActive && !liveMatchScreenActive() &&
      millis() - lastUserActivity >= IDLE_CLOCK_TIMEOUT_MS;
#else
  return false;
#endif
}

void drawAnalogClock() {
  constexpr int cx = 160;
  constexpr int cy = 130;
  constexpr int radius = 88;
  if (!analogClockFrameDrawn) {
    tft.fillScreen(BACKGROUND);
    tft.setTextSize(2);
    tft.setTextColor(LIT_RED);
    tft.setCursor(12, 12);
    tft.print("SPARTA PRAHA");
    drawWifiIcon();
    tft.drawCircle(cx, cy, radius, ILI9341_LIGHTGREY);
    tft.drawCircle(cx, cy, radius - 1, ILI9341_LIGHTGREY);
    analogClockFrameDrawn = true;
  }

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 5)) {
    if (!analogClockHandsDrawn) drawAsciiCentered("CEKAM NA CAS", 108, 2, ILI9341_LIGHTGREY);
    return;
  }

  // Odmazávají se pouze minulé ručičky; černý celý displej se už každou sekundu neposílá.
  if (analogClockHandsDrawn) {
    tft.drawLine(cx, cy, previousHourHandX, previousHourHandY, BACKGROUND);
    tft.drawLine(cx, cy, previousMinuteHandX, previousMinuteHandY, BACKGROUND);
    tft.drawLine(cx, cy, previousSecondHandX, previousSecondHandY, BACKGROUND);
    tft.fillCircle(cx, cy, 5, BACKGROUND);
  } else {
    char date[22];
    strftime(date, sizeof(date), "%d.%m.%Y", &timeinfo);
    drawAsciiCentered(date, 224, 1, ILI9341_LIGHTGREY);
  }

  const float secondAngle = (timeinfo.tm_sec * 6 - 90) * DEG_TO_RAD;
  const float minuteAngle = ((timeinfo.tm_min + timeinfo.tm_sec / 60.0f) * 6 - 90) * DEG_TO_RAD;
  const float hourAngle = (((timeinfo.tm_hour % 12) + timeinfo.tm_min / 60.0f) * 30 - 90) * DEG_TO_RAD;
  previousHourHandX = cx + cosf(hourAngle) * 43;
  previousHourHandY = cy + sinf(hourAngle) * 43;
  previousMinuteHandX = cx + cosf(minuteAngle) * 66;
  previousMinuteHandY = cy + sinf(minuteAngle) * 66;
  previousSecondHandX = cx + cosf(secondAngle) * 72;
  previousSecondHandY = cy + sinf(secondAngle) * 72;
  tft.drawLine(cx, cy, previousHourHandX, previousHourHandY, LIT_RED);
  tft.drawLine(cx, cy, previousMinuteHandX, previousMinuteHandY, ILI9341_WHITE);
  tft.drawLine(cx, cy, previousSecondHandX, previousSecondHandY, LIME);
  tft.fillCircle(cx, cy, 5, LIT_RED);
  analogClockHandsDrawn = true;
}

void drawWifiIcon() {
  if (WiFi.status() != WL_CONNECTED) {
    tft.drawLine(299, 7, 313, 21, ILI9341_RED);
    tft.drawLine(313, 7, 299, 21, ILI9341_RED);
    return;
  }
  const int rssi = WiFi.RSSI();
  uint8_t bars = 0;
  if (rssi >= -60) bars = 4;
  else if (rssi >= -67) bars = 3;
  else if (rssi >= -75) bars = 2;
  else if (rssi >= -82) bars = 1;
  for (uint8_t i = 0; i < 4; ++i) {
    const int x = 295 + i * 5;
    const int h = 4 + i * 4;
    const uint16_t color = i < bars ? ILI9341_GREEN : ILI9341_DARKGREY;
    tft.fillRect(x, 22 - h, 4, h, color);
  }
}

void drawTopBar() {
  tft.fillRect(0, 0, 320, TOP_BAR_H, 0x1082);
  tft.drawFastHLine(0, TOP_BAR_H - 1, 320, LINE);
  tft.setTextSize(1);
  tft.setTextColor(ILI9341_WHITE);
  tft.setCursor(5, 10);
  if (screen == FOOTBALL) {
    tft.print(TFT_FOOTBALL_TEAM_LABEL);
    tft.print(" ");
    tft.print(match.tablePosition > 0 ? String("#") + match.tablePosition : "-");
  } else {
    tft.print("LITVINOV");
    if (match.tablePosition > 0) {
      tft.print(" ");
      tft.print(match.tablePosition);
    }
  }
  const String currentClock = clockText();
  tft.setCursor(121, 10);
  tft.print(currentClock);
  lastTopBarClock = currentClock;
  drawWifiIcon();
}

void drawPuck(int x, int y) {
  tft.fillRoundRect(x, y, 68, 20, 8, ILI9341_DARKGREY);
  tft.drawRoundRect(x, y, 68, 20, 8, ILI9341_LIGHTGREY);
  tft.drawFastHLine(x + 8, y + 5, 52, 0x4208);
}

void drawFootball(int cx, int cy) {
  tft.fillCircle(cx, cy, 29, ILI9341_WHITE);
  tft.drawCircle(cx, cy, 29, ILI9341_LIGHTGREY);
  tft.fillTriangle(cx, cy - 10, cx - 10, cy - 3, cx + 10, cy - 3, ILI9341_BLACK);
  tft.drawLine(cx - 10, cy - 3, cx - 19, cy - 16, ILI9341_BLACK);
  tft.drawLine(cx + 10, cy - 3, cx + 19, cy - 16, ILI9341_BLACK);
  tft.drawLine(cx - 10, cy + 7, cx - 18, cy + 19, ILI9341_BLACK);
  tft.drawLine(cx + 10, cy + 7, cx + 18, cy + 19, ILI9341_BLACK);
}

void drawHome() {
  tft.fillScreen(BACKGROUND);
  drawTopBar();
  drawAsciiCentered("VYBER SPORT", 40, 2, ILI9341_WHITE);

  tft.fillRoundRect(12, 62, 140, 112, 10, PANEL);
  tft.fillRoundRect(168, 62, 140, 112, 10, PANEL);
  tft.drawRoundRect(12, 62, 140, 112, 10, LINE);
  tft.drawRoundRect(168, 62, 140, 112, 10, LINE);
  // These are RGB565 exports of the SVG artwork used by ui-preview.html.
  tft.drawRGBBitmap(50, 76, kPreviewSoccer, 64, 64);
  tft.drawRGBBitmap(200, 64, kPreviewPuck, 76, 76);
  drawAsciiCentered("", 0, 1, ILI9341_WHITE);
  tft.setTextSize(2);
  tft.setTextColor(LIT_RED);
  tft.setCursor(42, 142); tft.print("FOTBAL");
  tft.setCursor(44, 160); tft.print(TFT_FOOTBALL_TEAM_LABEL);
  tft.setTextColor(LIME);
  tft.setCursor(192, 142); tft.print("HOKEJ");
  tft.setCursor(177, 160); tft.print("LITVINOV");

  if (TOUCH_DIAGNOSTIC) {
    tft.fillRoundRect(6, 187, 308, 49, 5, PANEL);
    tft.drawRoundRect(6, 187, 308, 49, 5, ILI9341_ORANGE);
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_ORANGE);
    tft.setCursor(13, 193); tft.print("DIAGNOSTIKA: klepni na obe karty");
    tft.setTextColor(ILI9341_WHITE);
    if (debugTouchSeen) {
      tft.setCursor(13, 207); tft.printf("RAW %d,%d  MAP %d,%d", debugRawX, debugRawY, debugMapX, debugMapY);
      tft.setCursor(13, 221); tft.print(debugMapX < 160 ? "MAP zóna: LEVA" : "MAP zóna: PRAVA");
    } else {
      tft.setCursor(13, 211); tft.print("cekam na prvni dotyk...");
    }
  } else {
    tft.setTextSize(1);
    tft.setTextColor(ILI9341_LIGHTGREY);
    tft.setCursor(20, 202); tft.print("JAS");
    tft.drawRect(50, 201, 210, 10, LINE);
    const int fill = map(brightness, 20, 100, 0, 208);
    tft.fillRect(51, 202, fill, 8, LIME);
    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(270, 202); tft.printf("%u %%", brightness);
  }
}

void drawTeamBadge(int cx, int cy, const String &code, uint16_t color) {
  tft.drawCircle(cx, cy, 30, color);
  tft.drawCircle(cx, cy, 29, color);
  tft.setTextSize(2);
  tft.setTextColor(color);
  const int x = cx - int(code.length()) * 6;
  tft.setCursor(x, cy - 7);
  tft.print(code);
}

const uint16_t* sportLogoForCode(String code) {
  code.toUpperCase();
  if (screen == HOCKEY) {
    if (code == "CEB" || code == "MOT") return kHockeyLogoCEB;
    if (code == "HKR" || code == "MHK") return kHockeyLogoHKR;
    if (code == "KLA") return kHockeyLogoKLA;
    if (code == "KOM") return kHockeyLogoKOM;
    if (code == "KVA") return kHockeyLogoKVA;
    if (code == "LIB") return kHockeyLogoLIB;
    if (code == "LIT") return kHockeyLogoLIT;
    if (code == "MBL") return kHockeyLogoMBL;
    if (code == "OLO") return kHockeyLogoOLO;
    if (code == "PCE") return kHockeyLogoPCE;
    if (code == "PLZ") return kHockeyLogoPLZ;
    if (code == "SPA") return kHockeyLogoSPA;
    if (code == "TRI") return kHockeyLogoTRI;
    if (code == "VIT") return kHockeyLogoVIT;
  } else if (screen == FOOTBALL) {
    if (code == "ART") return kFootballLogoART;
    if (code == "BOH") return kFootballLogoBOH;
    if (code == "FCB") return kFootballLogoFCB;
    if (code == "FCS" || code == "SLO") return kFootballLogoFCS;
    if (code == "FCZ") return kFootballLogoFCZ;
    if (code == "FKJ") return kFootballLogoFKJ;
    if (code == "FKP" || code == "PCE") return kFootballLogoFKP;
    if (code == "HKR") return kFootballLogoHKR;
    if (code == "LIB") return kFootballLogoLIB;
    if (code == "MBL") return kFootballLogoMBL;
    if (code == "PLZ") return kFootballLogoPLZ;
    if (code == "SIG") return kFootballLogoSIG;
    if (code == "SKS") return kFootballLogoSKS;
    if (code == "SPA") return kFootballLogoSPA;
    if (code == "TEP") return kFootballLogoTEP;
    if (code == "ZBR") return kFootballLogoZBR;
  }
  return nullptr;
}

void drawTeamLogo(int x, int y, const String &, const String &code, uint16_t color) {
  const uint16_t *bitmap = sportLogoForCode(code);
  if (bitmap) tft.drawRGBBitmap(x, y, bitmap, 60, 60);
  else drawTeamBadge(x + 30, y + 30, code, color);
}

void drawGoalAnimation() {
  if (!goalAnimationActive) return;
  constexpr uint32_t PASS_MS = 2800;
  const uint32_t elapsed = min<uint32_t>(millis() - goalAnimationStarted, PASS_MS);
  const int puckX = goalByHome
      ? map(elapsed, 0, PASS_MS, -76, 320)
      : map(elapsed, 0, PASS_MS, 320, -76);
  // Home team is displayed left: its puck travels left-to-right; away reverses.
  const int goalTextX = goalByHome ? puckX - 80 : puckX + 80;
  tft.drawRGBBitmap(puckX, 84, kPreviewPuck, 76, 76);
  tft.setTextColor(goalByHome ? LIME : ILI9341_WHITE);
  tft.setTextSize(3);
  tft.setCursor(goalTextX, 108);
  tft.print("GOOL");
}

void drawGame() {
  // Kreslit celý rám pouze při přechodu na sportovní obrazovku.
  // Při běžné obnově API se mění jen obsahová plocha, takže TFT nebliká.
  if (!gameFrameDrawn) {
    tft.fillScreen(BACKGROUND);
    tft.fillRoundRect(5, 33, 58, 22, 5, PANEL);
    tft.setTextSize(1); tft.setTextColor(ILI9341_WHITE); tft.setCursor(11, 41); tft.print("< ZPET");
    gameFrameDrawn = true;
  }
  // Lišta se obnovuje i při částečném překreslení: po načtení API tak ukáže správný klub i tabulku.
  drawTopBar();
  tft.fillRect(0, 56, 320, 184, BACKGROUND);

  const bool live = match.state == "live";
  const bool finished = match.state == "finished";
  if (finished) drawUtf8Centered("KONEC ZÁPASU", 77, ILI9341_LIGHTGREY);
  else if (!live) {
    if (screen == FOOTBALL && match.competition.indexOf("MOL Cup") >= 0) {
      tft.drawRGBBitmap(124, 61, kLogoMolCup, 72, 32);
    } else if (screen == FOOTBALL && match.competition.indexOf("Chance") >= 0) {
      tft.drawRGBBitmap(102, 61, kLogoChanceLiga, 116, 32);
    } else {
      drawUtf8Centered("NÁSLEDUJE", 77, ILI9341_LIGHTGREY);
    }
  }
  drawTeamLogo(45, 92, match.homeLogo, match.homeCode, screen == HOCKEY ? LIME : LIT_RED);
  drawTeamLogo(215, 92, match.awayLogo, match.awayCode, ILI9341_WHITE);
  if (live || finished) {
    drawAsciiCentered(String(match.scoreHome) + ":" + String(match.scoreAway), 103, 4, ILI9341_YELLOW);
  } else {
    drawAsciiCentered("VS", 113, 3, ILI9341_LIGHTGREY);
  }
  tft.drawFastHLine(14, 194, 292, LINE);
  const bool periodBreak = match.liveEvent.indexOf("PRESTAVKA") >= 0 || match.liveEvent.indexOf("KOMERCNI") >= 0;
  const String bottomText = live
      ? (periodBreak ? match.liveEvent : (!match.penaltyText.isEmpty() ? match.penaltyText : (!match.liveEvent.isEmpty() ? match.liveEvent : match.penaltyIndicator)))
      : match.statusText;
  const uint16_t bottomColor = live && !bottomText.isEmpty()
      ? (match.liveEventCode == "LIT" || bottomText.indexOf("LIT") >= 0 ? LIME : ILI9341_ORANGE)
      : ILI9341_CYAN;
  if (!bottomText.isEmpty()) drawAsciiCentered(bottomText, 202, 2, bottomColor);
  drawGoalAnimation();
  // Any tap outside < ZPET still forces an immediate API refresh; no on-screen hint.
}

void drawWifiSetupInstructions() {
  tft.fillScreen(BACKGROUND);
  tft.fillRect(0, 0, 320, TOP_BAR_H, 0x1082);
  drawUtf8Centered("NASTAVENÍ WI-FI", 55, ILI9341_YELLOW);
  drawUtf8Centered("1. Připoj se k síti", 88, ILI9341_LIGHTGREY);
  drawAsciiCentered(TFT_SETUP_AP_SSID, 105, 2, ILI9341_WHITE);
  drawUtf8Centered("Heslo: litvinov", 128, ILI9341_CYAN);
  drawUtf8Centered("2. Otevři v mobilu", 165, ILI9341_LIGHTGREY);
  drawAsciiCentered("192.168.4.1", 184, 2, ILI9341_WHITE);
  drawUtf8Centered("a zadej novou Wi-Fi", 216, ILI9341_LIGHTGREY);
}

void drawScreen() {
  if (wifiSetupPortalActive) drawWifiSetupInstructions();
  else if (analogClockActive) drawAnalogClock();
  else if (screen == HOME) drawHome();
  else drawGame();
  needsRedraw = false;
}

void clearGoalLed() {
  goalLed.clear();
  goalLed.show();
}

void startGoalLedFlash(bool favoriteScored) {
  // Four short flashes: green for Litvínov/selected football club, red for conceded goal.
  goalLedFlashColor = favoriteScored ? goalLed.Color(0, 180, 0) : goalLed.Color(210, 0, 0);
  goalLedFlashStarted = millis();
  goalLedFlashActive = true;
}

void updateGoalLed() {
  if (!goalLedFlashActive) return;
  constexpr uint32_t STEP_MS = 180;
  constexpr uint8_t STEPS = 8;  // on/off × four flashes
  const uint32_t elapsed = millis() - goalLedFlashStarted;
  if (elapsed >= STEP_MS * STEPS) {
    goalLedFlashActive = false;
    clearGoalLed();
    return;
  }
  goalLed.setPixelColor(0, ((elapsed / STEP_MS) % 2 == 0) ? goalLedFlashColor : 0);
  goalLed.show();
}

void handleFootballScoreCue() {
  if (screen != FOOTBALL) return;
  const bool favoriteIsHome = match.homeCode == TFT_FOOTBALL_TEAM_CODE;
  const bool favoriteIsAway = match.awayCode == TFT_FOOTBALL_TEAM_CODE;
  if (!favoriteIsHome && !favoriteIsAway) return;
  const int favoriteScore = favoriteIsHome ? match.scoreHome : match.scoreAway;
  const int opponentScore = favoriteIsHome ? match.scoreAway : match.scoreHome;
  if (footballScoreBaselineKnown) {
    if (favoriteScore > previousFootballFavoriteScore) startGoalLedFlash(true);
    else if (opponentScore > previousFootballOpponentScore) startGoalLedFlash(false);
  }
  previousFootballFavoriteScore = favoriteScore;
  previousFootballOpponentScore = opponentScore;
  footballScoreBaselineKnown = true;
}

void handleGoalCue() {
  if (screen != HOCKEY) return;
  if (!goalBaselineKnown) {
    previousGoalEventId = match.eventId;
    goalBaselineKnown = true;
    return;
  }
  if (match.eventId.isEmpty() || match.eventId == previousGoalEventId) return;
  previousGoalEventId = match.eventId;
  if (match.audioCue != "lit_goal" && match.audioCue != "conceded_goal") return;
  startGoalLedFlash(match.audioCue == "lit_goal");
  goalByHome = match.goalSide != "away";
  goalAnimationStarted = millis();
  goalAnimationActive = true;
  lastGoalFrame = 0;
}

void checkRemoteFirmwareUpdate() {
  if (wifiSetupPortalActive) return;
  const String manifestUrl = String(TFT_API_BASE_URL) + "/api/v1/tft/firmware?channel=" + TFT_REMOTE_UPDATE_CHANNEL + "&current=" + TFT_FIRMWARE_VERSION;
  WiFiClientSecure manifestClient;
  manifestClient.setCACert(kRemoteOtaRootCa);
  HTTPClient manifestHttp;
  manifestHttp.setTimeout(12000);
  if (!manifestHttp.begin(manifestClient, manifestUrl)) return;
  const int manifestCode = manifestHttp.GET();
  if (manifestCode != HTTP_CODE_OK) { manifestHttp.end(); return; }
  JsonDocument manifest;
  const DeserializationError manifestError = deserializeJson(manifest, manifestHttp.getString());
  manifestHttp.end();
  if (manifestError || !(manifest["available"] | false)) return;
  const String version = manifest["version"] | "";
  const String binaryUrl = manifest["url"] | "";
  const String expectedSha256 = manifest["sha256"] | "";
  if (version.isEmpty() || binaryUrl.indexOf("https://") != 0 || expectedSha256.length() != 64) return;

  WiFiClientSecure firmwareClient;
  firmwareClient.setCACert(kRemoteOtaRootCa);
  HTTPClient firmwareHttp;
  firmwareHttp.setTimeout(20000);
  if (!firmwareHttp.begin(firmwareClient, binaryUrl)) return;
  const int firmwareCode = firmwareHttp.GET();
  const int contentLength = firmwareHttp.getSize();
  if (firmwareCode != HTTP_CODE_OK || contentLength <= 0 || !Update.begin(contentLength, U_FLASH)) {
    firmwareHttp.end();
    return;
  }

  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  mbedtls_sha256_starts_ret(&sha, 0);
  WiFiClient *stream = firmwareHttp.getStreamPtr();
  uint8_t buffer[1024];
  int remaining = contentLength;
  bool writeFailed = false;
  while (remaining > 0) {
    const size_t received = stream->readBytes(buffer, min<int>(sizeof(buffer), remaining));
    if (received == 0) { writeFailed = true; break; }
    mbedtls_sha256_update_ret(&sha, buffer, received);
    if (Update.write(buffer, received) != received) { writeFailed = true; break; }
    remaining -= received;
  }
  uint8_t digest[32];
  mbedtls_sha256_finish_ret(&sha, digest);
  mbedtls_sha256_free(&sha);
  firmwareHttp.end();
  char actualSha256[65];
  for (uint8_t i = 0; i < sizeof(digest); ++i) snprintf(actualSha256 + i * 2, 3, "%02x", digest[i]);
  if (writeFailed || remaining != 0 || expectedSha256 != actualSha256 || !Update.end(true)) {
    Update.abort();
    Serial.println("Remote firmware update rejected");
    return;
  }
  Serial.printf("Installing remote firmware %s for %s\n", version.c_str(), TFT_REMOTE_UPDATE_CHANNEL);
  delay(250);
  ESP.restart();
}

String visibleMatchKey() {
  return match.state + "|" + match.competition + "|" + match.homeCode + "|" + match.awayCode + "|" +
      match.homeLogo + "|" + match.awayLogo + "|" + match.statusText + "|" + match.liveEvent + "|" +
      match.liveEventCode + "|" + match.penaltyIndicator + "|" + match.penaltyText + "|" + String(match.scoreHome) + "|" + String(match.scoreAway) + "|" + String(match.tablePosition);
}

void fetchSelectedSport() {
  const char *sport = screen == FOOTBALL ? "football" : "hockey";
  HTTPClient http;
  String url = String(TFT_API_BASE_URL) + "/api/v1/sport/" + sport;
  if (screen == FOOTBALL) url += "?team=" TFT_FOOTBALL_TEAM_CODE;
  http.setTimeout(8000);
  if (!http.begin(url)) { match.statusText = "CHYBA API"; needsRedraw = true; return; }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) { match.statusText = "API HTTP " + String(code); http.end(); needsRedraw = true; return; }
  const String responseBody = http.getString();
  http.end();
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, responseBody);
  if (error) { match.statusText = "JSON " + String(error.c_str()); needsRedraw = true; return; }
  const String beforeRender = visibleMatchKey();
  match.state = doc["state"] | "scheduled";
  match.competition = doc["competition"] | "";
  match.homeCode = doc["home"]["code"] | (screen == HOCKEY ? "LIT" : TFT_FOOTBALL_TEAM_CODE);
  match.awayCode = doc["away"]["code"] | "?";
  match.homeLogo = doc["home"]["logo"] | match.homeCode;
  match.awayLogo = doc["away"]["logo"] | match.awayCode;
  match.eventId = doc["event_id"] | "";
  match.audioCue = doc["audio_cue"] | "";
  match.goalSide = doc["goal_side"] | "";
  match.liveEvent = doc["live_event"] | "";
  match.liveEventCode = doc["live_event_code"] | "";
  match.penaltyIndicator = doc["penalty_indicator"] | "";
  match.penaltyText = doc["penalty_text"] | "";
  match.statusText = doc["status_text"] | "";
  if (match.statusText.isEmpty()) match.statusText = "TERMIN CEKA";
  match.scoreHome = doc["score_home"] | 0;
  match.scoreAway = doc["score_away"] | 0;
  match.tablePosition = doc["table_position"] | 0;
  match.cached = doc["cached"] | false;
  handleGoalCue();
  handleFootballScoreCue();
  Serial.printf("%s: %s %s-%s %s\n", sport, match.state.c_str(), match.homeCode.c_str(), match.awayCode.c_str(), match.statusText.c_str());
  needsRedraw = needsRedraw || beforeRender != visibleMatchKey();
}

void openScreen(Screen next) {
  screen = next;
  gameFrameDrawn = false;
  footballScoreBaselineKnown = false;
  if (screen == HOCKEY) {
    goalBaselineKnown = false;
    goalAnimationActive = false;
    // Never leave football data visible while the OLED bridge refreshes hockey.
    match = MatchData();
    match.state = "scheduled";
    match.homeCode = "LIT";
    match.awayCode = "KOM";
    match.homeLogo = "litvinov";
    match.awayLogo = "kometa";
    match.statusText = "NACITAM HOKEJ";
    match.tablePosition = 0;
  } else if (screen == FOOTBALL) {
    match = MatchData();
    match.state = "scheduled";
    match.homeCode = TFT_FOOTBALL_TEAM_CODE;
    match.awayCode = "?";
    match.homeLogo = TFT_FOOTBALL_TEAM_CODE;
    match.awayLogo = "";
    match.statusText = "NACITAM FOTBAL";
    match.tablePosition = 0;
  }
  needsRedraw = true;
  if (screen != HOME) {
    lastPoll = millis();
    drawScreen();
    fetchSelectedSport();
  }
}

void handleTouch() {
  if (!touch.touched() || millis() - lastTouch < 250) return;
  const TS_Point point = touch.getPoint();
  // Keep the previously verified physical touch calibration unchanged.
  const int x = clampMap(point.x, 250, 3850, 319, 0);
  const int y = clampMap(point.y, 250, 3850, 239, 0);
  lastTouch = millis();
  lastUserActivity = lastTouch;
  Serial.printf("Touch x=%d y=%d\n", x, y);

  if (analogClockActive) {
    analogClockActive = false;
    analogClockFrameDrawn = false;
    analogClockHandsDrawn = false;
    gameFrameDrawn = false;
    needsRedraw = true;
    return;
  }

  if (screen == HOME) {
    if (TOUCH_DIAGNOSTIC) {
      debugRawX = point.x;
      debugRawY = point.y;
      debugMapX = x;
      debugMapY = y;
      debugTouchSeen = true;
      needsRedraw = true;
      return;
    }
    if (y >= 190) {
      // The rendered slider spans x=50..259; map its complete travel to 20..100 %.
      setBrightness(map(constrain(x, 50, 259), 50, 259, 20, 100));
      needsRedraw = true;
    } else {
      // Measured card centers: football MAP 72,124; hockey MAP 216,111.
      openScreen(x < 160 ? FOOTBALL : HOCKEY);
    }
  } else if (x <= 72 && y <= 60) {
    openScreen(HOME);
  } else {
    match.statusText = "OBNOVUJI...";
    needsRedraw = true;
    drawScreen();
    fetchSelectedSport();
    lastPoll = millis();
  }
}

void setup() {
  Serial.begin(115200);
  lastUserActivity = millis();
  goalLed.begin();
  clearGoalLed();
  delay(300);
  SPI.begin(TFT_SCK_PIN, TFT_MISO_PIN, TFT_MOSI_PIN, TFT_CS_PIN);
  tft.begin(40000000);
  tft.setRotation(1);
  ledcSetup(0, 5000, 8);
  ledcAttachPin(TFT_BACKLIGHT_PIN, 0);
  // Keep the TFT dark through the ESP/Wi-Fi startup peak, then raise it gradually.
  writeBacklight(0);
  touch.begin(SPI);
  touch.setRotation(1);
  u8g2.begin(tft);
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3", "pool.ntp.org", "time.nist.gov");
  drawScreen();
  startWifi();
  bootBacklightRampStarted = millis();
  bootBacklightRampActive = true;
}

void loop() {
  updateBootBacklightRamp();
  updateGoalLed();
  handleTouch();
  handleWifiSetup();
  if (WiFi.status() == WL_CONNECTED) {
    if (!otaReady) startOta();
    if (otaReady) ArduinoOTA.handle();
    if (lastRemoteUpdateCheck == 0 || millis() - lastRemoteUpdateCheck >= REMOTE_UPDATE_CHECK_MS) {
      lastRemoteUpdateCheck = millis();
      checkRemoteFirmwareUpdate();
    }
    if (screen != HOME && millis() - lastPoll >= POLL_INTERVAL_MS) {
      lastPoll = millis();
      fetchSelectedSport();
    }
  } else {
    otaReady = false;
  }
  if (goalAnimationActive) {
    constexpr uint32_t PASS_MS = 2800;
    if (millis() - goalAnimationStarted >= PASS_MS) {
      goalAnimationActive = false;
      needsRedraw = true;
    } else if (millis() - lastGoalFrame >= 50) {
      lastGoalFrame = millis();
      needsRedraw = true;
    }
  }
  const bool shouldUseClock = shouldShowAnalogClock();
  if (shouldUseClock != analogClockActive) {
    analogClockActive = shouldUseClock;
    analogClockFrameDrawn = false;
    analogClockHandsDrawn = false;
    writeBacklight(analogClockActive ? CLOCK_SAVER_BRIGHTNESS : brightness);
    if (!analogClockActive && screen != HOME) gameFrameDrawn = false;
    lastClockRedraw = 0;
    needsRedraw = true;
  }
  if (analogClockActive && millis() - lastClockRedraw >= 1000) {
    lastClockRedraw = millis();
    needsRedraw = true;
  }
  if (!wifiSetupPortalActive && !analogClockActive && screen != HOME && millis() - lastTopBarClockCheck >= 1000) {
    lastTopBarClockCheck = millis();
    if (clockText() != lastTopBarClock) drawTopBar();
  }
  if (needsRedraw) drawScreen();
}
