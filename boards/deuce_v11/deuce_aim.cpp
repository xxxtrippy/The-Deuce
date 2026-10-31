// THE DEUCE v1.1 -- live AIM screen. See .h.

#include "deuce_aim.h"

#include <Arduino.h>
#include <globals.h>
#include <math.h>

#include "core/display.h"
#include "deuce_mmc5983.h"
#include "modules/rf/rf_utils.h"

#if __has_include(<RF24.h>)
  #include <RF24.h>
  #define HAVE_NRF24 1
#endif
#if __has_include(<WiFi.h>)
  #include <WiFi.h>
  #define HAVE_WIFI_RSSI 1
#endif

// ── Band model ────────────────────────────────────────────────────────────────
enum AimBand { AIM_CC = 0, AIM_WIFI = 1, AIM_NRF = 2, AIM_N = 3 };
static const char* AIM_NAMES[AIM_N] = { "CC1101", "WiFi/BT", "NRF24" };
static const uint16_t AIM_COLORS[AIM_N] = { TFT_GREEN, TFT_CYAN, TFT_YELLOW };

// dBm window mapped across the bar
#define AIM_DBM_MIN (-100.0f)
#define AIM_DBM_MAX (-30.0f)

static float g_val[AIM_N];     // current: dBm for CC/WiFi, 0..100 (%) for NRF
static float g_peak[AIM_N];    // peak-hold
static bool  g_isPct[AIM_N] = { false, false, true };
static float g_ccFreq = 433.92f;

// ── Readers ───────────────────────────────────────────────────────────────────
static int aimReadCC1101(int n = 6) {
    long s = 0;
    for (int i = 0; i < n; i++) { s += ELECHOUSE_cc1101.getRssi(); delayMicroseconds(300); }
    return (int)(s / n);
}

#ifdef HAVE_NRF24
extern RF24 radio; // Bruce's NRF24 instance
static float aimReadNRF(int n = 16) {
    int hits = 0;
    for (int i = 0; i < n; i++) { if (radio.testCarrier()) hits++; delayMicroseconds(300); }
    return (100.0f * hits) / n;   // carrier-detect duty %
}
#endif

// ── Drawing ───────────────────────────────────────────────────────────────────
static void aimDrawStatic(bool mag) {
    int W = tft.width(), H = tft.height();
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    tft.fillScreen(bg);
    tft.setTextSize(1);
    // small centred title
    const char* t = "LIVE AIM";
    tft.setTextColor(pri, bg);
    tft.drawString(t, (W - tft.textWidth(t)) / 2, 4);
    tft.drawFastHLine(0, 16, W, sec);
    tft.setTextColor(sec, bg);
    const char* f = mag ? "[SEL] reset peak   [ESC] exit"
                        : "[SEL] reset peak   [ESC] exit";
    tft.drawString(f, 4, H - 11);
}

static int aimBarY(int i, int H) { return 26 + i * ((H - 40) / AIM_N); }

static void aimDrawBand(int i) {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, sec = bruceConfig.secColor, col = AIM_COLORS[i];
    int y = aimBarY(i, H);
    int bx = 10, bw = W - 20, bh = 18;
    int by = y + 13;

    // clear row region
    tft.fillRect(0, y, W, (H - 40) / AIM_N - 2, bg);

    // label + value
    tft.setTextColor(col, bg);
    tft.drawString(AIM_NAMES[i], bx, y);
    char v[20];
    if (g_isPct[i]) snprintf(v, sizeof(v), "%d%% CD", (int)g_val[i]);
    else            snprintf(v, sizeof(v), "%d dBm", (int)g_val[i]);
    tft.drawString(v, W - 10 - tft.textWidth(v), y);

    // bar frame
    tft.drawRect(bx, by, bw, bh, sec);

    // fill fraction
    float frac;
    if (g_isPct[i]) frac = g_val[i] / 100.0f;
    else            frac = (g_val[i] - AIM_DBM_MIN) / (AIM_DBM_MAX - AIM_DBM_MIN);
    if (frac < 0) frac = 0; if (frac > 1) frac = 1;
    int fw = (int)(frac * (bw - 2));
    tft.fillRect(bx + 1, by + 1, bw - 2, bh - 2, bg);
    if (fw > 0) tft.fillRect(bx + 1, by + 1, fw, bh - 2, col);

    // peak-hold tick
    float pf;
    if (g_isPct[i]) pf = g_peak[i] / 100.0f;
    else            pf = (g_peak[i] - AIM_DBM_MIN) / (AIM_DBM_MAX - AIM_DBM_MIN);
    if (pf < 0) pf = 0; if (pf > 1) pf = 1;
    int px = bx + 1 + (int)(pf * (bw - 2));
    tft.drawFastVLine(px, by - 2, bh + 4, TFT_WHITE);
}

static void aimDrawHeading(float hdg, bool have) {
    int W = tft.width();
    uint16_t bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    tft.fillRect(W - 70, 3, 68, 11, bg);
    if (!have) return;
    char h[16]; snprintf(h, sizeof(h), "HDG %03d", (int)hdg);
    tft.setTextColor(sec, bg);
    tft.drawString(h, W - 6 - tft.textWidth(h), 4);
}

// ── CC1101 band picker ────────────────────────────────────────────────────────
static void aimPickFreq() {
    options = {
        {"433.92 MHz", [&]() { g_ccFreq = 433.92f; }},
        {"315 MHz",    [&]() { g_ccFreq = 315.00f; }},
        {"868 MHz",    [&]() { g_ccFreq = 868.35f; }},
        {"915 MHz",    [&]() { g_ccFreq = 915.00f; }},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "CC1101 band");
}

// ── Main entry ────────────────────────────────────────────────────────────────
void deuceAimScreen() {
    if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) {
        displayError("Aim needs CC1101", true);
        return;
    }

    aimPickFreq();

    for (int i = 0; i < AIM_N; i++) { g_val[i] = g_isPct[i] ? 0 : AIM_DBM_MIN; g_peak[i] = g_val[i]; }

    bool mag = mmcBegin();

    // CC1101 up in rx
    float savedFreq = bruceConfigPins.rfFreq;
    bruceConfigPins.rfFreq = g_ccFreq;
    if (!initRfModule("rx", g_ccFreq)) { displayError("CC1101 init failed", true); return; }
    setMHZ(g_ccFreq);

#ifdef HAVE_WIFI_RSSI
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    WiFi.scanNetworks(true, true, false, 120);   // async
#endif
#ifdef HAVE_NRF24
    radio.setChannel(76);     // 2.476 GHz
    radio.startListening();
#endif

    aimDrawStatic(mag);
    for (int i = 0; i < AIM_N; i++) aimDrawBand(i);

    uint32_t lastDraw = 0, lastHdg = 0;

    while (true) {
        if (check(EscPress)) break;
        if (check(SelPress)) { for (int i = 0; i < AIM_N; i++) g_peak[i] = g_val[i]; }

        // CC1101 live
        g_val[AIM_CC] = aimReadCC1101();
        if (g_val[AIM_CC] > g_peak[AIM_CC]) g_peak[AIM_CC] = g_val[AIM_CC];

#ifdef HAVE_NRF24
        g_val[AIM_NRF] = aimReadNRF();
        if (g_val[AIM_NRF] > g_peak[AIM_NRF]) g_peak[AIM_NRF] = g_val[AIM_NRF];
#endif

#ifdef HAVE_WIFI_RSSI
        int done = WiFi.scanComplete();
        if (done >= 0) {
            int best = -100;
            for (int i = 0; i < done; i++) {
                int ch = WiFi.channel(i);
                if (ch >= 1 && ch <= 13) { int r = WiFi.RSSI(i); if (r > best) best = r; }
            }
            g_val[AIM_WIFI] = best;
            if (g_val[AIM_WIFI] > g_peak[AIM_WIFI]) g_peak[AIM_WIFI] = g_val[AIM_WIFI];
            WiFi.scanDelete();
            WiFi.scanNetworks(true, true, false, 120);  // kick next async scan
        }
#endif

        if (millis() - lastDraw > 90) {
            for (int i = 0; i < AIM_N; i++) aimDrawBand(i);
            lastDraw = millis();
        }
        if (mag && millis() - lastHdg > 200) {
            float gx, gy, gz;
            if (mmcReadGauss(gx, gy, gz)) {
                float hd = atan2f(gy, gx) * 57.2957795f;
                if (hd < 0) hd += 360.0f;
                aimDrawHeading(hd, true);
            }
            lastHdg = millis();
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

#ifdef HAVE_WIFI_RSSI
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
#endif
    deinitRfModule();
    bruceConfigPins.rfFreq = savedFreq;
}
