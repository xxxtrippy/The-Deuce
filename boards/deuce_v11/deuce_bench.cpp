// THE DEUCE v1.1 -- pocket bench instruments. See .h.

#include "deuce_bench.h"

#include <Arduino.h>
#include <globals.h>

#include "core/display.h"

#define BUZZER_PIN 32   // shared with Scope/Gen + Morse buzzer
#define CONT_PIN   33   // continuity probe (pullup-capable)

// pins usable as an analog/logic probe (ADC-capable, input-safe)
static const int PROBE_PINS[] = {33, 32, 34, 35, 39};
static const int NPROBE = sizeof(PROBE_PINS) / sizeof(PROBE_PINS[0]);

void deuceBench() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    int mode = 0;       // 0 volt, 1 logic, 2 freq, 3 continuity
    int pinIdx = 0;     // -> GPIO33
    bool redraw = true;
    bool contOn = false;

    while (true) {
        if (check(EscPress)) break;
        if (check(NextPress)) {
            if (contOn) { noTone(BUZZER_PIN); contOn = false; }
            mode = (mode + 1) % 4; redraw = true;
        }
        if (check(PrevPress) && mode != 3) { pinIdx = (pinIdx + 1) % NPROBE; redraw = true; }

        int pin = PROBE_PINS[pinIdx];

        // header (only on change)
        if (redraw) {
            tft.fillScreen(bg);
            tft.setTextColor(pri, bg); tft.setTextSize(1);
            tft.drawCentreString("POCKET BENCH", W / 2, 3, 1);
            tft.drawFastHLine(0, 15, W, sec);
            const char *mn = mode == 0 ? "Voltmeter" : mode == 1 ? "Logic Probe" : mode == 2 ? "Frequency" : "Continuity";
            tft.setTextColor(sec, bg); tft.drawString(mn, 6, 20);
            char pl[16];
            snprintf(pl, sizeof(pl), "GPIO%d", mode == 3 ? CONT_PIN : pin);
            tft.drawRightString(pl, W - 6, 20, 1);
            redraw = false;
        }

        tft.fillRect(0, 36, W, H - 50, bg);

        if (mode == 0) {                     // voltmeter
            pinMode(pin, INPUT);
            long acc = 0;
            for (int i = 0; i < 16; i++) acc += analogRead(pin);
            float v = (acc / 16.0f) * 3.3f / 4095.0f;
            tft.setTextSize(5); tft.setTextColor(pri, bg);
            char b[12]; snprintf(b, sizeof(b), "%.2f", v);
            tft.drawString(b, 24, 46);
            tft.setTextSize(2); tft.drawString("V", 24 + tft.textWidth(b) * 1 + 6, 70);
            tft.setTextSize(1); tft.setTextColor(sec, bg);
            tft.drawString("max input 3.3V -- do not exceed", 6, H - 24);
        } else if (mode == 1) {              // logic probe
            pinMode(pin, INPUT);
            int hi = 0, lo = 0;
            for (int i = 0; i < 500; i++) { if (digitalRead(pin)) hi++; else lo++; delayMicroseconds(20); }
            const char *st; uint16_t c;
            if (hi > 0 && lo > 0) { st = "PULSING"; c = TFT_YELLOW; }
            else if (hi) { st = "HIGH"; c = TFT_GREEN; }
            else { st = "LOW"; c = TFT_CYAN; }
            tft.setTextSize(4); tft.setTextColor(c, bg);
            tft.drawCentreString(st, W / 2, 50, 1);
            tft.setTextSize(1); tft.setTextColor(sec, bg);
            char d[24]; snprintf(d, sizeof(d), "duty ~%d%%", hi * 100 / 500);
            tft.drawCentreString(d, W / 2, 92, 1);
        } else if (mode == 2) {              // frequency
            pinMode(pin, INPUT);
            unsigned long hi = pulseIn(pin, HIGH, 50000UL);
            unsigned long lo = pulseIn(pin, LOW, 50000UL);
            tft.setTextSize(3); tft.setTextColor(pri, bg);
            if (hi + lo > 0) {
                float f = 1000000.0f / (hi + lo);
                char b[16];
                if (f >= 1000) snprintf(b, sizeof(b), "%.2f kHz", f / 1000.0f);
                else snprintf(b, sizeof(b), "%.0f Hz", f);
                tft.drawCentreString(b, W / 2, 52, 1);
            } else {
                tft.setTextColor(sec, bg);
                tft.drawCentreString("no signal / DC", W / 2, 56, 1);
            }
            tft.setTextSize(1); tft.setTextColor(sec, bg);
            tft.drawCentreString("square-ish signals, <~100 kHz", W / 2, H - 24, 1);
        } else {                             // continuity
            pinMode(CONT_PIN, INPUT_PULLUP);
            bool shorted = (digitalRead(CONT_PIN) == LOW);
            if (shorted && !contOn) { tone(BUZZER_PIN, 2000); contOn = true; }
            else if (!shorted && contOn) { noTone(BUZZER_PIN); contOn = false; }
            tft.setTextSize(4); tft.setTextColor(shorted ? TFT_GREEN : sec, bg);
            tft.drawCentreString(shorted ? "BEEP" : "open", W / 2, 50, 1);
            tft.setTextSize(1); tft.setTextColor(sec, bg);
            tft.drawCentreString("probe GPIO33 to GND", W / 2, 92, 1);
        }

        tft.setTextColor(sec, bg); tft.setTextSize(1);
        tft.drawString(mode == 3 ? "[NEXT] mode [ESC]" : "[NEXT] mode [PREV] pin [ESC]", 4, H - 11);
        vTaskDelay(((mode == 1 || mode == 2) ? 10 : 120) / portTICK_PERIOD_MS);
    }
    if (contOn) noTone(BUZZER_PIN);
}
