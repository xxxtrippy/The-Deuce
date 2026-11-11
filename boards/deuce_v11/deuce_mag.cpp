// THE DEUCE v1.1 -- Magnetometer suite. See .h.

#include "deuce_mag.h"

#include <Arduino.h>
#include <globals.h>
#include <math.h>

#include "core/display.h"
#include "deuce_mmc5983.h"

enum MagView { MV_COMPASS = 0, MV_FIELD = 1, MV_DETECT = 2, MV_RAW = 3, MV_N = 4 };
static const char *MV_NAMES[MV_N] = { "COMPASS", "FIELD METER", "METAL DETECT", "RAW XYZ" };

// hard-iron offsets (Gauss)
static float g_ox = 0, g_oy = 0, g_oz = 0;

static const char *cardinal(float hdg) {
    static const char *C[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
    int i = (int)((hdg + 22.5f) / 45.0f) & 7;
    return C[i];
}

// ── compass ───────────────────────────────────────────────────────────────────
static void drawCompass(float hdg) {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    int cx = W / 2, cy = (H - 18) / 2 + 18, R = (H - 40) / 2;
    tft.fillRect(0, 18, W, H - 30, bg);
    tft.drawCircle(cx, cy, R, sec);
    // fixed index triangle at top = device forward
    tft.fillTriangle(cx - 5, 20, cx + 5, 20, cx, 30, pri);
    // rotating rose
    const char *labels[4] = { "N", "E", "S", "W" };
    for (int i = 0; i < 4; i++) {
        float b = i * 90.0f;
        float a = (b - hdg) * DEG_TO_RAD;
        int x = cx + (int)((R - 12) * sinf(a));
        int y = cy - (int)((R - 12) * cosf(a));
        tft.setTextColor(i == 0 ? TFT_RED : sec, bg);
        tft.drawCentreString(labels[i], x, y - 4, 1);
    }
    // needle to magnetic north
    float an = (-hdg) * DEG_TO_RAD;
    int nx = cx + (int)((R - 20) * sinf(an));
    int ny = cy - (int)((R - 20) * cosf(an));
    tft.drawLine(cx, cy, nx, ny, TFT_RED);
    tft.fillCircle(cx, cy, 3, pri);
    // big heading
    tft.setTextColor(pri, bg);
    tft.setTextSize(2);
    char hs[16]; snprintf(hs, sizeof(hs), "%3d %s", (int)hdg, cardinal(hdg));
    tft.drawCentreString(hs, cx, H - 16, 1);
    tft.setTextSize(1);
}

// ── figure-8 calibration ──────────────────────────────────────────────────────
static void calibrate() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor;
    float xmin = 1e9, xmax = -1e9, ymin = 1e9, ymax = -1e9, zmin = 1e9, zmax = -1e9;
    uint32_t t0 = millis();
    while (millis() - t0 < 14000) {
        if (check(EscPress)) break;
        float gx, gy, gz;
        if (mmcReadGauss(gx, gy, gz)) {
            if (gx < xmin) xmin = gx; if (gx > xmax) xmax = gx;
            if (gy < ymin) ymin = gy; if (gy > ymax) ymax = gy;
            if (gz < zmin) zmin = gz; if (gz > zmax) zmax = gz;
        }
        tft.fillScreen(bg);
        tft.setTextColor(pri, bg);
        tft.drawCentreString("CALIBRATING", W / 2, H / 2 - 24, 1);
        tft.drawCentreString("rotate in a figure-8", W / 2, H / 2 - 8, 1);
        int secs = 14 - (millis() - t0) / 1000;
        char c[8]; snprintf(c, sizeof(c), "%ds", secs);
        tft.setTextSize(2);
        tft.drawCentreString(c, W / 2, H / 2 + 10, 1);
        tft.setTextSize(1);
        vTaskDelay(60 / portTICK_PERIOD_MS);
    }
    if (xmax > xmin) { g_ox = (xmin + xmax) / 2; g_oy = (ymin + ymax) / 2; g_oz = (zmin + zmax) / 2; }
}

// ── scrolling graph ───────────────────────────────────────────────────────────
#define MAG_HIST 160
static float g_hist[MAG_HIST];
static int g_n = 0, g_head = 0;
static void histPush(float v) { g_hist[g_head] = v; g_head = (g_head + 1) % MAG_HIST; if (g_n < MAG_HIST) g_n++; }
static float histGet(int i) { int s = (g_head - g_n + MAG_HIST) % MAG_HIST; return g_hist[(s + i) % MAG_HIST]; }

static void drawField(float ux, float uy, float uz, float umag, float &vmin, float &vmax) {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    tft.fillRect(0, 18, W, H - 30, bg);
    // big magnitude
    tft.setTextColor(pri, bg);
    tft.setTextSize(3);
    char m[16]; snprintf(m, sizeof(m), "%.0f", umag);
    tft.drawString(m, 8, 22);
    tft.setTextSize(1);
    tft.drawString("uT", 8 + tft.textWidth(m) * 3 + 2, 22);
    if (umag < vmin) vmin = umag; if (umag > vmax) vmax = umag;
    tft.setTextColor(sec, bg);
    char mm[32]; snprintf(mm, sizeof(mm), "min %.0f  max %.0f", vmin, vmax);
    tft.drawString(mm, W - 4 - tft.textWidth(mm), 24);
    // per-axis bars (-100..100 uT)
    const char *ax[3] = { "X", "Y", "Z" }; float av[3] = { ux, uy, uz };
    uint16_t ac[3] = { TFT_RED, TFT_GREEN, TFT_CYAN };
    for (int i = 0; i < 3; i++) {
        int y = 52 + i * 12;
        tft.setTextColor(ac[i], bg);
        tft.drawString(ax[i], 4, y);
        int bx = 18, bw = W - 60, mid = bx + bw / 2;
        tft.drawFastHLine(bx, y + 4, bw, sec);
        tft.drawFastVLine(mid, y, 9, sec);
        int d = (int)(av[i] / 100.0f * (bw / 2)); d = constrain(d, -bw / 2, bw / 2);
        tft.fillRect(d >= 0 ? mid : mid + d, y + 2, abs(d), 5, ac[i]);
        char vv[12]; snprintf(vv, sizeof(vv), "%4.0f", av[i]);
        tft.setTextColor(sec, bg); tft.drawString(vv, W - 4 - tft.textWidth(vv), y);
    }
    // scrolling magnitude graph
    int gy = 92, gh = H - gy - 14;
    tft.drawRect(4, gy, W - 8, gh, sec);
    float lo = 1e9, hi = -1e9;
    for (int i = 0; i < g_n; i++) { float v = histGet(i); if (v < lo) lo = v; if (v > hi) hi = v; }
    if (lo > hi) { lo = 0; hi = 1; } if (hi - lo < 1) { hi = lo + 1; }
    int px = 0, py = 0;
    for (int i = 0; i < g_n; i++) {
        float v = histGet(i);
        int x = 6 + (int)((float)i / (MAG_HIST - 1) * (W - 12));
        int y = gy + gh - 2 - (int)((v - lo) / (hi - lo) * (gh - 4));
        if (i > 0) tft.drawLine(px, py, x, y, pri);
        px = x; py = y;
    }
}

// ── metal / magnet detector ───────────────────────────────────────────────────
static void drawDetect(float umag, float base) {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    tft.fillRect(0, 18, W, H - 30, bg);
    float dev = umag - base;              // uT above/below baseline
    float adev = fabsf(dev);
    bool hit = adev > 12.0f;
    // big delta number
    tft.setTextColor(hit ? TFT_RED : pri, bg);
    tft.setTextSize(3);
    char d[16]; snprintf(d, sizeof(d), "%+.0f", dev);
    tft.drawCentreString(d, W / 2, 26, 1);
    tft.setTextSize(1);
    tft.drawCentreString("uT vs baseline", W / 2, 54, 1);
    // bidirectional bar from center, full-scale 150 uT
    int by = 78, bh = 20, bx = 14, bw = W - 28, mid = bx + bw / 2;
    tft.drawRect(bx, by, bw, bh, sec);
    tft.drawFastVLine(mid, by - 3, bh + 6, sec);
    int len = (int)(dev / 150.0f * (bw / 2)); len = constrain(len, -bw / 2, bw / 2);
    uint16_t c = hit ? TFT_RED : TFT_GREEN;
    tft.fillRect(len >= 0 ? mid : mid + len, by + 2, abs(len), bh - 4, c);
    // alert + strength dots
    if (hit) {
        tft.setTextColor(TFT_RED, bg);
        tft.setTextSize(2);
        tft.drawCentreString("** DETECT **", W / 2, H - 40, 1);
        tft.setTextSize(1);
    }
    tft.setTextColor(sec, bg);
    char b[40]; snprintf(b, sizeof(b), "field %.0f uT  base %.0f  [NEXT]=zero", umag, base);
    tft.drawString(b, 4, H - 11);
}

// ── raw ───────────────────────────────────────────────────────────────────────
static void drawRaw(float gx, float gy, float gz, float umag) {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    tft.fillRect(0, 18, W, H - 30, bg);
    tft.setTextColor(sec, bg);
    char l[48];
    snprintf(l, sizeof(l), "X  %+8.1f uT", gx * 100); tft.drawString(l, 10, 34);
    snprintf(l, sizeof(l), "Y  %+8.1f uT", gy * 100); tft.drawString(l, 10, 54);
    snprintf(l, sizeof(l), "Z  %+8.1f uT", gz * 100); tft.drawString(l, 10, 74);
    tft.setTextColor(pri, bg);
    snprintf(l, sizeof(l), "|B| %.1f uT (%.3f G)", umag, umag / 100.0f);
    tft.drawString(l, 10, 100);
    tft.setTextColor(sec, bg);
    tft.drawString("Earth field ~ 25-65 uT", 10, H - 11);
}

// ── main ──────────────────────────────────────────────────────────────────────
void deuceMagnetometer() {
    if (!mmcPresent() && !mmcBegin()) {
        displayError("No MMC5983 found", true);
        return;
    }
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;

    int view = MV_COMPASS;
    float vmin = 1e9, vmax = -1e9, base = 50.0f;
    bool baseSet = false;
    g_n = 0; g_head = 0;
    bool redrawTitle = true;
    tft.fillScreen(bg);

    while (true) {
        if (check(EscPress)) break;
        if (check(SelPress)) { view = (view + 1) % MV_N; tft.fillScreen(bg); redrawTitle = true; vmin = 1e9; vmax = -1e9; }
        if (check(NextPress)) {
            if (view == MV_COMPASS) { calibrate(); tft.fillScreen(bg); redrawTitle = true; }
            else if (view == MV_FIELD) { vmin = 1e9; vmax = -1e9; }
            else if (view == MV_DETECT) { baseSet = false; }
        }

        float gx, gy, gz;
        bool ok = mmcReadGauss(gx, gy, gz);
        float umag = 0, hdg = 0, ux = 0, uy = 0, uz = 0;
        if (ok) {
            float cx = gx - g_ox, cy = gy - g_oy, cz = gz - g_oz;
            umag = sqrtf(cx * cx + cy * cy + cz * cz) * 100.0f; // uT
            ux = cx * 100; uy = cy * 100; uz = cz * 100;
            hdg = atan2f(cy, cx) * RAD_TO_DEG; if (hdg < 0) hdg += 360.0f;
            if (!baseSet) { base = umag; baseSet = true; }
            if (view == MV_FIELD) histPush(umag);
        }

        if (redrawTitle) {
            tft.fillRect(0, 0, W, 17, bg);
            tft.setTextSize(1);
            tft.setTextColor(pri, bg);
            tft.drawCentreString(MV_NAMES[view], W / 2, 4, 1);
            tft.drawFastHLine(0, 16, W, sec);
            redrawTitle = false;
        }

        if (ok) {
            switch (view) {
                case MV_COMPASS: drawCompass(hdg); break;
                case MV_FIELD:   drawField(ux, uy, uz, umag, vmin, vmax); break;
                case MV_DETECT:  drawDetect(umag, base); break;
                case MV_RAW:     drawRaw(gx - g_ox, gy - g_oy, gz - g_oz, umag); break;
            }
        }
        vTaskDelay(70 / portTICK_PERIOD_MS);
    }
}
