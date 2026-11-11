// THE DEUCE v1.1 -- logging / security combo tools. See .h.

#include "deuce_envtools.h"

#include <Arduino.h>
#include <SD.h>
#include <globals.h>
#include <math.h>

#include "core/bus_HAL.h"
#include "core/display.h"
#include "core/sd_functions.h"
#include "deuce_mmc5983.h"
#include "deuce_sht41.h"
#include "modules/rfid/PN532.h"

// ── shared SD / time helpers ──────────────────────────────────────────────────
static String nowStamp() {
    struct tm lt = rtc.getTimeStruct();
    char s[24];
    if (lt.tm_year > 70)
        snprintf(s, sizeof(s), "%04d-%02d-%02d %02d:%02d:%02d", lt.tm_year + 1900, lt.tm_mon + 1,
                 lt.tm_mday, lt.tm_hour, lt.tm_min, lt.tm_sec);
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
static void toast(const char *msg, uint16_t col) {
    int W = tft.width(), H = tft.height();
    tft.fillRect(0, H - 22, W, 10, bruceConfig.bgColor);
    tft.setTextColor(col, bruceConfig.bgColor);
    tft.drawString(msg, 4, H - 22);
}
static void title(const char *t) {
    int W = tft.width();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    tft.fillRect(0, 0, W, 17, bg);
    tft.setTextSize(1);
    tft.setTextColor(pri, bg);
    tft.drawCentreString(t, W / 2, 4, 1);
    tft.drawFastHLine(0, 16, W, sec);
}

// ── Weather Log ───────────────────────────────────────────────────────────────
#define WX_HIST 160
static float g_wx[WX_HIST]; static int g_wn = 0, g_wh = 0;

void deuceWeatherLog() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    if (!sht41Present() && !sht41Begin()) { displayError("No SHT41 found", true); return; }
    const char *PATH = "/deuce/weather.csv";
    const String HDR = "time,tempF,rh,dewF";
    g_wn = 0; g_wh = 0;
    float tmin = 1e9, tmax = -1e9;
    bool autolog = true;
    uint32_t interval = 60000, lastLog = 0, lastDraw = 0;
    int count = 0;
    tft.fillScreen(bg);
    while (true) {
        if (check(EscPress)) break;
        float tC, rh;
        bool ok = sht41Read(tC, rh);
        if (check(NextPress)) autolog = !autolog;
        bool doLog = check(SelPress);
        if (autolog && millis() - lastLog > interval) doLog = true;
        if (doLog && ok) {
            lastLog = millis();
            float tF = tC * 9.0f / 5.0f + 32.0f;
            float dp = tC - (100 - rh) / 5.0f, dpF = dp * 9.0f / 5.0f + 32.0f;
            char row[80]; snprintf(row, sizeof(row), "%s,%.1f,%.0f,%.1f", nowStamp().c_str(), tF, rh, dpF);
            bool w = logRow(PATH, HDR, String(row));
            if (w) { count++; g_wx[g_wh] = tF; g_wh = (g_wh + 1) % WX_HIST; if (g_wn < WX_HIST) g_wn++; }
            if (tF < tmin) tmin = tF; if (tF > tmax) tmax = tF;
            toast(w ? "logged" : "SD error", w ? TFT_GREEN : TFT_RED);
        }
        if (millis() - lastDraw < 500) { vTaskDelay(20 / portTICK_PERIOD_MS); continue; }
        lastDraw = millis();
        title("WEATHER LOG");
        tft.fillRect(0, 18, W, H - 46, bg);
        if (ok) {
            tft.setTextColor(TFT_ORANGE, bg); tft.setTextSize(3);
            char s[12]; snprintf(s, sizeof(s), "%.0f", tC * 9.0f / 5.0f + 32.0f);
            tft.drawString(s, 8, 22); tft.setTextSize(1);
            tft.drawString("F", 8 + tft.textWidth(s) * 3 + 2, 22);
            tft.setTextColor(TFT_CYAN, bg);
            snprintf(s, sizeof(s), "%.0f%%", rh); tft.drawString(s, W / 2, 30);
            tft.setTextColor(sec, bg);
            char l[40];
            if (tmax > -1e8) { snprintf(l, sizeof(l), "min %.0f  max %.0f", tmin, tmax); tft.drawString(l, 8, 50); }
        }
        tft.setTextColor(autolog ? TFT_GREEN : sec, bg);
        char l[40]; snprintf(l, sizeof(l), "auto-log %s 60s  #%d", autolog ? "ON" : "off", count);
        tft.drawString(l, 8, 62);
        // trend graph
        if (g_wn >= 2) {
            int gy = 78, gh = H - gy - 14;
            tft.drawRect(4, gy, W - 8, gh, sec);
            float lo = 1e9, hi = -1e9;
            for (int i = 0; i < g_wn; i++) { int idx = (g_wh - g_wn + i + WX_HIST) % WX_HIST; lo = fmin(lo, g_wx[idx]); hi = fmax(hi, g_wx[idx]); }
            if (hi - lo < 1) hi = lo + 1;
            int px = 0, py = 0;
            for (int i = 0; i < g_wn; i++) {
                int idx = (g_wh - g_wn + i + WX_HIST) % WX_HIST;
                int x = 6 + (int)((float)i / (WX_HIST - 1) * (W - 12));
                int y = gy + gh - 2 - (int)((g_wx[idx] - lo) / (hi - lo) * (gh - 4));
                if (i > 0) tft.drawLine(px, py, x, y, TFT_ORANGE); px = x; py = y;
            }
        }
        tft.setTextColor(sec, bg);
        tft.drawString("[SEL]log now [NEXT]auto [ESC]", 4, H - 11);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// ── Security Tripwire ─────────────────────────────────────────────────────────
void deuceTripwire() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    if (!mmcPresent() && !mmcBegin()) { displayError("No MMC5983 found", true); return; }
    const char *PATH = "/deuce/tripwire.csv";
    const String HDR = "time,deviation_uT,field_uT";
    const float THRESH = 20.0f;
    bool armed = false, tripped = false;
    float base = 50; bool baseSet = false;
    int trips = 0; String lastTrip = "--";
    uint32_t trippedAt = 0, lastDraw = 0;
    tft.fillScreen(bg);
    while (true) {
        if (check(EscPress)) break;
        if (check(SelPress)) { armed = !armed; if (armed) { tripped = false; } }
        if (check(NextPress)) baseSet = false;
        float gx, gy, gz; float field = base, dev = 0;
        if (mmcReadGauss(gx, gy, gz)) {
            field = sqrtf(gx * gx + gy * gy + gz * gz) * 100.0f;
            if (!baseSet) { base = field; baseSet = true; }
            dev = field - base;
            if (armed && !tripped && fabsf(dev) > THRESH) {
                tripped = true; trippedAt = millis(); trips++;
                lastTrip = nowStamp();
                char row[80]; snprintf(row, sizeof(row), "%s,%.0f,%.0f", lastTrip.c_str(), dev, field);
                logRow(PATH, HDR, String(row));
            }
        }
        if (tripped && millis() - trippedAt > 4000) tripped = false; // auto re-arm window
        if (millis() - lastDraw < 120) { vTaskDelay(10 / portTICK_PERIOD_MS); continue; }
        lastDraw = millis();
        uint16_t state = tripped ? TFT_RED : (armed ? TFT_GREEN : sec);
        title("TRIPWIRE");
        tft.fillRect(0, 18, W, H - 46, bg);
        if (tripped) tft.drawRect(0, 17, W, H - 17, TFT_RED);   // red border alert
        tft.setTextColor(state, bg); tft.setTextSize(2);
        tft.drawCentreString(tripped ? "** TRIPPED **" : (armed ? "ARMED" : "DISARMED"), W / 2, 26, 1);
        tft.setTextSize(1);
        tft.setTextColor(sec, bg);
        char l[48];
        snprintf(l, sizeof(l), "field %.0f uT  dev %+.0f", field, dev); tft.drawCentreString(l, W / 2, 50, 1);
        // deviation bar
        int by = 66, bh = 16, bx = 14, bw = W - 28, mid = bx + bw / 2;
        tft.drawRect(bx, by, bw, bh, sec); tft.drawFastVLine(mid, by - 3, bh + 6, sec);
        int len = (int)(dev / 80.0f * (bw / 2)); len = constrain(len, -bw / 2, bw / 2);
        tft.fillRect(len >= 0 ? mid : mid + len, by + 2, abs(len), bh - 4, fabsf(dev) > THRESH ? TFT_RED : TFT_GREEN);
        snprintf(l, sizeof(l), "trips %d   last %s", trips, lastTrip.c_str());
        tft.setTextColor(sec, bg); tft.drawString(l, 6, H - 24);
        tft.drawString("[SEL]arm [NEXT]rebase [ESC]", 4, H - 11);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// ── NFC Access Log ────────────────────────────────────────────────────────────
void deuceNfcLog() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    RFIDInterface *rfid;
    switch (bruceConfigPins.rfidModule) {
        case PN532_I2C_SPI_MODULE: rfid = new PN532(PN532::CONNECTION_TYPE::I2C_SPI); break;
        case PN532_SPI_MODULE:     rfid = new PN532(PN532::CONNECTION_TYPE::SPI); break;
        default:                   rfid = new PN532(PN532::CONNECTION_TYPE::I2C); break;
    }
    if (!rfid->begin()) { displayError("No PN532 found", true); delete rfid; return; }

    const char *PATH = "/deuce/nfclog.csv";
    const String HDR = "time,uid";
    int count = 0; String lastUid = ""; String lastTime = "--"; uint32_t lastSeen = 0, lastDraw = 0;
    tft.fillScreen(bg);
    while (true) {
        if (check(EscPress)) break;
        int r = rfid->read();
        if (r == RFIDInterface::SUCCESS) {
            String uid = rfid->printableUID.uid;
            if (uid.length() && (uid != lastUid || millis() - lastSeen > 3000)) {
                lastUid = uid; lastSeen = millis(); lastTime = nowStamp();
                char row[96]; snprintf(row, sizeof(row), "%s,%s", lastTime.c_str(), uid.c_str());
                bool w = logRow(PATH, HDR, String(row));
                if (w) count++;
                toast(w ? "tag logged" : "SD error", w ? TFT_GREEN : TFT_RED);
            }
        }
        if (millis() - lastDraw < 150) { vTaskDelay(20 / portTICK_PERIOD_MS); continue; }
        lastDraw = millis();
        title("NFC ACCESS LOG");
        tft.fillRect(0, 18, W, H - 46, bg);
        tft.setTextColor(pri, bg);
        tft.drawCentreString("tap a tag", W / 2, 30, 1);
        tft.setTextColor(sec, bg);
        char l[64];
        snprintf(l, sizeof(l), "logged: %d", count); tft.drawString(l, 8, 52);
        tft.drawString("last UID:", 8, 68);
        tft.setTextColor(pri, bg);
        tft.drawString(lastUid.length() ? lastUid.c_str() : "-", 8, 80);
        tft.setTextColor(sec, bg);
        snprintf(l, sizeof(l), "at %s", lastTime.c_str()); tft.drawString(l, 8, 94);
        tft.drawString("-> /deuce/nfclog.csv   [ESC]", 4, H - 11);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    delete rfid;
}
