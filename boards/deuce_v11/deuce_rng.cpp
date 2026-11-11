// THE DEUCE v1.1 -- hardware RNG toys. See .h.

#include "deuce_rng.h"

#include <Arduino.h>
#include <esp_system.h>
#include <globals.h>

#include "core/display.h"
#include "core/mykeyboard.h"

// esp_random() is the ESP32 hardware RNG; remix with live ADC noise for extra
// physical entropy. (Best when WiFi/BT radio is active, which seeds the HW RNG.)
static uint32_t rng32() {
    uint32_t r = esp_random();
    for (int i = 0; i < 4; i++) r ^= ((uint32_t)analogRead(39) & 0x0F) << (i * 8);
    r ^= (uint32_t)micros();
    return r;
}
static uint32_t rngRange(uint32_t lo, uint32_t hi) { // inclusive
    if (hi <= lo) return lo;
    return lo + (rng32() % (hi - lo + 1));
}

void deuceRng() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;

    const int DICE[] = {4, 6, 8, 10, 12, 20, 100};
    const int NDICE = sizeof(DICE) / sizeof(DICE[0]);
    int mode = 0;      // 0 coin, 1 dice, 2 number, 3 bytes
    int diceIdx = 1;   // d6
    uint32_t rlo = 1, rhi = 100;
    String result = "";
    bool redraw = true;

    auto roll = [&]() {
        switch (mode) {
            case 0: result = (rng32() & 1) ? "HEADS" : "TAILS"; break;
            case 1: result = "d" + String(DICE[diceIdx]) + ": " + String((int)rngRange(1, DICE[diceIdx])); break;
            case 2: result = String((unsigned long)rngRange(rlo, rhi)); break;
            case 3: {
                result = "";
                for (int i = 0; i < 8; i++) { char b[4]; snprintf(b, sizeof(b), "%02X", (unsigned)(rng32() & 0xFF)); result += b; }
                break;
            }
        }
        redraw = true;
    };
    roll();

    while (true) {
        if (check(EscPress)) break;
        if (check(NextPress)) { mode = (mode + 1) % 4; roll(); }
        if (check(PrevPress) && mode == 1) { diceIdx = (diceIdx + 1) % NDICE; roll(); }
        if (check(SelPress)) {
            if (mode == 2) {
                String a = keyboard("1", 9, "Min");
                String b = keyboard("100", 9, "Max");
                rlo = a.toInt(); rhi = b.toInt();
            }
            roll();
        }

        if (redraw) {
            redraw = false;
            tft.fillScreen(bg);
            tft.setTextColor(pri, bg); tft.setTextSize(1);
            tft.drawCentreString("RANDOM", W / 2, 4, 1);
            tft.drawFastHLine(0, 16, W, sec);
            const char *mn = mode == 0 ? "Coin flip" : mode == 1 ? "Dice" : mode == 2 ? "Number" : "Hex bytes";
            tft.setTextColor(sec, bg); tft.drawCentreString(mn, W / 2, 22, 1);
            if (mode == 1) { char d[8]; snprintf(d, sizeof(d), "d%d", DICE[diceIdx]); tft.drawString(d, 6, 22); }
            if (mode == 2) { char r[20]; snprintf(r, sizeof(r), "%lu-%lu", (unsigned long)rlo, (unsigned long)rhi); tft.drawString(r, 6, 22); }

            tft.setTextColor(pri, bg);
            if (mode == 3) {
                tft.setTextSize(2);
                tft.drawCentreString(result.substring(0, 8).c_str(), W / 2, 48, 1);
                tft.drawCentreString(result.substring(8).c_str(), W / 2, 72, 1);
            } else {
                tft.setTextSize(4);
                tft.drawCentreString(result.c_str(), W / 2, 50, 1);
            }
            tft.setTextSize(1); tft.setTextColor(sec, bg);
            tft.drawString(mode == 1 ? "[SEL] roll [NEXT] mode [PREV] die"
                           : mode == 2 ? "[SEL] set+roll [NEXT] mode"
                                       : "[SEL] roll [NEXT] mode", 4, H - 22);
            tft.drawString("[ESC] exit", 4, H - 11);
        }
        vTaskDelay(30 / portTICK_PERIOD_MS);
    }
}
