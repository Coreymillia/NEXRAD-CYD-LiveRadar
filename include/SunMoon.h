#pragma once

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <math.h>
#include <time.h>

// Refresh hourly — sunrise/sunset API + moon position are stable over hours
#define SUN_MOON_INTERVAL (60UL * 60UL * 1000UL)

extern Arduino_GFX *gfx;

// ── HTTP helper ───────────────────────────────────────────────────────────────
static String sm_https_get(const String &url) {
  Serial.printf("[SunMoon] GET %s\n", url.c_str());
  WiFiClientSecure *client = new WiFiClientSecure;
  if (!client) return "";
  client->setInsecure();
  String body;
  {
    HTTPClient https;
    https.begin(*client, url);
    https.addHeader("User-Agent", "esp32-cyd-nexrad (github.com/Coreymillia)");
    https.setTimeout(15000);
    https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    int code = https.GET();
    if (code == HTTP_CODE_OK) body = https.getString();
    else Serial.printf("[SunMoon] HTTP %d\n", code);
    https.end();
  }
  delete client;
  return body;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

// Parse "2026-03-14T06:14:22+00:00" → h, m (UTC)
static void sm_parse_iso(const String &s, int &h, int &m) {
  h = m = 0;
  int t = s.indexOf('T');
  if (t < 0) return;
  h = s.substring(t + 1, t + 3).toInt();
  m = s.substring(t + 4, t + 6).toInt();
}

static void sm_fmt(char *buf, int h, int m) {
  if (h < 0) snprintf(buf, 8, "--:--");
  else        snprintf(buf, 8, "%02d:%02d", h, m);
}

// ── Astronomy ─────────────────────────────────────────────────────────────────

static double sm_rev(double x) { return x - 360.0 * floor(x / 360.0); }

// Julian Day Number
static double sm_jd(int yr, int mo, int dy, double h = 0.0) {
  if (mo <= 2) { yr--; mo += 12; }
  int A = yr / 100, B = 2 - A + A / 4;
  return (int)(365.25 * (yr + 4716)) + (int)(30.6001 * (mo + 1))
         + dy + h / 24.0 + B - 1524.5;
}

// Days since J2000.0 (2000-Jan-01 12:00 UTC)
static double sm_d2k(int yr, int mo, int dy, double h = 0.0) {
  return sm_jd(yr, mo, dy, h) - 2451545.0;
}

// Moon age in days since last new moon (0 = new, ~14.77 = full, ~29.53 = next new)
static double sm_moon_age(double jd) {
  const double T   = 29.53058853;
  const double ref = 2451549.5;  // known new moon: 2000-Jan-06
  double a = fmod(jd - ref, T);
  return a < 0 ? a + T : a;
}

static const char *sm_phase_name(double age) {
  double p = age / 29.53058853;
  if (p < 0.063) return "New Moon";
  if (p < 0.250) return "Wax. Crescent";
  if (p < 0.312) return "First Quarter";
  if (p < 0.500) return "Wax. Gibbous";
  if (p < 0.562) return "Full Moon";
  if (p < 0.750) return "Wan. Gibbous";
  if (p < 0.812) return "Last Quarter";
  return "Wan. Crescent";
}

// Moon ecliptic longitude & latitude using Paul Schlyter's algorithm
// d = days since J2000.0; results in degrees
static void sm_moon_ecl(double d, double &lon, double &lat) {
  const double toR = M_PI / 180.0;

  double N = sm_rev(125.1228 - 0.0529538083  * d);
  double w = sm_rev(318.0634 + 0.1643573223  * d);
  double M = sm_rev(115.3654 + 13.0649929509 * d);
  const double e = 0.054900;

  // Eccentric anomaly via Newton-Raphson on Kepler's equation
  double E = M + (180.0 / M_PI) * e * sin(M * toR) * (1.0 + e * cos(M * toR));
  for (int k = 0; k < 5; k++) {
    double Er = E * toR;
    E = E - (E - (180.0 / M_PI) * e * sin(Er) - M) / (1.0 - e * cos(Er));
  }

  double xv = cos(E * toR) - e;
  double yv = sqrt(1.0 - e * e) * sin(E * toR);
  double v   = sm_rev(atan2(yv, xv) * (180.0 / M_PI));
  double r   = sqrt(xv * xv + yv * yv);

  const double i_r = 5.1454 * toR;
  double N_r  = N * toR;
  double vw_r = (v + w) * toR;

  double xh = r * (cos(N_r) * cos(vw_r) - sin(N_r) * sin(vw_r) * cos(i_r));
  double yh = r * (sin(N_r) * cos(vw_r) + cos(N_r) * sin(vw_r) * cos(i_r));
  double zh = r * sin(vw_r) * sin(i_r);

  lon = sm_rev(atan2(yh, xh) * (180.0 / M_PI));
  lat = atan2(zh, sqrt(xh * xh + yh * yh)) * (180.0 / M_PI);

  // Perturbations (Paul Schlyter)
  double Ls = sm_rev(639.387  + 0.9856473678 * d);  // Sun mean longitude
  double Lm = sm_rev(N + w + M);                     // Moon mean longitude
  double Ms = sm_rev(356.0470 + 0.9856002585 * d);  // Sun mean anomaly
  double D  = sm_rev(Lm - Ls);
  double F  = sm_rev(Lm - N);

  lon += -1.274 * sin((M  - 2*D) * toR)
         +0.658 * sin((2*D)      * toR)
         -0.186 * sin( Ms        * toR)
         -0.059 * sin((2*M - 2*D)* toR)
         -0.057 * sin((M - 2*D + Ms) * toR)
         +0.053 * sin((M + 2*D)  * toR)
         +0.046 * sin((2*D - Ms) * toR)
         +0.041 * sin((M - Ms)   * toR)
         -0.035 * sin( D         * toR)
         -0.031 * sin((M + Ms)   * toR)
         -0.015 * sin((2*F - 2*D)* toR)
         +0.011 * sin((M - 4*D)  * toR);

  lat += -0.173 * sin((F - 2*D)      * toR)
         -0.055 * sin((M - F - 2*D)  * toR)
         -0.046 * sin((M + F - 2*D)  * toR)
         +0.033 * sin((F + 2*D)      * toR)
         +0.017 * sin((2*M + F)      * toR);
}

// Moon altitude above horizon (degrees)
// d = days since J2000, lat_deg = observer lat (N+), lon_deg = observer lon (E+, so W is negative)
static double sm_moon_alt(double d, double lat_deg, double lon_deg) {
  double elon, elat;
  sm_moon_ecl(d, elon, elat);

  const double toR = M_PI / 180.0;
  double obl   = (23.4393 - 3.563e-7 * d) * toR;
  double elon_r = elon * toR, elat_r = elat * toR;

  double xe = cos(elat_r) * cos(elon_r);
  double ye = cos(obl) * cos(elat_r) * sin(elon_r) - sin(obl) * sin(elat_r);
  double ze = sin(obl) * cos(elat_r) * sin(elon_r) + cos(obl) * sin(elat_r);

  double ra  = sm_rev(atan2(ye, xe) * (180.0 / M_PI));
  double dec = atan2(ze, sqrt(xe * xe + ye * ye)) * (180.0 / M_PI);

  double GMST = sm_rev(280.46061837 + 360.98564736629 * d);
  double LST  = sm_rev(GMST + lon_deg);
  double HA   = sm_rev(LST - ra) * toR;

  double la_r = lat_deg * toR, de_r = dec * toR;
  return asin(sin(la_r) * sin(de_r) + cos(la_r) * cos(de_r) * cos(HA)) * (180.0 / M_PI);
}

// Scan the given UTC day in 30-min steps to find moonrise and moonset.
// Returns -1,-1 when the moon does not rise or set that day (rare at mid-latitudes).
static void sm_moon_riseset(int yr, int mo, int dy, double lat, double lon,
                            int &rise_h, int &rise_m, int &set_h, int &set_m) {
  rise_h = rise_m = set_h = set_m = -1;
  const double step = 0.5 / 24.0;
  double d0   = sm_d2k(yr, mo, dy, 0.0);
  double prev = sm_moon_alt(d0, lat, lon);

  for (int s = 1; s <= 48; s++) {
    double cur = sm_moon_alt(d0 + s * step, lat, lon);

    if (prev < 0.0 && cur >= 0.0 && rise_h < 0) {
      double t = (s - 1.0 + (-prev / (cur - prev))) * step * 24.0;
      rise_h = (int)t;
      rise_m = (int)round((t - rise_h) * 60.0);
      if (rise_m >= 60) { rise_h++; rise_m = 0; }
    }
    if (prev >= 0.0 && cur < 0.0 && set_h < 0) {
      double t = (s - 1.0 + (prev / (prev - cur))) * step * 24.0;
      set_h = (int)t;
      set_m = (int)round((t - set_h) * 60.0);
      if (set_m >= 60) { set_h++; set_m = 0; }
    }
    prev = cur;
    if (rise_h >= 0 && set_h >= 0) break;
  }
}

// ── Moon phase circle ─────────────────────────────────────────────────────────
// Draws a moon at pixel (cx, cy) with radius R showing the correct illuminated portion.
// age = days since last new moon (0–29.53).
//
// Geometry: phase fraction p = age/29.53, phase angle = 2π*p
//   The terminator at row dy sits at x = cos(2π*p) * rx  (rx = half-chord at that row)
//   Waxing (p<0.5): right side is lit → pixel lit if dx > terminator
//   Waning (p≥0.5): left side is lit  → pixel lit if dx < −terminator
//
static void sm_draw_moon(int cx, int cy, int R, double age) {
  double p    = age / 29.53058853;
  double c2p  = cos(2.0 * M_PI * p);
  bool waxing = (p < 0.5);

  for (int dy = -R; dy <= R; dy++) {
    int   rx2 = R * R - dy * dy;
    if (rx2 < 0) continue;
    double rxf = sqrt((double)rx2);
    int    rx  = (int)rxf;
    if (rx == 0) continue;

    // Paint the full chord dark first
    gfx->drawFastHLine(cx - rx, cy + dy, 2 * rx + 1, 0x1082);

    // Terminator x-position for this row
    double term = c2p * rxf;

    // Calculate lit pixel range using floor() for correct rounding toward −∞
    int lit_x0, lit_w;
    if (waxing) {
      // lit if dx > term → first lit dx = floor(term) + 1
      int x0 = (int)floor(term) + 1;
      if (x0 > rx) continue;
      lit_x0 = cx + x0;
      lit_w  = rx - x0 + 1;
    } else {
      // lit if dx < -term → last lit dx = ceil(-term) - 1 = floor(-term - ε)
      int x1 = (int)floor(-term - 1e-9);
      if (x1 < -rx) continue;
      lit_x0 = cx - rx;
      lit_w  = x1 + rx + 1;
    }
    if (lit_w > 0) gfx->drawFastHLine(lit_x0, cy + dy, lit_w, RGB565_WHITE);
  }

  gfx->drawCircle(cx, cy, R, RGB565_WHITE);
}

// ── Main entry point ──────────────────────────────────────────────────────────
bool sunMoonFetchAndDisplay(const char *lat_str, const char *lon_str) {
  float lat = atof(lat_str);
  float lon = atof(lon_str);  // negative = West

  // ── Get current UTC time from NTP ─────────────────────────────────────────
  struct tm ti;
  if (!getLocalTime(&ti)) {
    Serial.println("[SunMoon] NTP time not available");
    return false;
  }
  int year  = ti.tm_year + 1900;
  int month = ti.tm_mon  + 1;
  int mday  = ti.tm_mday;

  // ── Sunrise/Sunset from sunrise-sunset.org ────────────────────────────────
  char sunUrl[128];
  snprintf(sunUrl, sizeof(sunUrl),
    "https://api.sunrise-sunset.org/json?lat=%.4f&lng=%.4f&date=today&formatted=0",
    lat, lon);
  String sunBody = sm_https_get(sunUrl);

  int sr_h = -1, sr_m = -1, ss_h = -1, ss_m = -1, noon_h = -1, noon_m = -1;
  if (!sunBody.isEmpty()) {
    StaticJsonDocument<128> filter;
    filter["results"]["sunrise"]    = true;
    filter["results"]["sunset"]     = true;
    filter["results"]["solar_noon"] = true;
    StaticJsonDocument<512> doc;
    if (!deserializeJson(doc, sunBody, DeserializationOption::Filter(filter))) {
      sm_parse_iso(doc["results"]["sunrise"]    | "", sr_h,   sr_m);
      sm_parse_iso(doc["results"]["sunset"]     | "", ss_h,   ss_m);
      sm_parse_iso(doc["results"]["solar_noon"] | "", noon_h, noon_m);
    }
  }

  // ── Moon phase ────────────────────────────────────────────────────────────
  double jd    = sm_jd(year, month, mday, (double)ti.tm_hour + ti.tm_min / 60.0);
  double age   = sm_moon_age(jd);
  double illum = 50.0 * (1.0 - cos(age / 29.53058853 * 2.0 * M_PI));  // 0–100%

  // ── Moonrise/Moonset (local calculation, ~30 trig calls × 48 steps) ───────
  int mr_h = -1, mr_m = -1, ms_h = -1, ms_m = -1;
  sm_moon_riseset(year, month, mday, (double)lat, (double)lon,
                  mr_h, mr_m, ms_h, ms_m);

  // ── Format all time strings ───────────────────────────────────────────────
  char sr_s[8], ss_s[8], noon_s[8], mr_s[8], ms_s[8];
  sm_fmt(sr_s,   sr_h,   sr_m);
  sm_fmt(ss_s,   ss_h,   ss_m);
  sm_fmt(noon_s, noon_h, noon_m);
  sm_fmt(mr_s,   mr_h,   mr_m);
  sm_fmt(ms_s,   ms_h,   ms_m);

  // ── Draw ──────────────────────────────────────────────────────────────────
  gfx->fillRect(0, 20, gfx->width(), gfx->height() - 20, RGB565_BLACK);
  gfx->setTextSize(1);

  // ─── SUN section (y 22–57) ────────────────────────────────────────────────
  gfx->setTextColor(0xFFE0);        // yellow header
  gfx->setCursor(4, 22);
  gfx->print("\x0F  SUN");          // note: most CYD fonts lack emoji; use ASCII

  gfx->setTextColor(0x07FF);        // cyan values
  gfx->setCursor(4, 34);
  gfx->print("Sunrise  "); gfx->print(sr_s); gfx->print(" UTC");

  gfx->setCursor(168, 34);
  gfx->print("Sunset   "); gfx->print(ss_s); gfx->print(" UTC");

  gfx->setTextColor(0x8410);        // dim gray for secondary row
  gfx->setCursor(4, 46);
  gfx->print("Solar Noon  ");
  gfx->setTextColor(0x07FF);
  gfx->print(noon_s); gfx->print(" UTC");

  gfx->drawFastHLine(0, 57, gfx->width(), 0x2104);

  // ─── MOON section (y 60–97) ───────────────────────────────────────────────
  gfx->setTextColor(0xFC60);        // amber header
  gfx->setCursor(4, 60);
  gfx->print("\x0E  MOON");

  gfx->setTextColor(0x07FF);
  gfx->setCursor(4, 72);
  gfx->print("Moonrise "); gfx->print(mr_s); gfx->print(" UTC");

  gfx->setCursor(165, 72);
  gfx->print("Moonset  "); gfx->print(ms_s); gfx->print(" UTC");

  // Phase name + illumination + age
  gfx->setTextColor(0xFFE0);
  gfx->setCursor(4, 84);
  gfx->print(sm_phase_name(age));

  char illum_buf[12];
  snprintf(illum_buf, sizeof(illum_buf), "  %.0f%%", illum);
  gfx->setTextColor(0xC618);        // light gray
  gfx->print(illum_buf);
  gfx->print(" lit");

  char age_buf[16];
  snprintf(age_buf, sizeof(age_buf), "  %.1fd", age);
  gfx->setTextColor(0x8410);
  gfx->print(age_buf);

  gfx->drawFastHLine(0, 96, gfx->width(), 0x2104);

  // ─── Moon phase circle ────────────────────────────────────────────────────
  const int moon_cx = 160, moon_cy = 172, moon_r = 38;
  sm_draw_moon(moon_cx, moon_cy, moon_r, age);

  // W / E orientation labels so the user knows which side is lit
  gfx->setTextColor(0x4208);        // very dim — just a hint
  gfx->setCursor(moon_cx - moon_r - 12, moon_cy - 3);
  gfx->print("W");
  gfx->setCursor(moon_cx + moon_r + 4, moon_cy - 3);
  gfx->print("E");

  // Phase label centered below circle
  const char *pname = sm_phase_name(age);
  int label_x = moon_cx - (strlen(pname) * 6) / 2;
  gfx->setTextColor(0x4208);
  gfx->setCursor(label_x, moon_cy + moon_r + 5);
  gfx->print(pname);

  Serial.printf("[SunMoon] SR=%s SS=%s Noon=%s MR=%s MS=%s Phase=%s %.0f%%\n",
    sr_s, ss_s, noon_s, mr_s, ms_s, sm_phase_name(age), illum);

  return true;
}
