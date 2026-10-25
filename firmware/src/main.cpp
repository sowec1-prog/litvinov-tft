#include <Arduino.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>
#include "config.h"

// Phase 1: prove Wi-Fi and the compact local/server contract.
// Display/touch driver is deliberately separate until its exact pin labels are confirmed.
static constexpr uint32_t POLL_INTERVAL_MS = 30000;
static uint32_t lastPoll = 0;
static bool otaServiceReady = false;
static bool otaConfigReported = false;

bool otaConfigured() {
  return strlen(OTA_PASSWORD) >= 12 && strncmp(OTA_PASSWORD, "CHANGE_ME_", 10) != 0;
}

void startOta() {
  if (!otaConfigured()) {
    if (!otaConfigReported) {
      Serial.println("OTA disabled: set a unique OTA_PASSWORD in local config.h");
      otaConfigReported = true;
    }
    return;
  }
  ArduinoOTA.setHostname(OTA_HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() { Serial.println("OTA update started"); });
  ArduinoOTA.onEnd([]() { Serial.println("OTA update complete; rebooting"); });
  ArduinoOTA.onError([](ota_error_t error) { Serial.printf("OTA error: %u\n", error); });
  ArduinoOTA.begin();
  otaServiceReady = true;
  Serial.printf("OTA ready on local LAN as %s\n", OTA_HOSTNAME);
}

void fetchSport(const char* sport) {
  HTTPClient http;
  const String url = String(TFT_API_BASE_URL) + "/api/v1/sport/" + sport;
  http.setTimeout(5000);
  if (!http.begin(url)) {
    Serial.println("API begin failed");
    return;
  }
  const int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("API HTTP %d\n", code);
    http.end();
    return;
  }
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, http.getStream());
  http.end();
  if (error) {
    Serial.printf("JSON error: %s\n", error.c_str());
    return;
  }
  Serial.printf("%s: %s %s-%s, state=%s\n", sport,
                doc["competition"] | "?", doc["home"]["code"] | "?",
                doc["away"]["code"] | "?", doc["state"] | "?");
  // Next phase renders this verified payload using local LittleFS logo files.
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("Litvinov TFT S3: network-contract test");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    otaServiceReady = false;
    return;
  }
  if (!otaServiceReady) startOta();
  if (otaServiceReady) ArduinoOTA.handle();
  if (millis() - lastPoll < POLL_INTERVAL_MS) return;
  lastPoll = millis();
  fetchSport("hockey");
}
