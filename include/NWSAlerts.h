#pragma once

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>

#define NWS_USER_AGENT      "esp32-cyd-nexrad (github.com/Coreymillia)"
#define NWS_ALERTS_INTERVAL  (5UL * 60UL * 1000UL)  // 5 minutes

extern Arduino_GFX *gfx;

static String nws_alerts_get(const String &url) {
  Serial.printf("[NWS] GET %s\n", url.c_str());
  WiFiClientSecure *client = new WiFiClientSecure;
  if (!client) return "";
  client->setInsecure();
  String body;
  {
    HTTPClient https;
    https.begin(*client, url);
    https.addHeader("User-Agent", NWS_USER_AGENT);
    https.addHeader("Accept", "application/geo+json");
    https.setTimeout(15000);
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int code = https.GET();
    if (code == HTTP_CODE_OK) {
      body = https.getString();
    } else {
      Serial.printf("[NWS] HTTP error: %d\n", code);
    }
    https.end();
  }
  delete client;
  return body;
}

// Word-wrap and draw text on the display. Returns y position after last line.
static int nws_draw_wrapped(const String &text, int x, int y, int maxW,
                             uint16_t color, int maxY = 228) {
  gfx->setTextColor(color);
  gfx->setTextSize(1);
  const int charW = 6, lineH = 10;
  int charsPerLine = maxW / charW;
  int pos = 0, len = text.length();
  while (pos < len && y < maxY) {
    int end = pos + charsPerLine;
    if (end >= len) {
      end = len;
    } else {
      for (int i = end; i > pos; i--) {
        if (text[i] == ' ') { end = i; break; }
      }
    }
    String line = text.substring(pos, end);
    line.trim();
    gfx->setCursor(x, y);
    gfx->print(line);
    y += lineH;
    pos = end;
    while (pos < len && text[pos] == ' ') pos++;
  }
  return y;
}

// Fetch NWS active alerts for lat/lon and display them. Returns true on success.
bool nwsFetchAndDisplayAlerts(const char *lat, const char *lon) {
  String url = String("https://api.weather.gov/alerts/active?point=") + lat + "," + lon;
  String body = nws_alerts_get(url);
  if (body.isEmpty()) return false;

  StaticJsonDocument<128> filter;
  filter["features"][0]["properties"]["event"]    = true;
  filter["features"][0]["properties"]["headline"] = true;

  DynamicJsonDocument doc(3072);
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) {
    Serial.println("[NWS] Alerts JSON parse failed");
    return false;
  }

  JsonArray features = doc["features"];
  int alertCount = features.size();

  gfx->fillRect(0, 20, gfx->width(), gfx->height() - 20, RGB565_BLACK);

  if (alertCount == 0) {
    gfx->setTextColor(0x07E0);  // green
    gfx->setTextSize(2);
    gfx->setCursor(4, 30);
    gfx->print("NWS Alerts");
    gfx->setTextColor(RGB565_WHITE);
    gfx->setTextSize(1);
    gfx->setCursor(4, 58);
    gfx->print("No active alerts");
    gfx->setCursor(4, 70);
    gfx->print("for your area.");
  } else {
    gfx->setTextColor(0xF800);  // red
    gfx->setTextSize(2);
    gfx->setCursor(4, 25);
    char title[24];
    snprintf(title, sizeof(title), "%d Alert%s!", alertCount, alertCount > 1 ? "s" : "");
    gfx->print(title);

    int y = 46;
    int shown = 0;
    for (JsonObject feature : features) {
      if (shown >= 2) break;
      String event    = feature["properties"]["event"]    | "Unknown Event";
      String headline = feature["properties"]["headline"] | "";

      gfx->setTextColor(0xFFE0);  // yellow
      gfx->setTextSize(1);
      gfx->setCursor(4, y);
      if (event.length() > 35) event = event.substring(0, 34);
      gfx->print(event);
      y += 12;

      if (headline.length() > 120) headline = headline.substring(0, 119);
      y = nws_draw_wrapped(headline, 4, y, gfx->width() - 8, RGB565_WHITE);
      y += 4;
      shown++;
    }
  }

  Serial.printf("[NWS] Alerts: %d active\n", alertCount);
  return true;
}
