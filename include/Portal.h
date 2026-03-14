#pragma once

#include <Arduino_GFX_Library.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

extern Arduino_GFX *gfx;

// ---------------------------------------------------------------------------
// Zoom level definitions (radar modes 0–2, alerts mode 3, forecast mode 4, sun/moon mode 5)
// ---------------------------------------------------------------------------
struct ZoomLevel {
  const char *name;
  float       degrees;  // half-span in degrees latitude for bounding box
};

static const ZoomLevel ZOOM_LEVELS[] = {
  { "Local (~100 mi)",    1.5f },
  { "Regional (~240 mi)", 3.5f },
  { "Wide (~480 mi)",     7.0f },
};
static const int NUM_ZOOM_LEVELS = 3;

#define NWS_ALERTS_MODE   (NUM_ZOOM_LEVELS)      // 3
#define NWS_FORECAST_MODE (NUM_ZOOM_LEVELS + 1)  // 4
#define SUN_MOON_MODE     (NUM_ZOOM_LEVELS + 2)  // 5
#define NUM_MODES         (NUM_ZOOM_LEVELS + 3)  // 6 total

// ---------------------------------------------------------------------------
// Persisted settings
// ---------------------------------------------------------------------------
static char nr_wifi_ssid[64] = "";
static char nr_wifi_pass[64] = "";
static int  nr_mode_idx      = 0;
static char nr_lat[16]       = "";
static char nr_lon[16]       = "";
static bool nr_has_settings  = false;

// ---------------------------------------------------------------------------
// Portal state
// ---------------------------------------------------------------------------
static WebServer *portalServer = nullptr;
static DNSServer *portalDNS    = nullptr;
static bool       portalDone   = false;

// ---------------------------------------------------------------------------
// NVS helpers
// ---------------------------------------------------------------------------
static void nrLoadSettings() {
  Preferences prefs;
  prefs.begin("nexradcyd", true);
  String ssid = prefs.getString("ssid", "");
  String pass = prefs.getString("pass", "");
  String lat  = prefs.getString("lat",  "");
  String lon  = prefs.getString("lon",  "");
  nr_mode_idx = prefs.getInt("mode", 0);
  prefs.end();

  nr_mode_idx = constrain(nr_mode_idx, 0, NUM_MODES - 1);
  ssid.toCharArray(nr_wifi_ssid, sizeof(nr_wifi_ssid));
  pass.toCharArray(nr_wifi_pass, sizeof(nr_wifi_pass));
  lat.toCharArray(nr_lat, sizeof(nr_lat));
  lon.toCharArray(nr_lon, sizeof(nr_lon));
  nr_has_settings = (ssid.length() > 0);
}

static void nrSaveSettings(const char *ssid, const char *pass, int mode,
                            const char *lat, const char *lon) {
  Preferences prefs;
  prefs.begin("nexradcyd", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.putInt   ("mode", mode);
  prefs.putString("lat",  lat);
  prefs.putString("lon",  lon);
  prefs.end();

  strncpy(nr_wifi_ssid, ssid, sizeof(nr_wifi_ssid) - 1);
  strncpy(nr_wifi_pass, pass, sizeof(nr_wifi_pass) - 1);
  strncpy(nr_lat, lat, sizeof(nr_lat) - 1);
  strncpy(nr_lon, lon, sizeof(nr_lon) - 1);
  nr_mode_idx     = mode;
  nr_has_settings = true;
}

static void nrSaveModeIndex(int mode) {
  Preferences prefs;
  prefs.begin("nexradcyd", false);
  prefs.putInt("mode", mode);
  prefs.end();
  nr_mode_idx = mode;
}

// ---------------------------------------------------------------------------
// On-screen setup instructions
// ---------------------------------------------------------------------------
static void nrShowPortalScreen() {
  gfx->fillScreen(RGB565_BLACK);

  gfx->setTextColor(0x07FF);  // cyan
  gfx->setTextSize(2);
  gfx->setCursor(22, 5);
  gfx->print("NEXRADCore Setup");

  gfx->setTextColor(RGB565_WHITE);
  gfx->setTextSize(1);
  gfx->setCursor(60, 26);
  gfx->print("Live NEXRAD Radar");

  gfx->setTextColor(0xFFE0);  // yellow
  gfx->setCursor(4, 46);
  gfx->print("1. Connect to WiFi network:");
  gfx->setTextColor(0x07FF);
  gfx->setTextSize(2);
  gfx->setCursor(14, 58);
  gfx->print("NEXRADCore_Setup");

  gfx->setTextColor(0xFFE0);
  gfx->setTextSize(1);
  gfx->setCursor(4, 82);
  gfx->print("2. Open browser and go to:");
  gfx->setTextColor(0x07FF);
  gfx->setTextSize(2);
  gfx->setCursor(50, 94);
  gfx->print("192.168.4.1");

  gfx->setTextColor(0xFFE0);
  gfx->setTextSize(1);
  gfx->setCursor(4, 118);
  gfx->print("3. Enter your WiFi & location,");
  gfx->setCursor(4, 130);
  gfx->print("   then tap  Save & Connect.");

  if (nr_has_settings) {
    gfx->setTextColor(0x07E0);  // green
    gfx->setCursor(4, 152);
    gfx->print("Existing settings found. Tap");
    gfx->setCursor(4, 164);
    gfx->print("'No Changes' to keep them.");
  }
}

// ---------------------------------------------------------------------------
// Web handlers
// ---------------------------------------------------------------------------
static void nrHandleRoot() {
  String html =
    "<!DOCTYPE html><html><head>"
    "<meta charset='UTF-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>NEXRADCore Setup</title>"
    "<style>"
    "body{background:#001a33;color:#00ccff;font-family:Arial,sans-serif;"
         "text-align:center;padding:20px;max-width:480px;margin:auto;}"
    "h1{color:#00ffff;font-size:1.6em;margin-bottom:4px;}"
    "p{color:#88aacc;font-size:0.9em;}"
    "label{display:block;text-align:left;margin:14px 0 4px;color:#88ddff;font-weight:bold;}"
    "input,select{width:100%;box-sizing:border-box;background:#002244;color:#00ccff;"
                 "border:2px solid #0066aa;border-radius:6px;padding:10px;font-size:1em;}"
    ".btn{display:block;width:100%;padding:14px;margin:10px 0;font-size:1.05em;"
         "border-radius:8px;border:none;cursor:pointer;font-weight:bold;}"
    ".btn-save{background:#004488;color:#00ffff;border:2px solid #0099dd;}"
    ".btn-save:hover{background:#0066bb;}"
    ".btn-skip{background:#1a1a2e;color:#667788;border:2px solid #334455;}"
    ".btn-skip:hover{background:#223344;color:#aabbcc;}"
    ".note{color:#445566;font-size:0.82em;margin-top:16px;}"
    "hr{border:1px solid #113355;margin:20px 0;}"
    "</style></head><body>"
    "<h1>&#127928; NEXRADCore Setup</h1>"
    "<p>Enter your WiFi credentials and location for live NEXRAD radar.</p>"
    "<form method='post' action='/save'>"
    "<label>WiFi Network Name (SSID):</label>"
    "<input type='text' name='ssid' value='";
  html += String(nr_wifi_ssid);
  html += "' placeholder='Your 2.4 GHz WiFi name' maxlength='63' required>"
    "<label>WiFi Password:</label>"
    "<input type='password' name='pass' value='";
  html += String(nr_wifi_pass);
  html += "' placeholder='Leave blank if open network' maxlength='63'>"
    "<label>Default Radar View:</label>"
    "<select name='mode'>";

  for (int i = 0; i < NUM_ZOOM_LEVELS; i++) {
    html += "<option value='" + String(i) + "'";
    if (i == nr_mode_idx) html += " selected";
    html += ">&#127928; NEXRAD " + String(ZOOM_LEVELS[i].name) + "</option>";
  }
  html += "<option value='" + String(NWS_ALERTS_MODE) + "'";
  if (nr_mode_idx == NWS_ALERTS_MODE) html += " selected";
  html += ">&#128680; NWS Alerts</option>";
  html += "<option value='" + String(NWS_FORECAST_MODE) + "'";
  if (nr_mode_idx == NWS_FORECAST_MODE) html += " selected";
  html += ">&#127783; NWS Forecast</option>";
  html += "<option value='" + String(SUN_MOON_MODE) + "'";
  if (nr_mode_idx == SUN_MOON_MODE) html += " selected";
  html += ">&#127774; Sun &amp; Moon</option>";

  html += "</select>"
    "<label>Latitude (decimal degrees, e.g. 38.8894):</label>"
    "<input type='text' name='lat' value='";
  html += String(nr_lat);
  html += "' placeholder='e.g. 38.8894' maxlength='15'>"
    "<label>Longitude (decimal degrees, e.g. -77.0352):</label>"
    "<input type='text' name='lon' value='";
  html += String(nr_lon);
  html += "' placeholder='e.g. -77.0352' maxlength='15'>"
    "<br>"
    "<button class='btn btn-save' type='submit'>&#128190; Save &amp; Connect</button>"
    "</form>";

  if (nr_has_settings) {
    html += "<hr>"
      "<form method='post' action='/nochange'>"
      "<button class='btn btn-skip' type='submit'>"
      "&#10006; No Changes &mdash; Use Current Settings</button>"
      "</form>";
  }

  html +=
    "<p class='note'>&#9888; ESP32 supports 2.4 GHz WiFi only.</p>"
    "<p class='note'>&#127760; NEXRAD composite covers contiguous US (CONUS) only.</p>"
    "<p class='note'>Touch left/right on the display to cycle zoom levels.</p>"
    "</body></html>";

  portalServer->send(200, "text/html", html);
}

static void nrHandleSave() {
  String ssid = portalServer->hasArg("ssid") ? portalServer->arg("ssid") : "";
  String pass = portalServer->hasArg("pass") ? portalServer->arg("pass") : "";
  int    mode = portalServer->hasArg("mode") ? portalServer->arg("mode").toInt() : 0;
  String lat  = portalServer->hasArg("lat")  ? portalServer->arg("lat")  : "";
  String lon  = portalServer->hasArg("lon")  ? portalServer->arg("lon")  : "";
  mode = constrain(mode, 0, NUM_MODES - 1);

  if (ssid.length() == 0) {
    portalServer->send(400, "text/html",
      "<html><body style='background:#001a33;color:#ff5555;font-family:Arial;"
      "text-align:center;padding:40px'>"
      "<h2>&#10060; SSID cannot be empty!</h2>"
      "<a href='/' style='color:#00ccff'>&#8592; Go Back</a></body></html>");
    return;
  }

  nrSaveSettings(ssid.c_str(), pass.c_str(), mode, lat.c_str(), lon.c_str());

  const char *modeName;
  if      (mode == NWS_ALERTS_MODE)   modeName = "NWS Alerts";
  else if (mode == NWS_FORECAST_MODE) modeName = "NWS Forecast";
  else if (mode == SUN_MOON_MODE)     modeName = "Sun & Moon";
  else modeName = ZOOM_LEVELS[mode].name;

  String html =
    "<html><head><meta charset='UTF-8'>"
    "<style>body{background:#001a33;color:#00ccff;font-family:Arial;"
    "text-align:center;padding:40px;}h2{color:#00ffff;}"
    "p{color:#88aacc;}</style></head><body>"
    "<h2>&#9989; Settings Saved!</h2>"
    "<p>Connecting to <b>" + ssid + "</b>...</p>"
    "<p>Default view: <b>" + String(modeName) + "</b></p>"
    "<p>You can close this page and disconnect from <b>NEXRADCore_Setup</b>.</p>"
    "<p style='color:#445566;font-size:0.85em'>"
    "The radar display will appear shortly.</p>"
    "</body></html>";
  portalServer->send(200, "text/html", html);

  delay(1500);
  portalDone = true;
}

static void nrHandleNoChange() {
  String html =
    "<html><head><meta charset='UTF-8'>"
    "<style>body{background:#001a33;color:#00ccff;font-family:Arial;"
    "text-align:center;padding:40px;}h2{color:#00ffff;}"
    "p{color:#88aacc;}</style></head><body>"
    "<h2>&#128077; No Changes</h2>"
    "<p>Using your saved settings. Device is connecting now.</p>"
    "<p>You can close this page and disconnect from <b>NEXRADCore_Setup</b>.</p>"
    "</body></html>";
  portalServer->send(200, "text/html", html);

  delay(1500);
  portalDone = true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

static void nrInitPortal() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("NEXRADCore_Setup", "");
  delay(500);

  portalDNS    = new DNSServer();
  portalServer = new WebServer(80);

  portalDNS->start(53, "*", WiFi.softAPIP());
  portalServer->on("/",         nrHandleRoot);
  portalServer->on("/save",     HTTP_POST, nrHandleSave);
  portalServer->on("/nochange", HTTP_POST, nrHandleNoChange);
  portalServer->onNotFound(nrHandleRoot);
  portalServer->begin();

  portalDone = false;
  nrShowPortalScreen();

  Serial.printf("[Portal] AP up — connect to NEXRADCore_Setup, open %s\n",
                WiFi.softAPIP().toString().c_str());
}

static void nrRunPortal() {
  portalDNS->processNextRequest();
  portalServer->handleClient();
}

static void nrClosePortal() {
  portalServer->stop();
  portalDNS->stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(300);
  delete portalServer; portalServer = nullptr;
  delete portalDNS;    portalDNS    = nullptr;
}
