#pragma once

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>

// Refresh interval — NWS forecast updates every hour; poll every 30 min
#define NWS_FORECAST_INTERVAL (30UL * 60UL * 1000UL)

extern Arduino_GFX *gfx;

// nws_draw_wrapped is defined in NWSAlerts.h (included before this file)
static int nws_draw_wrapped(const String &, int, int, int, uint16_t, int);

static String nws_forecast_get(const String &url) {
  Serial.printf("[Forecast] GET %s\n", url.c_str());
  WiFiClientSecure *client = new WiFiClientSecure;
  if (!client) return "";
  client->setInsecure();
  String body;
  {
    HTTPClient https;
    https.begin(*client, url);
    https.addHeader("User-Agent", "esp32-cyd-nexrad (github.com/Coreymillia)");
    https.addHeader("Accept", "application/geo+json");
    https.setTimeout(15000);
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int code = https.GET();
    if (code == HTTP_CODE_OK) {
      body = https.getString();
    } else {
      Serial.printf("[Forecast] HTTP error: %d\n", code);
    }
    https.end();
  }
  delete client;
  return body;
}

// Fetch the NWS 7-day forecast for the given lat/lon and draw it on screen.
// Uses the two-step NWS API: /points → /forecast.
// Returns true on success, false on any failure.
bool nwsFetchAndDisplayForecast(const char *lat, const char *lon) {
  // ── Step 1: /points → resolve the forecast URL for this location ──────────
  String pointsUrl = String("https://api.weather.gov/points/") + lat + "," + lon;
  String pointsBody = nws_forecast_get(pointsUrl);
  if (pointsBody.isEmpty()) return false;

  DynamicJsonDocument pointsDoc(4096);
  if (deserializeJson(pointsDoc, pointsBody)) {
    Serial.println("[Forecast] Points JSON parse failed");
    return false;
  }
  String forecastUrl = pointsDoc["properties"]["forecast"] | "";
  pointsDoc.clear();

  if (forecastUrl.isEmpty()) {
    Serial.println("[Forecast] No forecast URL in points response");
    return false;
  }
  Serial.printf("[Forecast] URL: %s\n", forecastUrl.c_str());

  // ── Step 2: forecast → extract first two periods ──────────────────────────
  String forecastBody = nws_forecast_get(forecastUrl);
  if (forecastBody.isEmpty()) return false;

  StaticJsonDocument<256> filter;
  filter["properties"]["periods"][0]["name"] = true;
  filter["properties"]["periods"][0]["detailedForecast"] = true;
  filter["properties"]["periods"][1]["name"] = true;
  filter["properties"]["periods"][1]["detailedForecast"] = true;

  DynamicJsonDocument forecastDoc(8192);
  if (deserializeJson(forecastDoc, forecastBody, DeserializationOption::Filter(filter))) {
    Serial.println("[Forecast] Forecast JSON parse failed");
    return false;
  }

  String p0Name   = forecastDoc["properties"]["periods"][0]["name"]             | "Unknown";
  String p0Detail = forecastDoc["properties"]["periods"][0]["detailedForecast"] | "No forecast available.";
  String p1Name   = forecastDoc["properties"]["periods"][1]["name"]             | "";
  String p1Detail = forecastDoc["properties"]["periods"][1]["detailedForecast"] | "";
  forecastDoc.clear();

  Serial.printf("[Forecast] %s: %.80s\n", p0Name.c_str(), p0Detail.c_str());

  // ── Draw on screen ────────────────────────────────────────────────────────
  gfx->fillRect(0, 20, gfx->width(), gfx->height() - 20, RGB565_BLACK);

  // Period 0 name in cyan, larger text
  gfx->setTextColor(0x07FF);  // cyan
  gfx->setTextSize(2);
  gfx->setCursor(4, 25);
  gfx->print(p0Name);

  // Period 0 detailed forecast word-wrapped in white, capped at y=113
  nws_draw_wrapped(p0Detail, 4, 44, gfx->width() - 8, RGB565_WHITE, 113);

  // Period 1 (if available)
  if (p1Name.length() > 0) {
    gfx->drawFastHLine(0, 115, gfx->width(), 0x2104);
    gfx->setTextColor(0xFFE0);  // yellow
    gfx->setTextSize(1);
    gfx->setCursor(4, 119);
    gfx->print(p1Name);
    nws_draw_wrapped(p1Detail, 4, 130, gfx->width() - 8, 0xC618, 228);  // light gray
  }

  return true;
}
