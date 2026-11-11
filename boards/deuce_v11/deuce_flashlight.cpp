// THE DEUCE v1.1 -- screen flashlight. See .h.

#include "deuce_flashlight.h"

#include <Arduino.h>
#include <globals.h>

#include "core/display.h"

void deuceFlashlight() {
    int W = tft.width(), H = tft.height();
    int mode = 0;          // 0 torch, 1 strobe, 2 SOS
    int strobeMs = 60;     // half-period
    bool on = true;
    uint32_t lastToggle = 0;
    // SOS pattern: 1=unit on. dot=1u, dash=3u, intra=1u gap, letter=3u, word=7u
    static const char *SOS = "101010001110111011100010101"; // ...---...
    int sosIdx = 0; uint32_t sosUnit = 200, lastSos = 0;
    bool redraw = true;

    auto paint = [&](bool lit) {
        tft.fillScreen(lit ? TFT_WHITE : TFT_BLACK);
        if (!lit) return;
        tft.setTextColor(TFT_BLACK, TFT_WHITE);
        const char *mn = mode == 0 ? "TORCH" : mode == 1 ? "STROBE" : "SOS";
        tft.drawString(mn, 6, 4);
        tft.drawString("[NEXT] mode [ESC] exit", 6, H - 11);
        if (mode == 1) { char s[20]; snprintf(s, sizeof(s), "%d ms  [UP/DN]", strobeMs); tft.drawString(s, 6, H - 22); }
    };

    paint(true);
    while (true) {
        if (check(EscPress)) break;
        if (check(NextPress)) { mode = (mode + 1) % 3; on = true; sosIdx = 0; redraw = true; }
        if (mode == 1) {
            if (check(UpPress)) { strobeMs += 20; if (strobeMs > 1000) strobeMs = 1000; }
            if (check(DownPress)) { strobeMs -= 20; if (strobeMs < 20) strobeMs = 20; }
        }

        uint32_t now = millis();
        if (mode == 0) {
            if (redraw) { paint(true); redraw = false; }
        } else if (mode == 1) {
            if (now - lastToggle >= (uint32_t)strobeMs || redraw) {
                lastToggle = now; on = !on; paint(on); redraw = false;
            }
        } else { // SOS
            if (now - lastSos >= sosUnit || redraw) {
                lastSos = now;
                bool lit = SOS[sosIdx] == '1';
                paint(lit); redraw = false;
                sosIdx = (sosIdx + 1) % (int)strlen(SOS);
            }
        }
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
    tft.fillScreen(bruceConfig.bgColor);
}
