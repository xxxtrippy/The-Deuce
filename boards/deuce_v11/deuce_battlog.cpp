// THE DEUCE v1.1 -- battery health / discharge logger. See .h.

#include "deuce_battlog.h"

#include <Arduino.h>
#include <SD.h>
#include <globals.h>

#include "core/display.h"
#include "core/sd_functions.h"
#include "core/utils.h"

#define BATT_CSV "/deuce/battlog.csv"

void deuceBattLog() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;

    const int N = 240;
    int8_t *hist = (int8_t *)malloc(N);          // heap, not boot RAM
    if (!hist) { displayError("Out of memory", true); return; }
    int count = 0, head = 0;
    uint32_t startMs = millis();
    int startPct = getBattery();
    bool logging = false;
    uint32_t lastSample = 0, lastLogRow = 0;
    bool redraw = true;

    while (true) {
        if (check(EscPress)) break;
        if (check(SelPress)) {
            logging = !logging;
            if (logging && sdcardMounted) { SD.mkdir("/deuce"); }
            redraw = true;
        }
        if (check(NextPress)) { count = 0; head = 0; startMs = millis(); startPct = getBattery(); redraw = true; }

        uint32_t now = millis();
        if (now - lastSample >= 2000 || count == 0) {   // sample every 2s
            lastSample = now;
            int pct = getBattery();
            if (pct < 0) pct = 0; if (pct > 100) pct = 100;
            hist[head] = (int8_t)pct;
            head = (head + 1) % N;
            if (count < N) count++;

            if (logging && sdcardMounted && now - lastLogRow >= 30000) {  // CSV every 30s
                lastLogRow = now;
                File f = SD.open(BATT_CSV, FILE_APPEND);
                if (f) { f.printf("%lu,%d\n", (unsigned long)(now / 1000), pct); f.close(); }
            }
            redraw = true;
        }

        if (redraw) {
            redraw = false;
            tft.fillScreen(bg);
            tft.setTextColor(pri, bg); tft.setTextSize(1);
            tft.drawCentreString("BATTERY LOG", W / 2, 3, 1);
            tft.drawFastHLine(0, 15, W, sec);

            int cur = count ? hist[(head - 1 + N) % N] : getBattery();
            tft.setTextSize(3); tft.setTextColor(cur < 20 ? TFT_RED : pri, bg);
            char b[8]; snprintf(b, sizeof(b), "%d%%", cur);
            tft.drawString(b, 10, 22);

            // runtime estimate from overall slope since start
            float mins = (now - startMs) / 60000.0f;
            int drop = startPct - cur;
            char est[28] = "est: --";
            if (mins > 0.5f && drop > 0) {
                float pctPerMin = drop / mins;
                int remain = (int)(cur / pctPerMin);
                snprintf(est, sizeof(est), "est: %dh %02dm left", remain / 60, remain % 60);
            } else if (drop < 0) {
                strcpy(est, "charging");
            }
            tft.setTextSize(1); tft.setTextColor(sec, bg);
            tft.drawRightString(est, W - 8, 30, 1);
            char rate[28]; snprintf(rate, sizeof(rate), "%.0f min logged", mins);
            tft.drawRightString(rate, W - 8, 44, 1);

            // discharge graph
            int gx = 6, gy = 60, gw = W - 12, gh = H - gy - 18;
            tft.drawRect(gx, gy, gw, gh, sec);
            for (int yy = 0; yy <= 100; yy += 25) {
                int py = gy + gh - 1 - (yy * (gh - 2) / 100);
                tft.drawFastHLine(gx + 1, py, gw - 2, bruceConfig.bgColor == TFT_BLACK ? 0x2104 : sec);
            }
            int plotN = count < gw - 2 ? count : gw - 2;
            for (int i = 0; i < plotN; i++) {
                int idx = (head - plotN + i + N) % N;
                int v = hist[idx];
                int px = gx + 1 + i;
                int py = gy + gh - 1 - (v * (gh - 2) / 100);
                tft.drawPixel(px, py, cur < 20 ? TFT_RED : TFT_GREEN);
                tft.drawFastVLine(px, py, (gy + gh - 1) - py, (bruceConfig.priColor & 0x39E7));
            }
            tft.setTextColor(logging ? TFT_GREEN : sec, bg);
            tft.drawString(logging ? "LOG: on (/deuce/battlog.csv)" : "[SEL] start CSV log", 6, H - 11);
            tft.drawRightString("[NEXT] reset", W - 6, H - 11, 1);
        }
        vTaskDelay(60 / portTICK_PERIOD_MS);
    }
    free(hist);
}
