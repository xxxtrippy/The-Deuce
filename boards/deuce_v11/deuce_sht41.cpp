// THE DEUCE v1.1 -- SHT41 temperature + humidity. See .h.

#include "deuce_sht41.h"

#include <Arduino.h>
#include <globals.h>
#include <math.h>

#include "core/bus_HAL.h"   // acquireI2CBus / releaseI2CBus (TwoWire)
#include "core/display.h"

#define SHT41_ADDR 0x44
#define SHT41_MEAS_HIGH 0xFD   // measure, high precision (~8.2 ms)
#define SHT41_SOFT_RST  0x94

static bool g_present = false;

// CRC-8, poly 0x31, init 0xFF (Sensirion)
static uint8_t sht41Crc(const uint8_t *d, int n) {
    uint8_t c = 0xFF;
    for (int i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
    }
    return c;
}

bool sht41Read(float &tC, float &rh) {
    if (!g_present) return false;
    TwoWire *w = acquireI2CBus();
    bool ok = false;
    if (w) {
        w->beginTransmission(SHT41_ADDR);
        w->write(SHT41_MEAS_HIGH);
        if (w->endTransmission() == 0) {
            delay(10);
            uint8_t n = w->requestFrom((uint8_t)SHT41_ADDR, (uint8_t)6, (uint8_t) true);
            if (n == 6) {
                uint8_t b[6];
                for (int i = 0; i < 6; i++) b[i] = w->read();
                if (sht41Crc(b, 2) == b[2] && sht41Crc(b + 3, 2) == b[5]) {
                    uint16_t rt = ((uint16_t)b[0] << 8) | b[1];
                    uint16_t rr = ((uint16_t)b[3] << 8) | b[4];
                    tC = -45.0f + 175.0f * ((float)rt / 65535.0f);
                    rh = -6.0f + 125.0f * ((float)rr / 65535.0f);
                    if (rh < 0) rh = 0;
                    if (rh > 100) rh = 100;
                    ok = true;
                }
            }
        }
    }
    releaseI2CBus();
    return ok;
}

bool sht41Begin() {
    // soft reset, then a trial read to confirm presence
    TwoWire *w = acquireI2CBus();
    if (w) {
        w->beginTransmission(SHT41_ADDR);
        w->write(SHT41_SOFT_RST);
        w->endTransmission();
        delay(2);
    }
    releaseI2CBus();
    g_present = true;             // allow the trial read
    float t, h;
    g_present = sht41Read(t, h);
    return g_present;
}

bool sht41Present() { return g_present; }

// ── throttled cache for the status bar ────────────────────────────────────────
static float g_cacheT = NAN;
static uint32_t g_cacheMs = 0;
bool sht41CachedTempC(float &tempC) {
    static bool probed = false;
    if (!probed) { probed = true; sht41Begin(); }   // auto-detect on first status-bar draw
    if (!g_present) return false;
    if (isnan(g_cacheT) || millis() - g_cacheMs > 5000) {
        float tt, hh;
        if (sht41Read(tt, hh)) { g_cacheT = tt; g_cacheMs = millis(); }
    }
    if (isnan(g_cacheT)) return false;
    tempC = g_cacheT;
    return true;
}

// ── derived metrics ───────────────────────────────────────────────────────────
static float dewPointC(float t, float rh) {
    const float a = 17.62f, b = 243.12f;
    float g = logf(fmaxf(rh, 0.1f) / 100.0f) + (a * t) / (b + t);
    return (b * g) / (a - g);
}
static float absHumidity(float t, float rh) { // g/m^3
    return 6.112f * expf((17.67f * t) / (t + 243.5f)) * rh * 2.1674f / (273.15f + t);
}
static float heatIndexF(float tF, float rh) {
    if (tF < 80.0f) return tF;
    float hi = -42.379f + 2.04901523f * tF + 10.14333127f * rh - 0.22475541f * tF * rh -
               0.00683783f * tF * tF - 0.05481717f * rh * rh + 0.00122874f * tF * tF * rh +
               0.00085282f * tF * rh * rh - 0.00000199f * tF * tF * rh * rh;
    return hi;
}
static const char *comfortLabel(float t, float rh) {
    if (t < 18) return "COLD";
    if (t > 27) return "HOT";
    if (rh < 30) return "DRY";
    if (rh > 60) return "HUMID";
    return "COMFORT";
}

// ── history ───────────────────────────────────────────────────────────────────
#define SHT_HIST 160
static float *g_tHist = nullptr, *g_hHist = nullptr;  // heap-allocated only while dashboard open
static int g_n = 0, g_head = 0;
static float g_tMin, g_tMax, g_hMin, g_hMax;

static void histPush(float t, float h) {
    g_tHist[g_head] = t;
    g_hHist[g_head] = h;
    g_head = (g_head + 1) % SHT_HIST;
    if (g_n < SHT_HIST) g_n++;
}
static float histGet(const float *buf, int i) { // i: 0 = oldest
    int start = (g_head - g_n + SHT_HIST) % SHT_HIST;
    return buf[(start + i) % SHT_HIST];
}
static void histRange(const float *buf, float &lo, float &hi) {
    lo = 1e9; hi = -1e9;
    for (int i = 0; i < g_n; i++) { float v = histGet(buf, i); if (v < lo) lo = v; if (v > hi) hi = v; }
    if (lo > hi) { lo = 0; hi = 1; }
    if (hi - lo < 0.5f) { float m = (lo + hi) / 2; lo = m - 0.5f; hi = m + 0.5f; }
}

// ── graph helper ──────────────────────────────────────────────────────────────
static void drawGraph(int x, int y, int w, int h, const float *buf, uint16_t col,
                      const char *label, const char *unit) {
    uint16_t bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    tft.drawRect(x, y, w, h, sec);
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, bg);
    float lo, hi; histRange(buf, lo, hi);
    // label + current/min/max
    tft.setTextColor(col, bg);
    char hdr[40];
    snprintf(hdr, sizeof(hdr), "%s %.1f%s", label, g_n ? histGet(buf, g_n - 1) : 0.0f, unit);
    tft.drawString(hdr, x + 3, y + 2);
    tft.setTextColor(sec, bg);
    char mm[32];
    snprintf(mm, sizeof(mm), "%.1f/%.1f", hi, lo);
    tft.drawString(mm, x + w - 3 - tft.textWidth(mm), y + 2);
    // plot
    int plotY = y + 12, plotH = h - 14;
    int prevX = 0, prevY = 0;
    for (int i = 0; i < g_n; i++) {
        float v = histGet(buf, i);
        int px = x + 2 + (int)((float)i / (SHT_HIST - 1) * (w - 4));
        int py = plotY + plotH - (int)((v - lo) / (hi - lo) * plotH);
        if (i > 0) tft.drawLine(prevX, prevY, px, py, col);
        prevX = px; prevY = py;
    }
}

// ── comfort map (T vs RH) ─────────────────────────────────────────────────────
static void drawComfortMap(float t, float rh) {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, sec = bruceConfig.secColor, pri = bruceConfig.priColor;
    tft.fillRect(0, 18, W, H - 30, bg);
    int gx = 34, gy = 26, gw = W - 44, gh = H - 46;
    // axes ranges: T 10..35 C, RH 0..100 %
    const float tLo = 10, tHi = 35, hLo = 0, hHi = 100;
    tft.drawRect(gx, gy, gw, gh, sec);
    // comfort zone T 20..26, RH 30..60
    int zx0 = gx + (int)((20 - tLo) / (tHi - tLo) * gw);
    int zx1 = gx + (int)((26 - tLo) / (tHi - tLo) * gw);
    int zy0 = gy + gh - (int)((60 - hLo) / (hHi - hLo) * gh);
    int zy1 = gy + gh - (int)((30 - hLo) / (hHi - hLo) * gh);
    tft.drawRect(zx0, zy0, zx1 - zx0, zy1 - zy0, TFT_GREEN);
    tft.setTextColor(TFT_GREEN, bg);
    tft.drawString("comfort", zx0 + 2, zy0 + 2);
    // axis labels
    tft.setTextColor(sec, bg);
    tft.drawString("RH%", 2, gy);
    tft.drawString("100", 10, gy);
    tft.drawString("0", 22, gy + gh - 8);
    tft.drawString("10C", gx, gy + gh + 2);
    tft.drawString("35C", gx + gw - 18, gy + gh + 2);
    // trail
    int shown = g_n < 40 ? g_n : 40;
    for (int i = g_n - shown; i < g_n; i++) {
        if (i < 0) continue;
        float tt = histGet(g_tHist, i), hh = histGet(g_hHist, i);
        int px = gx + (int)((tt - tLo) / (tHi - tLo) * gw);
        int py = gy + gh - (int)((hh - hLo) / (hHi - hLo) * gh);
        px = constrain(px, gx, gx + gw); py = constrain(py, gy, gy + gh);
        tft.drawPixel(px, py, sec);
    }
    // current point
    int cx = gx + (int)((t - tLo) / (tHi - tLo) * gw);
    int cy = gy + gh - (int)((rh - hLo) / (hHi - hLo) * gh);
    cx = constrain(cx, gx, gx + gw); cy = constrain(cy, gy, gy + gh);
    tft.fillCircle(cx, cy, 4, pri);
    tft.drawCircle(cx, cy, 6, pri);
    // readout
    tft.setTextColor(pri, bg);
    char r[32]; snprintf(r, sizeof(r), "%.1fC  %.0f%%  %s", t, rh, comfortLabel(t, rh));
    tft.drawString(r, (W - tft.textWidth(r)) / 2, H - 11);
}

// ── main dashboard ────────────────────────────────────────────────────────────
void deuceSht41() {
    if (!g_present && !sht41Begin()) {
        displayError("No SHT41 found", true);
        return;
    }
    g_tHist = (float *)malloc(SHT_HIST * sizeof(float));
    g_hHist = (float *)malloc(SHT_HIST * sizeof(float));
    if (!g_tHist || !g_hHist) { free(g_tHist); free(g_hHist); g_tHist = g_hHist = nullptr; displayError("Out of memory", true); return; }

    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    const uint16_t TCOL = TFT_ORANGE, HCOL = TFT_CYAN;

    g_n = 0; g_head = 0;
    g_tMin = 1e9; g_tMax = -1e9; g_hMin = 1e9; g_hMax = -1e9;
    int view = 0;                 // 0 = dashboard, 1 = comfort map
    bool useF = false;

    tft.fillScreen(bg);
    uint32_t lastRead = 0;
    float t = 0, h = 0;
    bool have = false;
    bool redrawStatic = true;

    while (true) {
        if (check(EscPress)) break;
        if (check(SelPress)) { view ^= 1; tft.fillScreen(bg); redrawStatic = true; }
        if (check(NextPress)) { useF = !useF; redrawStatic = true; }

        if (millis() - lastRead > 600) {
            lastRead = millis();
            float tt, hh;
            if (sht41Read(tt, hh)) {
                t = tt; h = hh; have = true;
                histPush(t, h);
                if (t < g_tMin) g_tMin = t; if (t > g_tMax) g_tMax = t;
                if (h < g_hMin) g_hMin = h; if (h > g_hMax) g_hMax = h;
            }
        }

        // title bar
        if (redrawStatic) {
            tft.fillRect(0, 0, W, 17, bg);
            tft.setTextSize(1);
            tft.setTextColor(pri, bg);
            const char *ti = "SHT41";
            tft.drawString(ti, (W - tft.textWidth(ti)) / 2, 4);
            tft.drawFastHLine(0, 16, W, sec);
            tft.setTextColor(sec, bg);
            tft.drawString("[SEL] view  [NEXT] C/F  [ESC]", 4, H - 11);
            redrawStatic = false;
        }

        if (!have) { vTaskDelay(20 / portTICK_PERIOD_MS); continue; }

        if (view == 1) {
            drawComfortMap(t, h);
            vTaskDelay(60 / portTICK_PERIOD_MS);
            continue;
        }

        // ---- dashboard ----
        float tShown = useF ? (t * 9.0f / 5.0f + 32.0f) : t;
        float dp = dewPointC(t, h);
        float dpShown = useF ? (dp * 9.0f / 5.0f + 32.0f) : dp;
        float ah = absHumidity(t, h);
        float hiF = heatIndexF(t * 9.0f / 5.0f + 32.0f, h);
        float hiShown = useF ? hiF : ((hiF - 32.0f) * 5.0f / 9.0f);

        // big numbers row
        tft.setTextColor(TCOL, bg);
        tft.setTextSize(3);
        char bt[16]; snprintf(bt, sizeof(bt), "%.1f", tShown);
        tft.fillRect(0, 20, W / 2, 30, bg);
        tft.drawString(bt, 8, 22);
        tft.setTextSize(1);
        tft.drawString(useF ? "F" : "C", 8 + tft.textWidth(bt) * 3 + 2, 22);

        tft.setTextColor(HCOL, bg);
        tft.setTextSize(3);
        char bh[16]; snprintf(bh, sizeof(bh), "%.0f", h);
        tft.fillRect(W / 2, 20, W / 2, 30, bg);
        tft.drawString(bh, W / 2 + 8, 22);
        tft.setTextSize(1);
        tft.drawString("%RH", W / 2 + 8 + tft.textWidth(bh) * 3 + 2, 22);

        // derived metrics row
        tft.fillRect(0, 52, W, 12, bg);
        tft.setTextColor(sec, bg);
        char d1[64];
        snprintf(d1, sizeof(d1), "dew %.1f%c  AH %.1fg  HI %.1f%c  %s",
                 dpShown, useF ? 'F' : 'C', ah, hiShown, useF ? 'F' : 'C', comfortLabel(t, h));
        tft.drawString(d1, 4, 54);

        // dual scrolling graphs
        int gy = 68, gh = (H - gy - 14) / 2;
        drawGraph(4, gy, W - 8, gh - 2, g_tHist, TCOL, useF ? "T" : "T", useF ? "" : "C");
        drawGraph(4, gy + gh, W - 8, gh - 2, g_hHist, HCOL, "RH", "%");

        vTaskDelay(60 / portTICK_PERIOD_MS);
    }
    free(g_tHist); free(g_hHist);
    g_tHist = g_hHist = nullptr;
}
