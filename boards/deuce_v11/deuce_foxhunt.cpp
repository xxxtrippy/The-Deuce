// THE DEUCE v1.1 -- RF Direction Finder / Fox Hunt. See .h.

#include "deuce_foxhunt.h"

#include <Arduino.h>
#include <globals.h>
#include <math.h>

#include "core/display.h"
#include "deuce_mmc5983.h"
#include "modules/rf/rf_utils.h"

#define FH_BINS 36
static float g_bin[FH_BINS];
static bool g_set[FH_BINS];

static int fhRssi(int n = 6) {
    long s = 0;
    for (int i = 0; i < n; i++) { s += ELECHOUSE_cc1101.getRssi(); delayMicroseconds(300); }
    return (int)(s / n);
}

void deuceFoxHunt() {
    if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) { displayError("Fox Hunt needs CC1101", true); return; }
    float freq = 433.92f;
    options = {
        {"433.92 MHz", [&]() { freq = 433.92f; }},
        {"315 MHz",    [&]() { freq = 315.00f; }},
        {"868 MHz",    [&]() { freq = 868.35f; }},
        {"915 MHz",    [&]() { freq = 915.00f; }},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "CC1101 band");

    bool mag = mmcBegin();
    float saved = bruceConfigPins.rfFreq;
    bruceConfigPins.rfFreq = freq;
    if (!initRfModule("rx", freq)) { displayError("CC1101 init failed", true); return; }
    setMHZ(freq);

    for (int i = 0; i < FH_BINS; i++) { g_bin[i] = -200; g_set[i] = false; }
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    tft.fillScreen(bg);
    int peak = -200, prev = -200;
    uint32_t lastDraw = 0;

    while (true) {
        if (check(EscPress)) break;
        if (check(SelPress)) { for (int i = 0; i < FH_BINS; i++) { g_bin[i] = -200; g_set[i] = false; } peak = -200; }
        int r = fhRssi();
        if (r > peak) peak = r;
        float hd = 0; bool haveH = false;
        if (mag) { float gx, gy, gz; if (mmcReadGauss(gx, gy, gz)) { hd = atan2f(gy, gx) * RAD_TO_DEG; if (hd < 0) hd += 360; haveH = true; } }
        if (haveH) { int b = ((int)(hd / 10)) % FH_BINS; if (r > g_bin[b]) { g_bin[b] = r; g_set[b] = true; } }

        if (millis() - lastDraw < 110) { vTaskDelay(10 / portTICK_PERIOD_MS); continue; }
        lastDraw = millis();

        tft.fillRect(0, 0, W, 17, bg);
        tft.setTextColor(pri, bg); tft.setTextSize(1);
        tft.drawCentreString("FOX HUNT", W / 2, 4, 1);
        tft.drawFastHLine(0, 16, W, sec);
        tft.fillRect(0, 18, W, H - 30, bg);

        // big RSSI + warmer/colder
        tft.setTextColor(pri, bg); tft.setTextSize(3);
        char s[12]; snprintf(s, sizeof(s), "%d", r);
        tft.drawString(s, 8, 24); tft.setTextSize(1); tft.drawString("dBm", 8 + tft.textWidth(s) * 3 + 2, 24);
        if (r > prev + 1) { tft.setTextColor(TFT_GREEN, bg); tft.drawString("WARMER", 8, 56); }
        else if (r < prev - 1) { tft.setTextColor(TFT_RED, bg); tft.drawString("colder", 8, 56); }
        prev = r;
        // signal bar
        float frac = (r + 100) / 70.0f; frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
        tft.drawRect(8, 70, W / 2 - 16, 10, sec);
        tft.fillRect(9, 71, (int)(frac * (W / 2 - 18)), 8, pri);

        // polar peak map on the right
        int cx = W - 58, cy = 18 + (H - 30) / 2, R = (H - 46) / 2;
        tft.drawCircle(cx, cy, R, sec);
        tft.fillTriangle(cx - 4, cy - R - 2, cx + 4, cy - R - 2, cx, cy - R + 6, sec); // device fwd
        int pb = -1; float pv = -200;
        for (int i = 0; i < FH_BINS; i++) {
            if (!g_set[i]) continue;
            if (g_bin[i] > pv) { pv = g_bin[i]; pb = i; }
            float f = (g_bin[i] + 100) / 70.0f; f = f < 0 ? 0 : f > 1 ? 1 : f;
            float rel = (i * 10.0f - hd) * DEG_TO_RAD;
            int x = cx + (int)(f * R * sinf(rel)), y = cy - (int)(f * R * cosf(rel));
            tft.drawLine(cx, cy, x, y, TFT_DARKGREY);
        }
        if (pb >= 0) {
            float rel = (pb * 10.0f - hd) * DEG_TO_RAD;
            int x = cx + (int)((R - 6) * sinf(rel)), y = cy - (int)((R - 6) * cosf(rel));
            tft.drawLine(cx, cy, x, y, TFT_RED);
            tft.fillCircle(x, y, 3, TFT_RED);
        }
        tft.fillCircle(cx, cy, 2, pri);
        tft.setTextColor(sec, bg);
        tft.drawString(haveH ? "turn: peak=source dir" : "mag n/a", cx - R, cy + R + 2);
        tft.drawString("[SEL] reset  [ESC]", 4, H - 11);
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    deinitRfModule();
    bruceConfigPins.rfFreq = saved;
}
