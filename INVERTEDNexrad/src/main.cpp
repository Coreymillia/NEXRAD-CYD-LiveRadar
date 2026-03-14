// NEXRADCore INVERTED — identical to main project but with display inversion.
// Use this build if your CYD shows colors inverted (common on some CYD variants).
// The only difference from src/main.cpp is gfx->invertDisplay(true) in setup().

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <math.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>

#include "HTTPS.h"
#include "JPEG.h"
#include "NWSAlerts.h"
#include "NWSForecast.h"

#include <Arduino_GFX_Library.h>

#define DEVICE_NAME      "NEXRADCore-INV"
#define FIRMWARE_VERSION "1.0.0"
#include "CYDIdentity.h"
#include "Portal.h"

#define GFX_BL 21

Arduino_DataBus *bus = new Arduino_HWSPI(
    2  /* DC */,
    15 /* CS */,
    14 /* SCK */,
    13 /* MOSI */,
    12 /* MISO */);

Arduino_GFX *gfx = new Arduino_ILI9341(
    bus, GFX_NOT_DEFINED /* RST */, 1 /* rotation: landscape 320x240 */);

#define TOUCH_CS   33
#define TOUCH_IRQ  36
SPIClass touchSPI(VSPI);
XPT2046_Touchscreen ts(TOUCH_CS, TOUCH_IRQ);

#define RADAR_INTERVAL  (5UL * 60UL * 1000UL)
#define CLOCK_INTERVAL  (60UL * 1000UL)

void showStatus(const char *msg) {
  gfx->fillRect(0, 0, gfx->width(), 20, RGB565_BLACK);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(4, 6);
  gfx->print(msg);
  Serial.println(msg);
}

void drawTimestamp() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return;
  char buf[12];
  strftime(buf, sizeof(buf), "%H:%M UTC", &timeinfo);
  int tw = strlen(buf) * 6;
  int tx = gfx->width()  - tw - 3;
  int ty = gfx->height() - 10;
  gfx->fillRect(tx - 1, ty - 1, tw + 2, 10, RGB565_BLACK);
  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(tx, ty);
  gfx->print(buf);
}

int PNGDraw(PNGDRAW *pDraw) {
  uint16_t lineBuffer[320];
  png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  gfx->draw16bitBeRGBBitmap(0, 20 + pDraw->y, lineBuffer, pDraw->iWidth, 1);
  return 1;
}

// ---------------------------------------------------------------------------
// City overlay — drawn after PNG decode using bounding box → pixel math
// ---------------------------------------------------------------------------
struct CityMark { float lat, lon; const char *name; };

static const CityMark LOCAL_CITIES[] = {
  { 38.7172f, -105.1364f, "Victor" },
};

// Regional mode: major Colorado reference cities
// (Victor is already shown as the Local crosshair — not repeated here)
static const CityMark REGIONAL_CITIES[] = {
  { 39.7392f, -104.9903f, "Denver"    },
  { 40.5853f, -105.0844f, "Ft Collins"},
  { 38.8339f, -104.8214f, "C.Springs" },
  { 38.2544f, -104.6091f, "Pueblo"    },
  { 39.0639f, -108.5506f, "Gr.Jct"   },
};

// Wide mode: major US cities — latlonToPixel clamps any that fall outside the view
static const CityMark WIDE_CITIES[] = {
  { 47.6062f, -122.3321f, "Seattle"   },
  { 45.5051f, -122.6750f, "Portland"  },
  { 37.7749f, -122.4194f, "S.F."      },
  { 34.0522f, -118.2437f, "L.A."      },
  { 36.1699f, -115.1398f, "Las Vegas" },
  { 33.4484f, -112.0740f, "Phoenix"   },
  { 43.6187f, -116.2146f, "Boise"     },
  { 40.7608f, -111.8910f, "SLC"       },
  { 39.7392f, -104.9903f, "Denver"    },
  { 35.0844f, -106.6504f, "Albuqrq"  },
  { 41.2565f,  -95.9345f, "Omaha"     },
  { 39.0997f,  -94.5786f, "K.City"    },
  { 44.9778f,  -93.2650f, "Minneap."  },
  { 41.8781f,  -87.6298f, "Chicago"   },
  { 35.4676f,  -97.5164f, "OKC"       },
  { 32.7767f,  -96.7970f, "Dallas"    },
  { 29.7604f,  -95.3698f, "Houston"   },
  { 35.1495f,  -90.0490f, "Memphis"   },
  { 36.1627f,  -86.7816f, "Nashville" },
  { 38.6270f,  -90.1994f, "St.Louis"  },
  { 33.7490f,  -84.3880f, "Atlanta"   },
  { 38.9072f,  -77.0369f, "D.C."      },
  { 40.7128f,  -74.0060f, "New York"  },
  { 42.3601f,  -71.0589f, "Boston"    },
  { 25.7617f,  -80.1918f, "Miami"     },
};

static bool latlonToPixel(float cityLat, float cityLon,
                           float minLon, float maxLon,
                           float minLat, float maxLat,
                           int &px, int &py) {
  px = (int)((cityLon - minLon) / (maxLon - minLon) * 320.0f);
  py = 20 + (int)((maxLat - cityLat) / (maxLat - minLat) * 220.0f);
  return (px >= 3 && px <= 316 && py >= 23 && py <= 236);
}

static void drawCityOverlay(float centerLat, float centerLon, float zoom_deg,
                            const CityMark *cities, int count, bool crosshair) {
  float lat_half = zoom_deg;
  float lon_half = zoom_deg * (320.0f / 240.0f) / cosf(centerLat * (M_PI / 180.0f));
  float minLon = centerLon - lon_half, maxLon = centerLon + lon_half;
  float minLat = centerLat - lat_half, maxLat = centerLat + lat_half;

  for (int i = 0; i < count; i++) {
    int px, py;
    if (!latlonToPixel(cities[i].lat, cities[i].lon,
                       minLon, maxLon, minLat, maxLat, px, py)) continue;

    if (crosshair) {
      gfx->drawFastHLine(px - 6, py,     13, 0xFD20);  // orange
      gfx->drawFastVLine(px,     py - 6, 13, 0xFD20);
      gfx->fillCircle(px, py, 2, 0xFD20);
    } else {
      gfx->fillCircle(px, py, 2, 0xFD20);  // orange dot
    }

    int tx = px + 4;
    int ty = py - 8;
    if (tx + (int)strlen(cities[i].name) * 6 > 318) tx = px - (int)strlen(cities[i].name) * 6 - 3;
    if (ty < 22) ty = py + 3;
    gfx->setTextSize(1);
    gfx->setTextColor(0xFFE0);  // yellow label
    gfx->setCursor(tx, ty);
    gfx->print(cities[i].name);
  }
}

static void buildRadarUrl(char *buf, size_t buflen, float lat, float lon,
                          float zoom_deg) {
  float lat_half = zoom_deg;
  float lon_half = zoom_deg * (320.0f / 240.0f) / cosf(lat * (M_PI / 180.0f));
  float minLon = lon - lon_half, maxLon = lon + lon_half;
  float minLat = lat - lat_half, maxLat = lat + lat_half;
  snprintf(buf, buflen,
    "https://opengeo.ncep.noaa.gov/geoserver/ows"
    "?service=WMS&VERSION=1.1.1&REQUEST=GetMap"
    "&FORMAT=image/png8"
    "&WIDTH=320&HEIGHT=240"
    "&SRS=EPSG:4326"
    "&LAYERS=geopolitical,conus_bref_qcd"
    "&BGCOLOR=0x0D1B2A"
    "&BBOX=%.4f,%.4f,%.4f,%.4f",
    minLon, minLat, maxLon, maxLat);
}

static void showModeStatus() {
  if (nr_mode_idx == NWS_ALERTS_MODE) {
    showStatus("Mode: NWS Alerts");
  } else if (nr_mode_idx == NWS_FORECAST_MODE) {
    showStatus("Mode: NWS Forecast");
  } else {
    char msg[48];
    snprintf(msg, sizeof(msg), "NEXRAD %s", ZOOM_LEVELS[nr_mode_idx].name);
    showStatus(msg);
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("NEXRADCore INVERTED - Live NEXRAD Radar (CYD)");

  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }
  gfx->invertDisplay(true);  // ← only difference from the standard build
  gfx->fillScreen(RGB565_BLACK);

  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
  pinMode(0, INPUT_PULLUP);

  touchSPI.begin(25, 39, 32, TOUCH_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  nrLoadSettings();

  bool showPortal = !nr_has_settings;

  if (!showPortal) {
    showStatus("Hold BOOT button to change settings...");
    for (int i = 0; i < 30 && !showPortal; i++) {
      if (digitalRead(0) == LOW) showPortal = true;
      delay(100);
    }
  }

  if (showPortal) {
    nrInitPortal();
    while (!portalDone) {
      nrRunPortal();
      delay(5);
    }
    nrClosePortal();
  }

  gfx->fillScreen(RGB565_BLACK);
  WiFi.mode(WIFI_STA);
  WiFi.begin(nr_wifi_ssid, nr_wifi_pass);

  int dots = 0;
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - wifiStart > 30000) {
      char errMsg[60];
      snprintf(errMsg, sizeof(errMsg), "WiFi failed: \"%s\"", nr_wifi_ssid);
      showStatus(errMsg);
      while (true) delay(1000);
    }
    delay(500);
    char msg[48];
    snprintf(msg, sizeof(msg), "Connecting to WiFi%.*s", (dots % 4) + 1, "....");
    showStatus(msg);
    dots++;
  }
  showStatus("WiFi connected!");
  identityBegin();
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  delay(600);
}

static unsigned long last_update  = 0;
static unsigned long last_clock   = 0;
static unsigned long lastTouchMs  = 0;

void loop() {
  identityHandle();

  if (digitalRead(0) == LOW) {
    delay(50);
    if (digitalRead(0) == LOW) {
      unsigned long pressStart = millis();
      while (digitalRead(0) == LOW) delay(10);
      unsigned long held = millis() - pressStart;

      if (held >= 1500) {
        showStatus("Opening setup... hold until AP appears");
        WiFi.disconnect(true);
        delay(500);
        nrInitPortal();
        while (!portalDone) { nrRunPortal(); delay(5); }
        nrClosePortal();
        gfx->fillScreen(RGB565_BLACK);
        WiFi.mode(WIFI_STA);
        WiFi.begin(nr_wifi_ssid, nr_wifi_pass);
        showStatus("Reconnecting to WiFi...");
        unsigned long t = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - t < 30000) delay(500);
        if (WiFi.status() == WL_CONNECTED) showStatus("WiFi connected!");
        last_update = 0;
      } else {
        nr_mode_idx = (nr_mode_idx + 1) % NUM_MODES;
        nrSaveModeIndex(nr_mode_idx);
        showModeStatus();
        last_update = 0;
      }
    }
  }

  if (ts.tirqTouched() && ts.touched()) {
    if (millis() - lastTouchMs > 400) {
      lastTouchMs = millis();
      TS_Point p  = ts.getPoint();
      int tx = map(p.x, 200, 3900, 0, 320);
      tx = constrain(tx, 0, 319);

      if (tx < 107) {
        nr_mode_idx = (nr_mode_idx + NUM_MODES - 1) % NUM_MODES;
        nrSaveModeIndex(nr_mode_idx);
        showModeStatus();
        last_update = 0;
      } else if (tx > 213) {
        nr_mode_idx = (nr_mode_idx + 1) % NUM_MODES;
        nrSaveModeIndex(nr_mode_idx);
        showModeStatus();
        last_update = 0;
      }
    }
  }

  if (WiFi.status() != WL_CONNECTED) {
    showStatus("WiFi lost - reconnecting...");
    WiFi.reconnect();
    unsigned long t = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t < 15000) delay(500);
    if (WiFi.status() == WL_CONNECTED) {
      showStatus("WiFi reconnected!");
      delay(500);
    }
  }

  unsigned long currentInterval;
  if      (nr_mode_idx == NWS_ALERTS_MODE)   currentInterval = NWS_ALERTS_INTERVAL;
  else if (nr_mode_idx == NWS_FORECAST_MODE) currentInterval = NWS_FORECAST_INTERVAL;
  else                                        currentInterval = RADAR_INTERVAL;

  if ((last_update == 0) || (millis() - last_update > currentInterval)) {
    Serial.printf("Heap: %d, PSRAM: %d\n", ESP.getFreeHeap(), ESP.getFreePsram());

    if (nr_mode_idx == NWS_ALERTS_MODE) {
      showStatus("Checking NWS alerts...");
      if (nwsFetchAndDisplayAlerts(nr_lat, nr_lon)) {
        last_update = millis();
        identity_last_fetch = millis() / 1000UL;
        drawTimestamp();
      } else {
        showStatus("NWS fetch failed - retrying in 60s");
        last_update = millis() - NWS_ALERTS_INTERVAL + 60000UL;
      }

    } else if (nr_mode_idx == NWS_FORECAST_MODE) {
      showStatus("Fetching NWS forecast...");
      if (nwsFetchAndDisplayForecast(nr_lat, nr_lon)) {
        last_update = millis();
        identity_last_fetch = millis() / 1000UL;
        showStatus("Mode: NWS Forecast");
        drawTimestamp();
      } else {
        showStatus("Forecast failed - retrying in 60s");
        last_update = millis() - NWS_FORECAST_INTERVAL + 60000UL;
      }

    } else {
      showModeStatus();

      char radarUrl[512];
      float lat = atof(nr_lat);
      float lon = atof(nr_lon);
      buildRadarUrl(radarUrl, sizeof(radarUrl), lat, lon,
                    ZOOM_LEVELS[nr_mode_idx].degrees);

      Serial.printf("[Radar] URL: %s\n", radarUrl);
      https_get_response_buf(radarUrl);

      if (https_response_buf && https_response_len > 0 &&
          png.openRAM(https_response_buf, https_response_len, PNGDraw) == PNG_SUCCESS) {
        showStatus("Decoding radar...");
        gfx->fillRect(0, 20, gfx->width(), gfx->height() - 20, RGB565_BLACK);
        png.decode(NULL, 0);
        png.close();
        last_update = millis();
        identity_last_fetch = millis() / 1000UL;
        drawTimestamp();

        // Draw city markers
        if (nr_mode_idx == 0) {
          drawCityOverlay(lat, lon, ZOOM_LEVELS[0].degrees,
                          LOCAL_CITIES, 1, true);
        } else if (nr_mode_idx == 1) {
          drawCityOverlay(lat, lon, ZOOM_LEVELS[1].degrees,
                          REGIONAL_CITIES,
                          sizeof(REGIONAL_CITIES) / sizeof(REGIONAL_CITIES[0]),
                          false);
        } else if (nr_mode_idx == 2) {
          drawCityOverlay(lat, lon, ZOOM_LEVELS[2].degrees,
                          WIDE_CITIES,
                          sizeof(WIDE_CITIES) / sizeof(WIDE_CITIES[0]),
                          false);
        }

        // Clear status bar to show mode name
        showModeStatus();

      } else {
        char errMsg[60];
        snprintf(errMsg, sizeof(errMsg), "Radar failed HTTP:%d len:%d",
                 https_last_http_code, https_response_len);
        showStatus(errMsg);
        Serial.println(errMsg);
        identity_error_flags |= 0x01;
        last_update = millis() - RADAR_INTERVAL + 60000UL;
      }

      if (https_response_buf) {
        free(https_response_buf);
        https_response_buf = nullptr;
      }
    }
  }

  if (last_update != 0 && millis() - last_clock > CLOCK_INTERVAL) {
    drawTimestamp();
    last_clock = millis();
  }

  {
    unsigned long elapsed = (last_update == 0) ? 0 : (millis() - last_update);
    if (elapsed > currentInterval) elapsed = currentInterval;
    int barW = (int)((long)(currentInterval - elapsed) * gfx->width() / currentInterval);
    gfx->drawFastHLine(0,    gfx->height() - 1, barW,                0x001F);
    gfx->drawFastHLine(barW, gfx->height() - 1, gfx->width() - barW, RGB565_BLACK);
  }

  delay(50);
}
