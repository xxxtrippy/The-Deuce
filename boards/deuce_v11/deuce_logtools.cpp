// THE DEUCE v1.1 -- GPS logging tools. See .h.

#include "deuce_logtools.h"

#include <Arduino.h>
#include <SD.h>
#include <TinyGPS++.h>
#include <globals.h>
#include <math.h>

#include "core/display.h"
#include "core/sd_functions.h"
#include "deuce_mmc5983.h"
#include "deuce_sht41.h"
#include "modules/rf/rf_utils.h"

#if __has_include(<RF24.h>)
  #include <RF24.h>
  #define HAVE_NRF24 1
#endif
#if __has_include(<WiFi.h>)
  #include <WiFi.h>
  #define HAVE_WIFI_RSSI 1
#endif

// ── GPS reader (self-contained) ───────────────────────────────────────────────
static HardwareSerial GPSser(2);
static TinyGPSPlus gps;
static bool g_rxReleased = false;

static void gpsRelease() {
    g_rxReleased = false;
    if (bruceConfigPins.CC1101_bus.checkConflict(bruceConfigPins.gps_bus.rx) ||
        bruceConfigPins.NRF24_bus.checkConflict(bruceConfigPins.gps_bus.rx) ||
        bruceConfigPins.SDCARD_bus.checkConflict(bruceConfigPins.gps_bus.rx)) {
        pinMode(bruceConfigPins.gps_bus.rx, INPUT);
        g_rxReleased = true;
    }
}
static void gpsRestore() {
    if (!g_rxReleased) return;
    pinMode(bruceConfigPins.gps_bus.rx, OUTPUT);
    if (bruceConfigPins.gps_bus.rx == bruceConfigPins.CC1101_bus.cs ||
        bruceConfigPins.gps_bus.rx == bruceConfigPins.NRF24_bus.cs ||
        bruceConfigPins.gps_bus.rx == bruceConfigPins.SDCARD_bus.cs)
        digitalWrite(bruceConfigPins.gps_bus.rx, HIGH);
    else digitalWrite(bruceConfigPins.gps_bus.rx, LOW);
    g_rxReleased = false;
}
static void gpsBegin() {
#ifdef IO_EXP_GPS
    ioExpander.turnPinOnOff(IO_EXP_GPS, HIGH);
#endif
    gpsRelease();
    pinMode(bruceConfigPins.gps_bus.rx, INPUT);
    GPSser.begin(bruceConfigPins.gpsBaudrate, SERIAL_8N1, bruceConfigPins.gps_bus.rx,
                 bruceConfigPins.gps_bus.tx);
}
static void gpsEnd() {
    GPSser.end();
    gpsRestore();
#ifdef IO_EXP_GPS
    ioExpander.turnPinOnOff(IO_EXP_GPS, LOW);
#endif
}
static void gpsFeed() { while (GPSser.available() > 0) gps.encode(GPSser.read()); }

// ── helpers ───────────────────────────────────────────────────────────────────
static float magHeading() {
    float gx, gy, gz;
    if (!mmcPresent() || !mmcReadGauss(gx, gy, gz)) return -1;
    float h = atan2f(gy, gx) * RAD_TO_DEG; if (h < 0) h += 360.0f; return h;
}
static float magFieldUT() {
    float gx, gy, gz;
    if (!mmcPresent() || !mmcReadGauss(gx, gy, gz)) return -1;
    return sqrtf(gx * gx + gy * gy + gz * gz) * 100.0f;
}
static int wifiBestRSSI() {
#ifdef HAVE_WIFI_RSSI
    int n = WiFi.scanComplete();
    if (n == -2) { WiFi.scanNetworks(true, true, false, 120); return 0; }
    if (n < 0) return 0;
    int best = -100;
    for (int i = 0; i < n; i++) { int ch = WiFi.channel(i); if (ch >= 1 && ch <= 13) { int r = WiFi.RSSI(i); if (r > best) best = r; } }
    WiFi.scanDelete();
    WiFi.scanNetworks(true, true, false, 120);
    return best;
#else
    return 0;
#endif
}

static String nowStamp() {
    struct tm lt = rtc.getTimeStruct();
    char s[24];
    if (lt.tm_year > 70)
        snprintf(s, sizeof(s), "%04d-%02d-%02d %02d:%02d:%02d", lt.tm_year + 1900, lt.tm_mon + 1,
                 lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
    else if (gps.time.isValid())
        snprintf(s, sizeof(s), "%02d:%02d:%02dZ", gps.time.hour(), gps.time.minute(), gps.time.second());
    else
        snprintf(s, sizeof(s), "%lu", (unsigned long)millis());
    return String(s);
}

static bool logRow(const char *path, const String &header, const String &row) {
    if (!sdcardMounted) return false;
    SD.mkdir("/deuce");
    bool exists = SD.exists(path);
    File f = SD.open(path, FILE_APPEND);
    if (!f) return false;
    if (!exists) f.println(header);
    f.println(row);
    f.close();
    return true;
}

static void logToast(bool ok, int count) {
    int W = tft.width(), H = tft.height();
    tft.fillRect(0, H - 22, W, 10, bruceConfig.bgColor);
    tft.setTextColor(ok ? TFT_GREEN : TFT_RED, bruceConfig.bgColor);
    char s[40]; snprintf(s, sizeof(s), ok ? "logged #%d" : "SD error", count);
    tft.drawString(s, 4, H - 22);
}

// ── Site Survey ───────────────────────────────────────────────────────────────
void deuceSiteSurvey() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    gpsBegin();
#ifdef HAVE_WIFI_RSSI
    WiFi.mode(WIFI_STA); WiFi.disconnect(); WiFi.scanNetworks(true, true, false, 120);
#endif
    tft.fillScreen(bg);
    int count = 0; bool autolog = false; uint32_t lastAuto = 0, lastDraw = 0;
    const char *PATH = "/deuce/survey.csv";
    const String HDR = "time,lat,lng,alt_m,sats,tempF,rh,dewF,heading,field_uT,wifi_rssi";
    while (true) {
        if (check(EscPress)) break;
        gpsFeed();
        bool fix = gps.location.isValid();
        float tC = 0, rh = 0; bool env = sht41Read(tC, rh);
        float hd = magHeading(), fu = magFieldUT();
        int wr = wifiBestRSSI();
        bool doLog = false;
        if (check(SelPress)) doLog = true;
        if (check(NextPress)) { autolog = !autolog; lastAuto = millis(); }
        if (autolog && millis() - lastAuto > 3000) { doLog = true; lastAuto = millis(); }
        if (doLog && fix) {
            float tF = tC * 9.0f / 5.0f + 32.0f;
            float dp = tC - (100 - rh) / 5.0f; float dpF = dp * 9.0f / 5.0f + 32.0f;
            char row[200];
            snprintf(row, sizeof(row), "%s,%.6f,%.6f,%.0f,%d,%.1f,%.0f,%.1f,%.0f,%.0f,%d",
                     nowStamp().c_str(), gps.location.lat(), gps.location.lng(),
                     gps.altitude.meters(), gps.satellites.value(), env ? tF : 0, env ? rh : 0,
                     env ? dpF : 0, hd, fu, wr);
            bool ok = logRow(PATH, HDR, String(row));
            if (ok) count++;
            logToast(ok, count);
        }
        if (millis() - lastDraw < 400) { vTaskDelay(20 / portTICK_PERIOD_MS); continue; }
        lastDraw = millis();
        tft.fillRect(0, 0, W, 17, bg);
        tft.setTextColor(pri, bg); tft.drawCentreString("SITE SURVEY", W / 2, 4, 1);
        tft.drawFastHLine(0, 16, W, sec);
        tft.fillRect(0, 18, W, H - 46, bg);
        tft.setTextColor(sec, bg);
        char l[48];
        if (fix) snprintf(l, sizeof(l), "%.5f, %.5f  s%d", gps.location.lat(), gps.location.lng(), gps.satellites.value());
        else snprintf(l, sizeof(l), "waiting fix... sats %d", gps.satellites.value());
        tft.drawString(l, 6, 24);
        if (env) { snprintf(l, sizeof(l), "%.0fF  %.0f%%RH", tC * 9.0f / 5.0f + 32.0f, rh); tft.drawString(l, 6, 40); }
        if (hd >= 0) { snprintf(l, sizeof(l), "hdg %d  field %.0f uT", (int)hd, fu); tft.drawString(l, 6, 54); }
        snprintf(l, sizeof(l), "wifi %d dBm", wr); tft.drawString(l, 6, 68);
        tft.setTextColor(autolog ? TFT_GREEN : sec, bg);
        snprintf(l, sizeof(l), "auto-log %s (3s)", autolog ? "ON" : "off"); tft.drawString(l, 6, 84);
        tft.setTextColor(sec, bg);
        tft.drawString("[SEL] log  [NEXT] auto  [ESC]", 4, H - 11);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
#ifdef HAVE_WIFI_RSSI
    WiFi.scanDelete(); WiFi.mode(WIFI_OFF);
#endif
    gpsEnd();
}

// ── RF Coverage Mapper ────────────────────────────────────────────────────────
#ifdef HAVE_NRF24
extern RF24 radio;
#endif

#define RFM_TRAIL 120
static float *g_tLat = nullptr, *g_tLng = nullptr; static int *g_tRssi = nullptr; // heap while open
static int g_tn = 0, g_th = 0;

static uint16_t rssiColor(int r) {
    if (r > -55) return TFT_GREEN;
    if (r > -70) return TFT_YELLOW;
    if (r > -85) return TFT_ORANGE;
    return TFT_RED;
}

void deuceRFMap() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;

    float ccFreq = 433.92f;
    options = {
        {"433.92 MHz", [&]() { ccFreq = 433.92f; }},
        {"315 MHz",    [&]() { ccFreq = 315.00f; }},
        {"868 MHz",    [&]() { ccFreq = 868.35f; }},
        {"915 MHz",    [&]() { ccFreq = 915.00f; }},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "CC1101 band");

    g_tLat = (float *)malloc(RFM_TRAIL * sizeof(float));
    g_tLng = (float *)malloc(RFM_TRAIL * sizeof(float));
    g_tRssi = (int *)malloc(RFM_TRAIL * sizeof(int));
    if (!g_tLat || !g_tLng || !g_tRssi) {
        free(g_tLat); free(g_tLng); free(g_tRssi);
        g_tLat = g_tLng = nullptr; g_tRssi = nullptr;
        displayError("Out of memory", true); return;
    }
    g_tn = 0; g_th = 0;

    gpsBegin();
    bool ccOk = (bruceConfigPins.rfModule == CC1101_SPI_MODULE) && initRfModule("rx", ccFreq);
    if (ccOk) setMHZ(ccFreq);
#ifdef HAVE_WIFI_RSSI
    WiFi.mode(WIFI_STA); WiFi.disconnect(); WiFi.scanNetworks(true, true, false, 120);
#endif
#ifdef HAVE_NRF24
    bool nrfOk = radio.begin() && radio.isChipConnected();
    if (nrfOk) { radio.setChannel(76); radio.startListening(); }
#endif
    g_tn = 0; g_th = 0;
    tft.fillScreen(bg);
    int count = 0; bool autolog = false; uint32_t lastAuto = 0, lastDraw = 0;
    const char *PATH = "/deuce/rfmap.csv";
    const String HDR = "time,lat,lng,sats,cc_rssi,wifi_rssi,nrf_cd";
    while (true) {
        if (check(EscPress)) break;
        gpsFeed();
        bool fix = gps.location.isValid();
        int cc = ccOk ? ELECHOUSE_cc1101.getRssi() : 0;
        int wr = wifiBestRSSI();
        int nd = 0;
#ifdef HAVE_NRF24
        if (nrfOk) { int hits = 0; for (int i = 0; i < 10; i++) { if (radio.testCarrier()) hits++; delayMicroseconds(300); } nd = hits * 10; }
#endif
        bool doLog = false;
        if (check(SelPress)) doLog = true;
        if (check(NextPress)) { autolog = !autolog; lastAuto = millis(); }
        if (autolog && millis() - lastAuto > 2000) { doLog = true; lastAuto = millis(); }
        if (doLog && fix) {
            char row[160];
            snprintf(row, sizeof(row), "%s,%.6f,%.6f,%d,%d,%d,%d", nowStamp().c_str(),
                     gps.location.lat(), gps.location.lng(), gps.satellites.value(), cc, wr, nd);
            bool ok = logRow(PATH, HDR, String(row));
            if (ok) {
                count++;
                g_tLat[g_th] = gps.location.lat(); g_tLng[g_th] = gps.location.lng();
                g_tRssi[g_th] = ccOk ? cc : wr; g_th = (g_th + 1) % RFM_TRAIL; if (g_tn < RFM_TRAIL) g_tn++;
            }
            logToast(ok, count);
        }
        if (millis() - lastDraw < 300) { vTaskDelay(20 / portTICK_PERIOD_MS); continue; }
        lastDraw = millis();
        tft.fillRect(0, 0, W, 17, bg);
        tft.setTextColor(pri, bg); tft.drawCentreString("RF COVERAGE MAP", W / 2, 4, 1);
        tft.drawFastHLine(0, 16, W, sec);
        tft.fillRect(0, 18, W, H - 46, bg);
        // live RSSI lines
        tft.setTextColor(sec, bg);
        char l[48];
        snprintf(l, sizeof(l), "CC %d  WiFi %d  NRF %d%%", cc, wr, nd); tft.drawString(l, 6, 22);
        if (fix) snprintf(l, sizeof(l), "%.5f,%.5f s%d", gps.location.lat(), gps.location.lng(), gps.satellites.value());
        else snprintf(l, sizeof(l), "waiting fix... s%d", gps.satellites.value());
        tft.drawString(l, 6, 34);
        // auto-scaled coloured trail map
        if (g_tn >= 2) {
            float la0 = 1e9, la1 = -1e9, lo0 = 1e9, lo1 = -1e9;
            for (int i = 0; i < g_tn; i++) {
                int idx = (g_th - g_tn + i + RFM_TRAIL) % RFM_TRAIL;
                la0 = fmin(la0, g_tLat[idx]); la1 = fmax(la1, g_tLat[idx]);
                lo0 = fmin(lo0, g_tLng[idx]); lo1 = fmax(lo1, g_tLng[idx]);
            }
            int mx = 10, my = 48, mw = W - 20, mh = H - my - 16;
            tft.drawRect(mx, my, mw, mh, sec);
            float dla = la1 - la0, dlo = lo1 - lo0; if (dla < 1e-7) dla = 1e-7; if (dlo < 1e-7) dlo = 1e-7;
            for (int i = 0; i < g_tn; i++) {
                int idx = (g_th - g_tn + i + RFM_TRAIL) % RFM_TRAIL;
                int px = mx + 2 + (int)((g_tLng[idx] - lo0) / dlo * (mw - 4));
                int py = my + mh - 2 - (int)((g_tLat[idx] - la0) / dla * (mh - 4));
                tft.fillCircle(px, py, 2, rssiColor(g_tRssi[idx]));
            }
        } else {
            tft.setTextColor(sec, bg);
            tft.drawString("log points to build the map", 10, 52);
        }
        tft.setTextColor(autolog ? TFT_GREEN : sec, bg);
        snprintf(l, sizeof(l), "auto %s", autolog ? "ON" : "off"); tft.drawString(l, W - 4 - tft.textWidth(l), 22);
        tft.setTextColor(sec, bg);
        tft.drawString("[SEL]log [NEXT]auto [ESC]", 4, H - 11);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
#ifdef HAVE_WIFI_RSSI
    WiFi.scanDelete(); WiFi.mode(WIFI_OFF);
#endif
    if (ccOk) deinitRfModule();
    gpsEnd();
    free(g_tLat); free(g_tLng); free(g_tRssi);
    g_tLat = g_tLng = nullptr; g_tRssi = nullptr;
}
