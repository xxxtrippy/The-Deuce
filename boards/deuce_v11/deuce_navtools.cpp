// THE DEUCE v1.1 -- GPS navigation tools. See .h.

#include "deuce_navtools.h"

#include <Arduino.h>
#include <TinyGPS++.h>
#include <globals.h>
#include <math.h>

#include "core/display.h"
#include "deuce_mmc5983.h"
#include "deuce_sht41.h"

// ── shared GPS reader (mirrors the GPS tracker init) ──────────────────────────
static HardwareSerial GPSser(2);
static TinyGPSPlus gps;
static bool g_rxReleased = false;

static void navReleasePins() {
    g_rxReleased = false;
    if (bruceConfigPins.CC1101_bus.checkConflict(bruceConfigPins.gps_bus.rx) ||
        bruceConfigPins.NRF24_bus.checkConflict(bruceConfigPins.gps_bus.rx) ||
        bruceConfigPins.SDCARD_bus.checkConflict(bruceConfigPins.gps_bus.rx)) {
        pinMode(bruceConfigPins.gps_bus.rx, INPUT);
        g_rxReleased = true;
    }
}
static void navRestorePins() {
    if (!g_rxReleased) return;
    pinMode(bruceConfigPins.gps_bus.rx, OUTPUT);
    if (bruceConfigPins.gps_bus.rx == bruceConfigPins.CC1101_bus.cs ||
        bruceConfigPins.gps_bus.rx == bruceConfigPins.NRF24_bus.cs ||
        bruceConfigPins.gps_bus.rx == bruceConfigPins.SDCARD_bus.cs)
        digitalWrite(bruceConfigPins.gps_bus.rx, HIGH);
    else
        digitalWrite(bruceConfigPins.gps_bus.rx, LOW);
    g_rxReleased = false;
}

static void navGpsBegin() {
#ifdef IO_EXP_GPS
    ioExpander.turnPinOnOff(IO_EXP_GPS, HIGH);
#endif
    navReleasePins();
    pinMode(bruceConfigPins.gps_bus.rx, INPUT);
    GPSser.begin(bruceConfigPins.gpsBaudrate, SERIAL_8N1, bruceConfigPins.gps_bus.rx,
                 bruceConfigPins.gps_bus.tx);
}
static void navGpsEnd() {
    GPSser.end();
    navRestorePins();
#ifdef IO_EXP_GPS
    ioExpander.turnPinOnOff(IO_EXP_GPS, LOW);
#endif
}
static void navGpsFeed() { while (GPSser.available() > 0) gps.encode(GPSser.read()); }

// ── heading from the magnetometer (0 if absent) ───────────────────────────────
static bool navHeading(float &hdg) {
    float gx, gy, gz;
    if (!mmcPresent() || !mmcReadGauss(gx, gy, gz)) return false;
    hdg = atan2f(gy, gx) * RAD_TO_DEG;
    if (hdg < 0) hdg += 360.0f;
    return true;
}

// ── geo helpers ───────────────────────────────────────────────────────────────
static double haversine(double la1, double lo1, double la2, double lo2) {
    double R = 6371000.0, p = M_PI / 180.0;
    double dla = (la2 - la1) * p, dlo = (lo2 - lo1) * p;
    double a = sin(dla / 2) * sin(dla / 2) +
               cos(la1 * p) * cos(la2 * p) * sin(dlo / 2) * sin(dlo / 2);
    return 2 * R * atan2(sqrt(a), sqrt(1 - a));
}
static double bearingTo(double la1, double lo1, double la2, double lo2) {
    double p = M_PI / 180.0;
    double y = sin((lo2 - lo1) * p) * cos(la2 * p);
    double x = cos(la1 * p) * sin(la2 * p) -
               sin(la1 * p) * cos(la2 * p) * cos((lo2 - lo1) * p);
    double b = atan2(y, x) * 180.0 / M_PI;
    if (b < 0) b += 360;
    return b;
}

// ── shared UI bits ────────────────────────────────────────────────────────────
static void navTitle(const char *t) {
    int W = tft.width();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    tft.fillRect(0, 0, W, 17, bg);
    tft.setTextSize(1);
    tft.setTextColor(pri, bg);
    tft.drawCentreString(t, W / 2, 4, 1);
    tft.drawFastHLine(0, 16, W, sec);
}
// a direction arrow (deg, 0=up/forward) inside a circle at cx,cy,R
static void navArrow(int cx, int cy, int R, float deg, uint16_t col, const char *tag) {
    uint16_t sec = bruceConfig.secColor, bg = bruceConfig.bgColor;
    tft.drawCircle(cx, cy, R, sec);
    tft.fillTriangle(cx - 4, 18 + (cy - R - 18) / 2, cx + 4, 18 + (cy - R - 18) / 2, cx,
                     24 + (cy - R - 18) / 2, sec); // tiny forward tick up top (device fwd)
    float a = deg * DEG_TO_RAD;
    int tx = cx + (int)((R - 8) * sinf(a));
    int ty = cy - (int)((R - 8) * cosf(a));
    tft.drawLine(cx, cy, tx, ty, col);
    // arrowhead
    float a1 = (deg + 150) * DEG_TO_RAD, a2 = (deg - 150) * DEG_TO_RAD;
    tft.drawLine(tx, ty, tx + (int)(8 * sinf(a1)), ty - (int)(8 * cosf(a1)), col);
    tft.drawLine(tx, ty, tx + (int)(8 * sinf(a2)), ty - (int)(8 * cosf(a2)), col);
    tft.fillCircle(cx, cy, 3, col);
    if (tag) { tft.setTextColor(col, bg); tft.drawCentreString(tag, cx, cy + R + 2, 1); }
}

static void navWaitFix(const char *title) {
    navTitle(title);
    int W = tft.width(), H = tft.height();
    tft.setTextColor(bruceConfig.secColor, bruceConfig.bgColor);
    char s[32]; snprintf(s, sizeof(s), "Sats %d  waiting fix...", gps.satellites.value());
    tft.fillRect(0, H / 2 - 6, W, 12, bruceConfig.bgColor);
    tft.drawCentreString(s, W / 2, H / 2 - 4, 1);
}

// ── Waypoint Navigator ────────────────────────────────────────────────────────
void deuceWaypointNav() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    navGpsBegin();
    tft.fillScreen(bg);
    bool haveTarget = false;
    double tLat = 0, tLng = 0;
    uint32_t lastDraw = 0;
    while (true) {
        if (check(EscPress)) break;
        navGpsFeed();
        bool fix = gps.location.isValid();
        if (check(SelPress) && fix) { tLat = gps.location.lat(); tLng = gps.location.lng(); haveTarget = true; tft.fillScreen(bg); }
        if (millis() - lastDraw < 250) { vTaskDelay(20 / portTICK_PERIOD_MS); continue; }
        lastDraw = millis();
        if (!fix) { navWaitFix("WAYPOINT"); continue; }
        navTitle("WAYPOINT");
        double cLat = gps.location.lat(), cLng = gps.location.lng();
        float hdg = 0; bool haveH = navHeading(hdg);
        int cx = W - 60, cy = 18 + (H - 30) / 2, R = (H - 46) / 2;
        tft.fillRect(0, 18, W, H - 30, bg);
        if (!haveTarget) {
            tft.setTextColor(pri, bg);
            tft.drawString("[SEL] mark target here", 8, 40);
            tft.setTextColor(sec, bg);
            char c[40]; snprintf(c, sizeof(c), "%.5f, %.5f", cLat, cLng);
            tft.drawString(c, 8, 60);
            snprintf(c, sizeof(c), "Sats %d", gps.satellites.value());
            tft.drawString(c, 8, 80);
        } else {
            double dist = haversine(cLat, cLng, tLat, tLng);
            double brg = bearingTo(cLat, cLng, tLat, tLng);
            float rel = haveH ? (float)(brg - hdg) : (float)brg;
            navArrow(cx, cy, R, rel, pri, haveH ? "to target" : "brg");
            tft.setTextColor(pri, bg);
            tft.setTextSize(2);
            char d[20];
            if (dist < 1000) snprintf(d, sizeof(d), "%dm", (int)dist);
            else snprintf(d, sizeof(d), "%.2fkm", dist / 1000.0);
            tft.drawString(d, 8, 30);
            tft.setTextSize(1);
            tft.setTextColor(sec, bg);
            char l[40];
            snprintf(l, sizeof(l), "brg %d%s", (int)brg, haveH ? "" : " (mag n/a)"); tft.drawString(l, 8, 54);
            snprintf(l, sizeof(l), "hdg %d", (int)hdg); if (haveH) tft.drawString(l, 8, 66);
            snprintf(l, sizeof(l), "tgt %.5f,%.5f", tLat, tLng); tft.drawString(l, 8, 80);
            snprintf(l, sizeof(l), "spd %.1f km/h  sat %d", gps.speed.kmph(), gps.satellites.value());
            tft.drawString(l, 8, 92);
            tft.setTextColor(sec, bg);
            tft.drawString("[SEL] re-mark   [ESC]", 4, H - 11);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    navGpsEnd();
}

// ── solar position (NOAA approximation) ───────────────────────────────────────
static void solarPos(double lat, double lon, int y, int mo, int d, double utcH, double &az,
                     double &el, double &sunrise, double &sunset) {
    static const int cum[] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    int doy = cum[(mo - 1) % 12] + d;
    if (mo > 2 && (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0))) doy++;
    double gamma = 2 * M_PI / 365.0 * (doy - 1 + (utcH - 12) / 24.0);
    double eqt = 229.18 * (0.000075 + 0.001868 * cos(gamma) - 0.032077 * sin(gamma) -
                           0.014615 * cos(2 * gamma) - 0.040849 * sin(2 * gamma));
    double decl = 0.006918 - 0.399912 * cos(gamma) + 0.070257 * sin(gamma) -
                  0.006758 * cos(2 * gamma) + 0.000907 * sin(2 * gamma) -
                  0.002697 * cos(3 * gamma) + 0.00148 * sin(3 * gamma);
    double tst = utcH * 60 + eqt + 4 * lon;
    double ha = (tst / 4.0) - 180.0;
    double latr = lat * M_PI / 180, har = ha * M_PI / 180;
    double cosZ = sin(latr) * sin(decl) + cos(latr) * cos(decl) * cos(har);
    cosZ = fmax(-1.0, fmin(1.0, cosZ));
    double zen = acos(cosZ);
    el = 90 - zen * 180 / M_PI;
    double den = cos(latr) * sin(zen);
    double azr = (fabs(den) < 1e-6) ? 0 : acos(fmax(-1.0, fmin(1.0, (sin(decl) - sin(latr) * cos(zen)) / den)));
    az = azr * 180 / M_PI;
    if (ha > 0) az = 360 - az;
    double cosH = cos(90.833 * M_PI / 180) / (cos(latr) * cos(decl)) - tan(latr) * tan(decl);
    if (cosH >= -1 && cosH <= 1) {
        double Hh = acos(cosH) * 180 / M_PI;
        sunrise = (720 - 4 * (lon + Hh) - eqt) / 60.0;
        sunset = (720 - 4 * (lon - Hh) - eqt) / 60.0;
    } else { sunrise = -1; sunset = -1; }
}

static void fmtHours(double h, int tzOff, char *out, int n) {
    if (h < 0) { snprintf(out, n, "--:--"); return; }
    h += tzOff;
    while (h < 0) h += 24; while (h >= 24) h -= 24;
    int hh = (int)h, mm = (int)((h - hh) * 60);
    snprintf(out, n, "%02d:%02d", hh, mm);
}

// ── Sun Finder ────────────────────────────────────────────────────────────────
void deuceSunFinder() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    navGpsBegin();
    tft.fillScreen(bg);
    uint32_t lastDraw = 0;
    while (true) {
        if (check(EscPress)) break;
        navGpsFeed();
        if (millis() - lastDraw < 400) { vTaskDelay(20 / portTICK_PERIOD_MS); continue; }
        lastDraw = millis();
        if (!gps.location.isValid() || !gps.time.isValid() || !gps.date.isValid()) { navWaitFix("SUN FINDER"); continue; }
        navTitle("SUN FINDER");
        double lat = gps.location.lat(), lon = gps.location.lng();
        double utcH = gps.time.hour() + gps.time.minute() / 60.0 + gps.time.second() / 3600.0;
        double az, el, sr, ss;
        solarPos(lat, lon, gps.date.year(), gps.date.month(), gps.date.day(), utcH, az, el, sr, ss);
        // tz offset from RTC local vs GPS UTC (best effort)
        int tzOff = 0;
        {
            struct tm lt = rtc.getTimeStruct();
            if (lt.tm_year > 70) {   // clock has been set
                tzOff = (int)lroundf((lt.tm_hour + lt.tm_min / 60.0f) - (float)utcH);
                if (tzOff > 14) tzOff -= 24;
                if (tzOff < -14) tzOff += 24;
            }
        }
        float hdg = 0; bool haveH = navHeading(hdg);
        int cx = W - 60, cy = 18 + (H - 30) / 2, R = (H - 46) / 2;
        tft.fillRect(0, 18, W, H - 30, bg);
        float rel = haveH ? (float)(az - hdg) : (float)az;
        navArrow(cx, cy, R, rel, TFT_YELLOW, el > 0 ? "to sun" : "below");
        tft.setTextColor(pri, bg);
        char l[40];
        snprintf(l, sizeof(l), "Az %3d  El %+3d", (int)az, (int)el); tft.drawString(l, 8, 30);
        tft.setTextColor(sec, bg);
        char sr1[8], ss1[8]; fmtHours(sr, tzOff, sr1, 8); fmtHours(ss, tzOff, ss1, 8);
        snprintf(l, sizeof(l), "Rise %s  Set %s", sr1, ss1); tft.drawString(l, 8, 50);
        snprintf(l, sizeof(l), "%s", el > 0 ? "sun is up" : "sun is down"); tft.drawString(l, 8, 64);
        if (!haveH) tft.drawString("(mag n/a: az is true)", 8, 78);
        snprintf(l, sizeof(l), "%.4f,%.4f tz%+d", lat, lon, tzOff); tft.drawString(l, 8, 92);
        tft.drawString("[ESC]", 4, H - 11);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    navGpsEnd();
}

// ── Trip Computer ─────────────────────────────────────────────────────────────
void deuceTripComputer() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    navGpsBegin();
    tft.fillScreen(bg);
    double trip = 0, pLat = 0, pLng = 0; bool havePrev = false;
    float vmax = 0;
    uint32_t lastDraw = 0;
    while (true) {
        if (check(EscPress)) break;
        if (check(SelPress)) { trip = 0; vmax = 0; havePrev = false; }
        navGpsFeed();
        bool fix = gps.location.isValid();
        if (fix && gps.location.isUpdated()) {
            double la = gps.location.lat(), lo = gps.location.lng();
            if (havePrev) { double d = haversine(pLat, pLng, la, lo); if (d < 1000) trip += d; }
            pLat = la; pLng = lo; havePrev = true;
        }
        if (millis() - lastDraw < 400) { vTaskDelay(20 / portTICK_PERIOD_MS); continue; }
        lastDraw = millis();
        navTitle("TRIP COMPUTER");
        tft.fillRect(0, 18, W, H - 30, bg);
        if (!fix) { navWaitFix("TRIP COMPUTER"); continue; }
        float spd = gps.speed.kmph(); if (spd > vmax) vmax = spd;
        float hdg = 0; bool haveH = navHeading(hdg);
        float course = gps.course.isValid() ? gps.course.deg() : (haveH ? hdg : 0);
        // big speed
        tft.setTextColor(pri, bg); tft.setTextSize(3);
        char s[16]; snprintf(s, sizeof(s), "%.0f", spd);
        tft.drawString(s, 8, 22);
        tft.setTextSize(1); tft.drawString("km/h", 8 + tft.textWidth(s) * 3 + 2, 22);
        tft.setTextColor(sec, bg);
        char l[40];
        snprintf(l, sizeof(l), "max %.0f", vmax); tft.drawString(l, W - 4 - tft.textWidth(l), 24);
        snprintf(l, sizeof(l), "trip %.2f km", trip / 1000.0); tft.drawString(l, 8, 54);
        snprintf(l, sizeof(l), "head %d %c", (int)course, haveH ? 'M' : 'G'); tft.drawString(l, 8, 68);
        snprintf(l, sizeof(l), "alt %.0fm  sat %d", gps.altitude.meters(), gps.satellites.value());
        tft.drawString(l, 8, 82);
        {
            struct tm lt = rtc.getTimeStruct();
            if (lt.tm_year > 70) {
                snprintf(l, sizeof(l), "%02d:%02d", lt.tm_hour, lt.tm_min);
                tft.drawString(l, 8, 96);
            }
        }
        float tC;
        if (sht41CachedTempC(tC)) {
            snprintf(l, sizeof(l), "cabin %d%c", (int)(tC * 9.0f / 5.0f + 32.0f + 0.5f), (char)247);
            tft.setTextColor(TFT_ORANGE, bg); tft.drawString(l, W - 4 - tft.textWidth(l), 96);
        }
        tft.setTextColor(sec, bg);
        tft.drawString("[SEL] reset  [ESC]", 4, H - 11);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    navGpsEnd();
}
