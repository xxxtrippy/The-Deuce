// THE DEUCE v1.1 -- antenna pattern plotter (live, measured). See .h.

#include "deuce_antplot.h"

#include <Arduino.h>
#include <globals.h>
#include <math.h>

#include "core/display.h"
#include "deuce_mmc5983.h"        // optional magnetometer -> auto bearing
#include "modules/rf/rf_utils.h" // initRfModule/deinitRfModule/setMHZ + ELECHOUSE_cc1101

#define AP_HDR 24
#define AP_BINS 36          // 10 deg bins
#define AP_DEG2RAD 0.017453293f

static float g_bin[AP_BINS];
static bool g_set[AP_BINS];
static int RCX, RCY, RR;

static int apReadRssiAvg(int n) {
    long s = 0;
    for (int i = 0; i < n; i++) {
        s += ELECHOUSE_cc1101.getRssi();
        tft.drawPixel(0, 0, bruceConfig.bgColor); // nudge shared SPI
        delayMicroseconds(400);
    }
    return (int)(s / n);
}

static void apReset() {
    for (int i = 0; i < AP_BINS; i++) {
        g_bin[i] = -200;
        g_set[i] = false;
    }
}

// peak bin + value
static void apStats(int &peakBin, float &peak, float &fb, int &hpbw, int &filled) {
    peakBin = -1;
    peak = -300;
    filled = 0;
    for (int i = 0; i < AP_BINS; i++) {
        if (!g_set[i]) continue;
        filled++;
        if (g_bin[i] > peak) {
            peak = g_bin[i];
            peakBin = i;
        }
    }
    fb = 0;
    hpbw = 0;
    if (peakBin < 0) return;
    int opp = (peakBin + AP_BINS / 2) % AP_BINS;
    if (g_set[opp]) fb = peak - g_bin[opp];
    // HPBW: contiguous span around peak within 3 dB
    int span = 1;
    for (int d = 1; d < AP_BINS / 2; d++) {
        int a = (peakBin + d) % AP_BINS, b = (peakBin - d + AP_BINS) % AP_BINS;
        bool ina = g_set[a] && (peak - g_bin[a] <= 3.0f);
        bool inb = g_set[b] && (peak - g_bin[b] <= 3.0f);
        if (ina) span++;
        if (inb) span++;
        if (!ina && !inb) break;
    }
    hpbw = span * (360 / AP_BINS);
}

static void apPoint(float relDeg, float rad, int &x, int &y) {
    // 0 deg boresight = screen right; angle increases counter-clockwise
    float a = relDeg * AP_DEG2RAD;
    x = RCX + (int)(rad * cosf(a));
    y = RCY - (int)(rad * sinf(a));
}

static void apDrawStatic(float freq, bool mag) {
    int W = tft.width();
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    tft.fillScreen(bg);
    tft.setTextSize(1);
    tft.setTextColor(pri, bg);
    tft.drawString("ANT PLOT", 6, 6);
    String s = String(freq, 2) + " " + (mag ? "MAG" : "STEP");
    tft.setTextColor(sec, bg);
    tft.drawString(s, W - 6 - tft.textWidth(s), 6);
    tft.drawFastHLine(0, AP_HDR - 2, W, sec);
    RR = 92;
    RCX = 8 + RR;
    RCY = AP_HDR + 8 + RR;
    if (RCY + RR > tft.height() - 4) RCY = tft.height() - 4 - RR;
}

static void apDrawPlot() {
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    int W = tft.width();
    tft.fillRect(RCX - RR - 2, RCY - RR - 2, 2 * RR + 4, 2 * RR + 4, bg);

    // rings + boresight spoke
    tft.drawCircle(RCX, RCY, RR, sec);
    tft.drawCircle(RCX, RCY, (RR * 2) / 3, sec);
    tft.drawCircle(RCX, RCY, RR / 3, sec);
    tft.drawLine(RCX, RCY, RCX + RR, RCY, pri); // boresight -> right
    tft.drawLine(RCX, RCY, RCX - RR, RCY, sec);
    tft.drawLine(RCX, RCY - RR, RCX, RCY + RR, sec);

    int peakBin, hpbw, filled;
    float peak, fb;
    apStats(peakBin, peak, fb, hpbw, filled);

    if (peakBin >= 0 && filled >= 2) {
        float floorv = peak - 30.0f; // 30 dB dynamic range
        int px = -1, py = -1, fx = -1, fy = -1;
        for (int i = 0; i <= AP_BINS; i++) {
            int b = i % AP_BINS;
            if (!g_set[b]) {
                px = -1;
                continue;
            }
            float v = g_bin[b];
            float norm = (v - floorv) / 30.0f;
            if (norm < 0.04f) norm = 0.04f;
            if (norm > 1.0f) norm = 1.0f;
            int x, y;
            apPoint(b * (360.0f / AP_BINS), norm * RR, x, y);
            if (fx < 0) {
                fx = x;
                fy = y;
            }
            if (px >= 0) tft.drawLine(px, py, x, y, pri);
            px = x;
            py = y;
        }
        // peak marker
        int mx, my;
        float pn = 1.0f;
        apPoint(peakBin * (360.0f / AP_BINS), pn * RR, mx, my);
        tft.fillCircle(mx, my, 3, TFT_GREEN);
    }

    // stats panel
    int sx = RCX + RR + 10;
    if (sx < 210) sx = 210;
    tft.fillRect(sx, AP_HDR + 6, W - sx - 2, 120, bg);
    tft.setTextColor(pri, bg);
    char l[24];
    if (peakBin >= 0) {
        snprintf(l, sizeof(l), "Peak %d dBm", (int)peak);
        tft.drawString(l, sx, AP_HDR + 10);
        snprintf(l, sizeof(l), "@ %d deg", peakBin * (360 / AP_BINS));
        tft.setTextColor(sec, bg);
        tft.drawString(l, sx, AP_HDR + 24);
        snprintf(l, sizeof(l), "F/B %d dB", (int)fb);
        tft.setTextColor(pri, bg);
        tft.drawString(l, sx, AP_HDR + 42);
        snprintf(l, sizeof(l), "HPBW %d", hpbw);
        tft.drawString(l, sx, AP_HDR + 56);
    } else {
        tft.drawString("rotate to", sx, AP_HDR + 10);
        tft.drawString("sample...", sx, AP_HDR + 24);
    }
    snprintf(l, sizeof(l), "%d/%d bins", filled, AP_BINS);
    tft.setTextColor(sec, bg);
    tft.drawString(l, sx, AP_HDR + 74);
}

void deuceAntennaPlot() {
    if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) {
        displayError("Antenna plot needs a CC1101", true);
        return;
    }

    float freq = 433.92;
    options = {
        {"433.92 MHz", [&]() { freq = 433.92; }},
        {"315 MHz",    [&]() { freq = 315.00; }},
        {"868 MHz",    [&]() { freq = 868.35; }},
        {"915 MHz",    [&]() { freq = 915.00; }},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "Antenna: frequency");

    bool mag = mmcBegin();

    float savedFreq = bruceConfigPins.rfFreq;
    bruceConfigPins.rfFreq = freq;
    if (!initRfModule("rx", freq)) {
        bruceConfigPins.rfFreq = savedFreq;
        deinitRfModule();
        displayError("RF init failed", true);
        return;
    }
    setMHZ(freq);

    apReset();
    apDrawStatic(freq, mag);

    int W = tft.width();
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;

    // footer help
    tft.setTextColor(sec, bg);
    if (mag) tft.drawString("[SEL] set front  [NEXT] reset  [ESC] exit", 6, tft.height() - 12);
    else tft.drawString("[SEL] capture step  [NEXT] reset  [ESC]", 6, tft.height() - 12);

    float heading0 = 0;
    bool haveZero = false;
    int stepIdx = 0; // manual: 0..11 (every 30 deg)
    uint32_t lastDraw = 0;

    apDrawPlot();

    while (true) {
        if (check(EscPress)) break;
        if (check(NextPress)) {
            apReset();
            stepIdx = 0;
            apDrawPlot();
        }

        if (mag) {
            // continuous: read heading + rssi, peak-hold into the matching bin
            float gx, gy, gz;
            if (mmcReadGauss(gx, gy, gz)) {
                float hd = atan2f(gy, gx) / AP_DEG2RAD; // -180..180
                if (hd < 0) hd += 360.0f;
                if (check(SelPress)) {
                    heading0 = hd;
                    haveZero = true;
                }
                if (!haveZero) {
                    heading0 = hd;
                    haveZero = true;
                }
                float rel = hd - heading0;
                while (rel < 0) rel += 360.0f;
                while (rel >= 360.0f) rel -= 360.0f;
                int bin = ((int)(rel / (360.0f / AP_BINS) + 0.5f)) % AP_BINS;
                int rssi = apReadRssiAvg(4);
                if (!g_set[bin] || rssi > g_bin[bin]) {
                    g_bin[bin] = rssi;
                    g_set[bin] = true;
                }
            }
            if (millis() - lastDraw > 120) {
                apDrawPlot();
                lastDraw = millis();
            }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        } else {
            // stepped: SEL captures the current 30-deg step
            int targetDeg = stepIdx * 30;
            tft.fillRect(0, tft.height() - 24, W, 10, bg);
            tft.setTextColor(pri, bg);
            if (stepIdx < 12) {
                char s[32];
                snprintf(s, sizeof(s), "Point to %d deg, press SEL (%d/12)", targetDeg, stepIdx + 1);
                tft.drawString(s, 6, tft.height() - 24);
            } else {
                tft.drawString("Sweep complete.", 6, tft.height() - 24);
            }
            if (check(SelPress) && stepIdx < 12) {
                int rssi = apReadRssiAvg(12);
                int bin = (stepIdx * 3) % AP_BINS; // 30 deg -> every 3rd bin
                g_bin[bin] = rssi;
                g_set[bin] = true;
                stepIdx++;
                apDrawPlot();
            }
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
    }

    bruceConfigPins.rfFreq = savedFreq;
    deinitRfModule();
}
