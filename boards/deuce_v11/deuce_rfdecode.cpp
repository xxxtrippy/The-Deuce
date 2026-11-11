// THE DEUCE v1.1 -- live Sub-GHz protocol decoder. See .h.

#include "deuce_rfdecode.h"

#include <Arduino.h>
#include <globals.h>

#include "core/display.h"
#include "modules/rf/protocols/rf_decoder.h"
#include "modules/rf/rf_utils.h"

// a short, common curated list to cycle with NEXT
static const float DEUCE_RF_FREQS[] = { 300.00f, 303.87f, 310.00f, 315.00f, 318.00f,
                                        390.00f, 418.00f, 433.07f, 433.92f, 434.42f,
                                        868.35f, 915.00f };
static const int DEUCE_RF_NFREQS = sizeof(DEUCE_RF_FREQS) / sizeof(DEUCE_RF_FREQS[0]);

static int nearestFreqIdx(float f) {
    int best = 8; float bd = 1e9f; // default 433.92
    for (int i = 0; i < DEUCE_RF_NFREQS; i++) {
        float d = fabsf(DEUCE_RF_FREQS[i] - f);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

void deuceSubghzDecoder() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;

    int fIdx = nearestFreqIdx(bruceConfigPins.rfFreq);
    int total = 0;
    String lp = "--", lpreset = "--", lkey = "--"; int lbit = 0, lte = 0; bool lraw = false;
    bool freqChanged = true, headerDrawn = false;

    deinitRfModule();

    while (true) {
        float freq = DEUCE_RF_FREQS[fIdx];

        if (freqChanged) {
            tft.fillScreen(bg);
            headerDrawn = false;
            if (!initRfModule("rx", freq)) {
                tft.setTextColor(TFT_RED, bg);
                tft.drawCentreString("RF module init failed", W / 2, H / 2, 1);
                delay(1200);
                break;
            }
            freqChanged = false;
        }

        RfRxSession rx;
        if (!rx.begin()) {
            deinitRfModule();
            tft.setTextColor(TFT_RED, bg);
            tft.drawCentreString("RX begin failed", W / 2, H / 2, 1);
            delay(1200);
            break;
        }

        // inner capture loop on this frequency
        bool leave = false;
        uint32_t lastDraw = 0;
        while (true) {
            if (check(EscPress)) { leave = true; break; }
            if (check(NextPress)) { fIdx = (fIdx + 1) % DEUCE_RF_NFREQS; freqChanged = true; break; }
            if (check(PrevPress)) { fIdx = (fIdx - 1 + DEUCE_RF_NFREQS) % DEUCE_RF_NFREQS; freqChanged = true; break; }
            if (check(SelPress)) { total = 0; lp = lpreset = lkey = "--"; lbit = lte = 0; lraw = false; headerDrawn = false; }

            std::vector<int> durations;
            bool got = false;
            if (rx.poll(durations)) {
                RfCodes rc;
                if (rf_decode_keeloq(durations, rc) || rf_decode_ook(durations, rc)) {
                    lp = rc.protocol.length() ? rc.protocol : "RcSwitch";
                    lpreset = rc.preset.length() ? rc.preset : "Ook";
                    lbit = rc.Bit; lte = rc.te; lraw = false;
                    lkey = "0x" + String((uint32_t)(rc.key >> 32), HEX) + String((uint32_t)rc.key, HEX);
                    if ((rc.key >> 32) == 0) lkey = "0x" + String((uint32_t)rc.key, HEX);
                    total++; got = true;
                } else {
                    String data; bool hasCrc = false; uint64_t crc = 0; std::vector<int> idx; int rb = 0, rt = 0;
                    int tr = rf_build_raw(durations, data, hasCrc, crc, idx, rb, rt);
                    if (tr > 24) { lp = "RAW"; lpreset = "Ook270Async"; lbit = tr; lte = rt; lraw = true; lkey = "(unparsed)"; total++; got = true; }
                }
            }

            uint32_t now = millis();
            if (got || !headerDrawn || now - lastDraw > 400) {
                lastDraw = now;
                if (!headerDrawn) {
                    tft.fillScreen(bg);
                    tft.setTextColor(pri, bg); tft.setTextSize(1);
                    tft.drawCentreString("SUB-GHZ DECODER", W / 2, 3, 1);
                    tft.drawFastHLine(0, 15, W, sec);
                    headerDrawn = true;
                }
                char l[48];
                tft.fillRect(0, 18, W, H - 30, bg);
                tft.setTextColor(sec, bg);
                snprintf(l, sizeof(l), "%.2f MHz   caught: %d", freq, total);
                tft.drawString(l, 6, 20);
                tft.setTextColor(lraw ? TFT_ORANGE : TFT_GREEN, bg); tft.setTextSize(2);
                tft.drawString(lp.c_str(), 6, 36);
                tft.setTextSize(1); tft.setTextColor(pri, bg);
                snprintf(l, sizeof(l), "preset: %s", lpreset.c_str()); tft.drawString(l, 6, 60);
                snprintf(l, sizeof(l), "bits: %d   te: %d us", lbit, lte); tft.drawString(l, 6, 74);
                tft.drawString("key:", 6, 88);
                tft.setTextColor(sec, bg);
                // wrap key if long
                String k = lkey; int kx = 34, ky = 88;
                for (size_t i = 0; i < k.length(); i += 24) {
                    tft.drawString(k.substring(i, i + 24).c_str(), kx, ky); kx = 6; ky += 12;
                }
                tft.setTextColor(sec, bg);
                tft.drawString("[NEXT/PREV] freq  [SEL] clear", 4, H - 22);
                tft.drawString("[ESC] exit", 4, H - 11);
            }
            vTaskDelay(15 / portTICK_PERIOD_MS);
        }

        rx.end();
        if (leave) { deinitRfModule(); break; }
        // freq change: deinit happens inside init path; ensure clean
        deinitRfModule();
    }
}
