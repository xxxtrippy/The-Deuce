// THE DEUCE v1.1 -- Faraday cage / RF-shielding tester (see .h for the method).
//
// Live RSSI meter on one sub-GHz frequency. Capture a reference level with the
// device outside the enclosure, then measure inside: attenuation = ref - live
// (dB). A good shielded bag/cage drops a strong source by >=20 dB; little or no
// drop means it leaks. Peak-hold tracks the strongest reading since the last
// reference capture so a brief fob press is easy to read.

#include "deuce_faraday.h"

#include <Arduino.h>
#include <globals.h>

#include "core/display.h"
#include "deuce_seg14.h"
#include "modules/rf/rf_utils.h" // initRfModule/deinitRfModule/setMHZ + ELECHOUSE_cc1101

#define FAR_HDR 26

static int farClampBar(int rssi) {
    // map RSSI dBm (-110..-20) to 0..100%
    int p = (rssi + 110) * 100 / 90;
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    return p;
}

static void farDrawStatic(float freq) {
    int W = tft.width();
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    tft.fillScreen(bg);
    tft.setTextSize(1);
    tft.setTextColor(pri, bg);
    tft.drawString("FARADAY TEST", 6, 6);
    String f = String(freq, 2) + " MHz";
    tft.setTextColor(sec, bg);
    tft.drawString(f, W - 6 - tft.textWidth(f), 6);
    tft.drawFastHLine(0, FAR_HDR - 2, W, sec);
    tft.setTextColor(sec, bg);
    tft.drawString("[SEL] set reference   [ESC] exit", 6, tft.height() - 12);
}

void deuceFaradayTest() {
    if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) {
        displayError("Faraday test needs a CC1101", true);
        return;
    }

    // frequency pick (where a source will be present)
    float freq = 433.92;
    options = {
        {"433.92 MHz", [&]() { freq = 433.92; }},
        {"315 MHz",    [&]() { freq = 315.00; }},
        {"868 MHz",    [&]() { freq = 868.35; }},
        {"915 MHz",    [&]() { freq = 915.00; }},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "Faraday Frequency");

    float savedFreq = bruceConfigPins.rfFreq;
    bruceConfigPins.rfFreq = freq;
    if (!initRfModule("rx", freq)) {
        bruceConfigPins.rfFreq = savedFreq;
        deinitRfModule();
        displayError("RF init failed", true);
        return;
    }
    setMHZ(freq);

    farDrawStatic(freq);

    float live = -110;   // smoothed live RSSI
    int ref = 0;         // captured reference dBm
    bool haveRef = false;
    int peak = -140;     // peak-hold since last reference
    uint32_t lastDraw = 0;

    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    int W = tft.width();

    while (true) {
        if (check(EscPress)) break;

        int raw = ELECHOUSE_cc1101.getRssi();
        tft.drawPixel(0, 0, bg); // nudge shared SPI (mirrors rf_scan)
        live += (raw - live) * 0.30f;
        if ((int)live > peak) peak = (int)live;

        if (check(SelPress)) {
            ref = (int)live;
            haveRef = true;
            peak = (int)live;
        }

        if (millis() - lastDraw > 90) {
            lastDraw = millis();

            // big live dBm (seg14)
            tft.fillRect(0, FAR_HDR + 4, W, 46, bg);
            String ls = String((int)live);
            int cw = 16, chh = 40, gap = 4, th = 4;
            int lw = deuceSeg14Width(ls, cw, gap);
            uint16_t lc = (live > -55) ? TFT_RED : (live > -80 ? 0xFD20 : TFT_GREEN);
            deuceSeg14At(ls, 10, FAR_HDR + 6, cw, chh, gap, th, lc, bg);
            tft.setTextColor(sec, bg);
            tft.drawString("dBm", 10 + lw + 4, FAR_HDR + 8);

            // live signal bar
            int by = FAR_HDR + 54, bw = W - 20, bh = 14;
            tft.drawRect(10, by, bw, bh, sec);
            int fillw = (bw - 2) * farClampBar((int)live) / 100;
            tft.fillRect(11, by + 1, bw - 2, bh - 2, bg);
            tft.fillRect(11, by + 1, fillw, bh - 2, lc);

            // reference + attenuation block
            int ry = by + 26;
            tft.fillRect(0, ry, W, 70, bg);
            tft.setTextColor(pri, bg);
            if (!haveRef) {
                tft.drawString("Capture a REFERENCE outside", 10, ry);
                tft.drawString("the bag/cage, then test inside.", 10, ry + 12);
                char pk[24];
                snprintf(pk, sizeof(pk), "peak: %d dBm", peak);
                tft.setTextColor(sec, bg);
                tft.drawString(pk, 10, ry + 30);
            } else {
                char rs[28];
                snprintf(rs, sizeof(rs), "REF %d dBm   PEAK %d dBm", ref, peak);
                tft.setTextColor(sec, bg);
                tft.drawString(rs, 10, ry);

                int atten = ref - (int)live; // positive = signal dropped
                if (atten < 0) atten = 0;
                const char *verdict;
                uint16_t vc;
                if (atten >= 20) {
                    verdict = "SHIELDED";
                    vc = TFT_GREEN;
                } else if (atten >= 10) {
                    verdict = "PARTIAL";
                    vc = 0xFD20;
                } else {
                    verdict = "LEAKY";
                    vc = TFT_RED;
                }
                // big attenuation number
                String as = String(atten);
                int cw2 = 14, chh2 = 30, gap2 = 4, th2 = 3;
                int aw = deuceSeg14Width(as, cw2, gap2);
                deuceSeg14At(as, 10, ry + 16, cw2, chh2, gap2, th2, vc, bg);
                tft.setTextColor(sec, bg);
                tft.drawString("dB atten", 10 + aw + 4, ry + 18);
                tft.setTextSize(2);
                tft.setTextColor(vc, bg);
                tft.drawString(verdict, 10 + aw + 4, ry + 30);
                tft.setTextSize(1);
            }
        }
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }

    bruceConfigPins.rfFreq = savedFreq;
    deinitRfModule();
}
