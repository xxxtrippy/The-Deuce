// THE DEUCE v1.1 -- 433 MHz weather-sensor receiver. See .h.

#include "deuce_weather433.h"

#include <Arduino.h>
#include <globals.h>

#include "core/display.h"
#include "modules/rf/protocols/rf_decoder.h"
#include "modules/rf/rf_utils.h"

// Nexus-family frame: 36 bits, each bit = one HIGH pulse (~0.5ms) + a LOW gap
// (~1ms -> 0, ~2ms -> 1). Frames are separated by a ~4ms sync gap and repeated.
//  bits  0..7  : id (reset on battery change)
//  bit   8     : battery ok (1 = ok)
//  bit   9     : (unused)
//  bits 10..11 : channel (0 -> ch1)
//  bits 12..23 : temperature, signed 12-bit, deci-degrees C
//  bits 24..27 : constant 0xF
//  bits 28..35 : humidity (percent)

struct NexusFrame { uint8_t id; bool batt; uint8_t chan; int16_t tempC10; uint8_t hum; };

static bool gapBit(int g, int &bit) {         // g = magnitude of a LOW gap (us)
    if (g > 800 && g < 1500) { bit = 0; return true; }
    if (g > 1700 && g < 2600) { bit = 1; return true; }
    return false;
}

// Try to pull a 36-bit Nexus frame out of a duration burst starting at index i.
static bool decodeNexusAt(const std::vector<int> &d, size_t start, NexusFrame &f) {
    uint64_t bits = 0; int n = 0;
    size_t i = start;
    while (i + 1 < d.size() && n < 36) {
        int pulse = d[i], gap = d[i + 1];
        if (pulse <= 0 || gap >= 0) return false;       // expect +pulse then -gap
        if (pulse < 150 || pulse > 1000) return false;  // pulse width sanity
        int b;
        if (!gapBit(-gap, b)) return false;
        bits = (bits << 1) | (uint64_t)b;
        n++; i += 2;
    }
    if (n < 36) return false;
    // constant nibble check (bits 24..27 == 0xF)
    uint8_t konst = (bits >> 8) & 0x0F;
    if (konst != 0x0F) return false;
    f.id = (bits >> 28) & 0xFF;
    f.batt = (bits >> 27) & 0x01;
    f.chan = ((bits >> 24) & 0x03) + 1;
    int16_t t = (bits >> 12) & 0xFFF;
    if (t & 0x800) t -= 0x1000;                          // sign-extend 12-bit
    f.tempC10 = t;
    f.hum = bits & 0xFF;
    if (f.hum > 100) return false;
    if (f.tempC10 < -400 || f.tempC10 > 800) return false; // -40..80 C
    return true;
}

static bool sameFrame(const NexusFrame &a, const NexusFrame &b) {
    return a.id == b.id && a.tempC10 == b.tempC10 && a.hum == b.hum && a.chan == b.chan && a.batt == b.batt;
}

// Scan a whole burst. Returns the first decodable frame in `f`, and sets
// `twoInBurst` when a second, identical frame is also found in the same burst
// (which, since the protocol has no CRC, is strong enough to trust at once).
static bool scanBurst(const std::vector<int> &d, NexusFrame &f, bool &twoInBurst) {
    twoInBurst = false;
    bool haveFirst = false;
    for (size_t i = 0; i + 72 < d.size(); i++) {
        if (d[i] <= 0) continue;
        NexusFrame g;
        if (!decodeNexusAt(d, i, g)) continue;
        if (!haveFirst) { f = g; haveFirst = true; i += 70; }  // skip past this frame
        else if (sameFrame(f, g)) { twoInBurst = true; return true; }
    }
    return haveFirst;
}

void deuceWeather433() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;

    deinitRfModule();
    if (!initRfModule("rx", 433.92f)) {
        displayError("RF init failed", true);
        return;
    }
    RfRxSession rx;
    if (!rx.begin()) { deinitRfModule(); displayError("RX begin failed", true); return; }

    bool have = false; NexusFrame cur{}; uint32_t lastRx = 0; int frames = 0;
    NexusFrame pend{}; bool havePend = false;
    bool showF = true; // emphasise Fahrenheit by default
    bool redraw = true;

    while (true) {
        if (check(EscPress)) break;
        if (check(SelPress)) { showF = !showF; redraw = true; }

        std::vector<int> durations;
        if (rx.poll(durations)) {
            NexusFrame f; bool twoInBurst = false;
            if (scanBurst(durations, f, twoInBurst)) {
                if (twoInBurst) {
                    // two identical frames in one burst -> trust immediately
                    cur = f; have = true; lastRx = millis(); frames++; redraw = true;
                    havePend = false;
                } else if (havePend && sameFrame(pend, f)) {
                    cur = f; have = true; lastRx = millis(); frames++; redraw = true;
                    havePend = false;
                } else {
                    pend = f; havePend = true;
                }
            }
        }

        if (redraw || (have && millis() - lastRx > 2000)) {
            redraw = false;
            tft.fillScreen(bg);
            tft.setTextColor(pri, bg); tft.setTextSize(1);
            tft.drawCentreString("433 WEATHER RX", W / 2, 3, 1);
            tft.drawFastHLine(0, 15, W, sec);
            if (!have) {
                tft.setTextColor(sec, bg);
                tft.drawCentreString("Listening on 433.92 MHz", W / 2, 50, 1);
                tft.drawCentreString("for a Nexus-type sensor...", W / 2, 66, 1);
                tft.drawCentreString(havePend ? "(got 1 frame, confirming)" : "", W / 2, 84, 1);
            } else {
                float c = cur.tempC10 / 10.0f;
                float ff = c * 9.0f / 5.0f + 32.0f;
                char big[16];
                tft.setTextSize(4); tft.setTextColor(pri, bg);
                snprintf(big, sizeof(big), "%.0f", showF ? ff : c);
                tft.drawString(big, 20, 28);
                int tw = tft.textWidth(big);
                tft.setTextSize(2);
                tft.drawString(showF ? "F" : "C", 20 + tw + 6, 34);
                // humidity
                tft.setTextSize(3); tft.setTextColor(sec, bg);
                char hs[8]; snprintf(hs, sizeof(hs), "%d%%", cur.hum);
                tft.drawRightString(hs, W - 10, 36, 1);
                // precise line
                tft.setTextSize(1); tft.setTextColor(pri, bg);
                char l[40];
                snprintf(l, sizeof(l), "%.1f C / %.1f F", c, ff);
                tft.drawString(l, 10, 80);
                snprintf(l, sizeof(l), "ch %d   id 0x%02X   batt %s", cur.chan, cur.id, cur.batt ? "OK" : "LOW");
                tft.setTextColor(sec, bg); tft.drawString(l, 10, 94);
                snprintf(l, sizeof(l), "frames %d   %lus ago", frames, (unsigned long)((millis() - lastRx) / 1000));
                tft.drawString(l, 10, 108);
            }
            tft.setTextColor(sec, bg); tft.setTextSize(1);
            tft.drawString("[SEL] C/F   [ESC] exit", 4, H - 11);
        }
        vTaskDelay(15 / portTICK_PERIOD_MS);
    }
    rx.end();
    deinitRfModule();
}
