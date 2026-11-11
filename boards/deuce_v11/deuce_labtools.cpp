// THE DEUCE v1.1 -- bench / learning tools. See .h.

#include "deuce_labtools.h"

#include <Arduino.h>
#include <globals.h>

#include "core/display.h"
#include "core/mykeyboard.h"

// ── Mini Scope + Signal Generator ─────────────────────────────────────────────
// Scope reads an ADC pin (default 39 = battery, safe); Gen outputs a square wave
// via tone() on a pin (default 32, normally free). Change pins with care.
void deuceScope() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    int view = 0;                 // 0 = scope, 1 = signal gen
    int scopePin = 39;            // ADC (battery) by default
    int tbIdx = 1; const int TB[4] = { 50, 150, 500, 1500 }; // us/sample
    int genPin = 32, genFreq = 1000; bool genOn = false;
    static uint16_t buf[320];
    tft.fillScreen(bg);
    bool redrawHdr = true;

    while (true) {
        if (check(EscPress)) break;
        if (check(SelPress)) { if (genOn) { noTone(genPin); genOn = false; } view ^= 1; tft.fillScreen(bg); redrawHdr = true; }

        if (redrawHdr) {
            tft.fillRect(0, 0, W, 17, bg);
            tft.setTextColor(pri, bg); tft.setTextSize(1);
            tft.drawCentreString(view == 0 ? "MINI SCOPE" : "SIGNAL GEN", W / 2, 4, 1);
            tft.drawFastHLine(0, 16, W, sec);
            redrawHdr = false;
        }

        if (view == 0) {
            if (check(NextPress)) tbIdx = (tbIdx + 1) % 4;
            int n = W - 10; if (n > 320) n = 320;
            int tb = TB[tbIdx];
            uint16_t mn = 4095, mx = 0;
            for (int i = 0; i < n; i++) { uint16_t v = analogRead(scopePin); buf[i] = v; if (v < mn) mn = v; if (v > mx) mx = v; delayMicroseconds(tb); }
            tft.fillRect(0, 18, W, H - 30, bg);
            int gy = 30, gh = H - gy - 16;
            tft.drawRect(4, gy, W - 8, gh, sec);
            int px = 0, py = 0;
            for (int i = 0; i < n; i++) {
                int x = 5 + i;
                int y = gy + gh - 2 - (int)((long)buf[i] * (gh - 4) / 4095);
                if (i > 0) tft.drawLine(px, py, x, y, pri);
                px = x; py = y;
            }
            tft.setTextColor(sec, bg);
            char l[48];
            snprintf(l, sizeof(l), "GPIO%d  %.2f-%.2fV  %dus", scopePin, mn * 3.3f / 4095, mx * 3.3f / 4095, tb);
            tft.drawString(l, 4, 20);
            tft.drawString("[NEXT] timebase [SEL] gen [ESC]", 4, H - 11);
        } else {
            if (check(NextPress)) { genOn = !genOn; if (genOn) tone(genPin, genFreq); else noTone(genPin); }
            if (check(UpPress)) { genFreq *= 2; if (genFreq > 100000) genFreq = 100000; if (genOn) tone(genPin, genFreq); }
            if (check(DownPress)) { genFreq /= 2; if (genFreq < 1) genFreq = 1; if (genOn) tone(genPin, genFreq); }
            tft.fillRect(0, 18, W, H - 30, bg);
            tft.setTextColor(genOn ? TFT_GREEN : sec, bg); tft.setTextSize(2);
            tft.drawCentreString(genOn ? "OUTPUT ON" : "output off", W / 2, 26, 1);
            tft.setTextSize(3); tft.setTextColor(pri, bg);
            char f[16]; snprintf(f, sizeof(f), "%d", genFreq);
            tft.drawCentreString(f, W / 2, 52, 1);
            tft.setTextSize(1); tft.drawCentreString("Hz square", W / 2, 82, 1);
            tft.setTextColor(sec, bg);
            char l[40]; snprintf(l, sizeof(l), "GPIO%d", genPin); tft.drawCentreString(l, W / 2, 100, 1);
            tft.drawString("[UP/DN] freq [NEXT] on/off [ESC]", 4, H - 11);
        }
        vTaskDelay(40 / portTICK_PERIOD_MS);
    }
    if (genOn) noTone(genPin);
}

// ── Morse Trainer ─────────────────────────────────────────────────────────────
static const char *MORSE[36] = {
    ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--",
    "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..",
    "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."
};
static String morseFor(char c) {
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c >= 'A' && c <= 'Z') return MORSE[c - 'A'];
    if (c >= '0' && c <= '9') return MORSE[26 + c - '0'];
    if (c == ' ') return "/";
    return "";
}

#define MORSE_PIN 32   // same free pin as the signal gen for the buzzer beep

static void morsePlay(const String &txt, int wpm) {
    int W = tft.width(), H = tft.height();
    int unit = 1200 / wpm; // ms
    for (size_t i = 0; i < txt.length(); i++) {
        if (check(EscPress)) break;
        String m = morseFor(txt[i]);
        if (m == "/") { delay(unit * 4); continue; }
        for (size_t j = 0; j < m.length(); j++) {
            int on = (m[j] == '-') ? unit * 3 : unit;
            tft.fillRect(W / 2 - 40, H / 2 - 20, 80, 40, TFT_GREEN);
            tone(MORSE_PIN, 700);
            delay(on);
            noTone(MORSE_PIN);
            tft.fillRect(W / 2 - 40, H / 2 - 20, 80, 40, bruceConfig.bgColor);
            delay(unit); // intra-char gap
        }
        delay(unit * 2); // letter gap (plus the 1 already) ~3
    }
    noTone(MORSE_PIN);
}

void deuceMorse() {
    String txt = keyboard("SOS", 40, "Text to Morse");
    if (txt.length() == 0) return;
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    int wpm = 12;
    // build morse string
    String ms = "";
    for (size_t i = 0; i < txt.length(); i++) { ms += morseFor(txt[i]); ms += " "; }
    bool redraw = true;
    while (true) {
        if (check(EscPress)) break;
        if (check(SelPress)) { morsePlay(txt, wpm); redraw = true; }
        if (check(UpPress)) { if (wpm < 40) wpm++; redraw = true; }
        if (check(DownPress)) { if (wpm > 5) wpm--; redraw = true; }
        if (redraw) {
            tft.fillScreen(bg);
            tft.setTextColor(pri, bg); tft.drawCentreString("MORSE", W / 2, 4, 1);
            tft.drawFastHLine(0, 16, W, sec);
            tft.setTextColor(sec, bg);
            tft.drawString(txt.c_str(), 6, 22);
            tft.setTextColor(pri, bg);
            // wrap morse
            int x = 6, y = 40;
            for (size_t i = 0; i < ms.length(); i++) {
                char ch[2] = { ms[i], 0 };
                int w = tft.textWidth(ch);
                if (x + w > W - 6) { x = 6; y += 12; if (y > H - 40) break; }
                tft.drawString(ch, x, y); x += w;
            }
            tft.setTextColor(sec, bg);
            char l[32]; snprintf(l, sizeof(l), "WPM %d", wpm); tft.drawString(l, 6, H - 24);
            tft.drawString("[SEL] play [UP/DN] WPM [ESC]", 4, H - 11);
            redraw = false;
        }
        vTaskDelay(30 / portTICK_PERIOD_MS);
    }
}
