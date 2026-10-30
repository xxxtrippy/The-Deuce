// THE DEUCE v1.1 -- unified multi-antenna pattern plotter. See .h.

#include "deuce_antplot.h"

#include <Arduino.h>
#include <globals.h>
#include <math.h>

#include "core/display.h"
#include "deuce_mmc5983.h"
#include "modules/rf/rf_utils.h"

// ── NRF24 carrier-detect RSSI ────────────────────────────────────────────────
// We use the RF24 library already linked by Bruce; just need carrier-detect
// register to get a proxy RSSI reading (−90 dBm if CD=0, −64 dBm if CD=1).
// Include guard: only pull in RF24 if the header is present.
#if __has_include(<RF24.h>)
  #include <RF24.h>
  #define HAVE_NRF24 1
#endif
#if __has_include(<WiFi.h>)
  #include <WiFi.h>
  #define HAVE_WIFI_RSSI 1
#endif

// ── Layout constants ─────────────────────────────────────────────────────────
#define AP_HDR    24
#define AP_BINS   36        // 10-degree bins
#define AP_RAD2DEG 57.2957795f
#define AP_DEG2RAD  0.0174532925f
#define AP_MAX_ANT  3

// ── Antenna descriptors ───────────────────────────────────────────────────────
enum AntId { ANT_CC1101 = 0, ANT_WIFI = 1, ANT_NRF24 = 2 };
static const char* ANT_NAMES[AP_MAX_ANT] = { "CC1101", "WiFi/BT", "NRF24" };
// Display colours per antenna: green / cyan / yellow
static const uint16_t ANT_COLORS[AP_MAX_ANT] = { TFT_GREEN, TFT_CYAN, TFT_YELLOW };

// ── Per-sweep state ───────────────────────────────────────────────────────────
struct ApSweep {
    float  bin[AP_BINS];
    bool   set[AP_BINS];
    bool   done;          // sweep completed
    bool   enabled;
    float  freq;          // MHz (for CC1101 only; 2400 for others)
};
static ApSweep g_sweep[AP_MAX_ANT];

// ── Polar-plot geometry ───────────────────────────────────────────────────────
static int RCX, RCY, RR;

// ── Helpers ───────────────────────────────────────────────────────────────────
static void apResetSweep(int a) {
    for (int i = 0; i < AP_BINS; i++) {
        g_sweep[a].bin[i] = -200;
        g_sweep[a].set[i] = false;
    }
    g_sweep[a].done = false;
}

static void apStats(int a, int &peakBin, float &peak, float &fb, int &hpbw, int &filled) {
    peakBin = -1; peak = -300; filled = 0; fb = 0; hpbw = 0;
    for (int i = 0; i < AP_BINS; i++) {
        if (!g_sweep[a].set[i]) continue;
        filled++;
        if (g_sweep[a].bin[i] > peak) { peak = g_sweep[a].bin[i]; peakBin = i; }
    }
    if (peakBin < 0) return;
    int opp = (peakBin + AP_BINS / 2) % AP_BINS;
    if (g_sweep[a].set[opp]) fb = peak - g_sweep[a].bin[opp];
    int span = 1;
    for (int d = 1; d < AP_BINS / 2; d++) {
        int pa = (peakBin + d) % AP_BINS, pb = (peakBin - d + AP_BINS) % AP_BINS;
        bool ina = g_sweep[a].set[pa] && (peak - g_sweep[a].bin[pa] <= 3.0f);
        bool inb = g_sweep[a].set[pb] && (peak - g_sweep[a].bin[pb] <= 3.0f);
        if (ina) span++;
        if (inb) span++;
        if (!ina && !inb) break;
    }
    hpbw = span * (360 / AP_BINS);
}

// Estimated gain dBi from HPBW (Kraus approximation for simple monopole/dipole)
static float apEstGain(int hpbw) {
    if (hpbw <= 0) return 0;
    // G ≈ 10*log10(41253 / (hpbw_az * hpbw_el)); assume azimuth-only, el ~120 deg for monopole
    float g = 10.0f * log10f(41253.0f / ((float)hpbw * 120.0f));
    return g < -3.0f ? -3.0f : g;
}

static void apPoint(float relDeg, float rad, int &x, int &y) {
    float a = relDeg * AP_DEG2RAD;
    x = RCX + (int)(rad * cosf(a));
    y = RCY - (int)(rad * sinf(a));
}

// ── Draw static background ────────────────────────────────────────────────────
static void apDrawBg(const char *label, bool mag) {
    int W = tft.width(), H = tft.height();
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    tft.fillScreen(bg);
    tft.setTextSize(1);
    tft.setTextColor(pri, bg);
    tft.drawString(label, 6, 6);
    tft.setTextColor(sec, bg);
    String ms = mag ? "MAG" : "STEP";
    tft.drawString(ms, W - 6 - tft.textWidth(ms), 6);
    tft.drawFastHLine(0, AP_HDR - 2, W, sec);
    RR  = 92;
    RCX = 8 + RR;
    RCY = AP_HDR + 8 + RR;
    if (RCY + RR > H - 28) RCY = H - 28 - RR;
}

static void apDrawGrid() {
    uint16_t pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    tft.drawCircle(RCX, RCY, RR,        sec);
    tft.drawCircle(RCX, RCY, (RR*2)/3,  sec);
    tft.drawCircle(RCX, RCY, RR/3,      sec);
    tft.drawLine(RCX, RCY, RCX + RR, RCY,      pri); // boresight
    tft.drawLine(RCX, RCY, RCX - RR, RCY,      sec);
    tft.drawLine(RCX, RCY - RR, RCX, RCY + RR, sec);
}

// Draw one antenna's sweep on the polar plot (colour-coded)
static void apDrawSweep(int a) {
    if (!g_sweep[a].enabled) return;
    int peakBin, hpbw, filled; float peak, fb;
    apStats(a, peakBin, peak, fb, hpbw, filled);
    if (peakBin < 0 || filled < 2) return;
    float floorv = peak - 30.0f;
    int px = -1, py = -1;
    uint16_t col = ANT_COLORS[a];
    for (int i = 0; i <= AP_BINS; i++) {
        int b = i % AP_BINS;
        if (!g_sweep[a].set[b]) { px = -1; continue; }
        float norm = (g_sweep[a].bin[b] - floorv) / 30.0f;
        if (norm < 0.04f) norm = 0.04f;
        if (norm > 1.0f)  norm = 1.0f;
        int x, y; apPoint(b * (360.0f / AP_BINS), norm * RR, x, y);
        if (px >= 0) tft.drawLine(px, py, x, y, col);
        px = x; py = y;
    }
    // peak dot
    if (peakBin >= 0) {
        int mx, my; apPoint(peakBin * (360.0f / AP_BINS), (float)RR, mx, my);
        tft.fillCircle(mx, my, 3, col);
    }
}

// Stats panel (right side, multiple antennas stacked)
static void apDrawStats(int singleAnt = -1) {
    int W = tft.width();
    int sx = RCX + RR + 10;
    if (sx < 212) sx = 212;
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    tft.fillRect(sx, AP_HDR + 2, W - sx - 1, tft.height() - AP_HDR - 28, bg);
    int y = AP_HDR + 6;
    for (int a = 0; a < AP_MAX_ANT; a++) {
        if (singleAnt >= 0 && a != singleAnt) continue;
        if (!g_sweep[a].enabled) continue;
        int peakBin, hpbw, filled; float peak, fb;
        apStats(a, peakBin, peak, fb, hpbw, filled);
        tft.setTextColor(ANT_COLORS[a], bg);
        tft.drawString(ANT_NAMES[a], sx, y); y += 13;
        tft.setTextColor(pri, bg);
        if (peakBin >= 0) {
            char l[22];
            snprintf(l, sizeof(l), "%ddBm@%ddeg", (int)peak, peakBin * (360 / AP_BINS));
            tft.drawString(l, sx, y); y += 12;
            snprintf(l, sizeof(l), "F/B%ddB H%ddeg", (int)fb, hpbw);
            tft.drawString(l, sx, y); y += 12;
            float g = apEstGain(hpbw);
            snprintf(l, sizeof(l), "~%.1fdBi", g);
            tft.setTextColor(sec, bg);
            tft.drawString(l, sx, y); y += 14;
        } else {
            tft.setTextColor(sec, bg);
            tft.drawString("no data", sx, y); y += 14;
        }
    }
}

// Full polar redraw
static void apRedraw(int singleAnt = -1) {
    uint16_t bg = bruceConfig.bgColor;
    tft.fillRect(RCX - RR - 2, RCY - RR - 2, 2*RR+4, 2*RR+4, bg);
    apDrawGrid();
    if (singleAnt >= 0) {
        apDrawSweep(singleAnt);
    } else {
        for (int a = 0; a < AP_MAX_ANT; a++) apDrawSweep(a);
    }
    apDrawStats(singleAnt);
}

// ── RSSI readers per antenna ──────────────────────────────────────────────────
static int apReadCC1101(int n = 8) {
    long s = 0;
    for (int i = 0; i < n; i++) {
        s += ELECHOUSE_cc1101.getRssi();
        tft.drawPixel(0, 0, bruceConfig.bgColor);
        delayMicroseconds(400);
    }
    return (int)(s / n);
}

#ifdef HAVE_WIFI_RSSI
// Return RSSI of the loudest AP on the 2.4 GHz band, or -100 if none seen.
static int apReadWifi(int n = 4) {
    int best = -100;
    for (int t = 0; t < n; t++) {
        int found = WiFi.scanNetworks(false, true, false, 200);
        for (int i = 0; i < found; i++) {
            int ch = WiFi.channel(i);
            if (ch >= 1 && ch <= 13) {
                int r = WiFi.RSSI(i);
                if (r > best) best = r;
            }
        }
        WiFi.scanDelete();
        delay(80);
    }
    return best;
}
#endif

#ifdef HAVE_NRF24
// Attempt a carrier-detect read. Returns a proxy dBm.
extern RF24 radio; // defined in Bruce's NRF24 module
static int apReadNRF(int n = 8) {
    long s = 0;
    radio.setChannel(76); // 2.476 GHz
    radio.startListening();
    for (int i = 0; i < n; i++) {
        delay(2);
        bool cd = radio.testCarrier();
        s += cd ? -64 : -90;
    }
    radio.stopListening();
    return (int)(s / n);
}
#endif

// ── Antenna toggle selection screen ──────────────────────────────────────────
static void apSelectAntennas() {
    int W = tft.width(), H = tft.height();
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;

    int sel = 0; // cursor
    bool redraw = true;

    while (true) {
        if (redraw) {
            tft.fillScreen(bg);
            tft.setTextSize(1);
            tft.setTextColor(pri, bg);
            tft.drawString("ANT SELECT", 6, 6);
            tft.drawFastHLine(0, 20, W, sec);
            for (int a = 0; a < AP_MAX_ANT; a++) {
                int y = 30 + a * 36;
                bool en = g_sweep[a].enabled;
                uint16_t c = en ? ANT_COLORS[a] : sec;
                tft.drawRoundRect(10, y, W - 20, 28, 5, a == sel ? pri : sec);
                tft.setTextColor(c, bg);
                String s = String(en ? "[ON]  " : "[OFF] ") + ANT_NAMES[a];
                if (a == ANT_CC1101) s += " " + String(g_sweep[a].freq, 2) + "MHz";
                tft.drawString(s, 20, y + 8);
            }
            int y = 30 + AP_MAX_ANT * 36 + 4;
            tft.setTextColor(sec, bg);
            tft.drawString("[SEL] toggle  [NEXT] run  [ESC] back", 6, y);
            redraw = false;
        }

        if (check(UpPress))   { sel = (sel - 1 + AP_MAX_ANT) % AP_MAX_ANT; redraw = true; }
        if (check(DownPress)) { sel = (sel + 1) % AP_MAX_ANT; redraw = true; }
        if (check(SelPress))  { g_sweep[sel].enabled = !g_sweep[sel].enabled; redraw = true; }
        if (check(NextPress)) break;
        if (check(EscPress))  break;
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

// CC1101 frequency picker (reuses existing menu style)
static void apPickFreq() {
    options = {
        {"433.92 MHz", [&]() { g_sweep[ANT_CC1101].freq = 433.92f; }},
        {"315 MHz",    [&]() { g_sweep[ANT_CC1101].freq = 315.00f; }},
        {"868 MHz",    [&]() { g_sweep[ANT_CC1101].freq = 868.35f; }},
        {"915 MHz",    [&]() { g_sweep[ANT_CC1101].freq = 915.00f; }},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "CC1101 frequency");
}

// ── Single antenna sweep (blocking, returns when done or ESC) ─────────────────
static void apRunSweep(int a, bool mag) {
    int W = tft.width(), H = tft.height();
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;

    char label[24];
    if (a == ANT_CC1101)
        snprintf(label, sizeof(label), "CC1101 %.2fMHz", g_sweep[a].freq);
    else if (a == ANT_WIFI)
        strncpy(label, "WiFi 2.4GHz", sizeof(label));
    else
        strncpy(label, "NRF24 2.4GHz", sizeof(label));

    apResetSweep(a);
    apDrawBg(label, mag);
    apDrawGrid();

    tft.setTextColor(sec, bg);
    if (mag) tft.drawString("[SEL] zero  [NEXT] reset  [ESC] done", 4, H - 13);
    else      tft.drawString("[SEL] capture  [NEXT] reset  [ESC] done", 4, H - 13);

    float heading0 = 0; bool haveZero = false;
    int stepIdx = 0;
    uint32_t lastDraw = 0;

    apDrawStats(a);

    while (true) {
        if (check(EscPress)) break;
        if (check(NextPress)) { apResetSweep(a); stepIdx = 0; apRedraw(a); }

        if (mag) {
            float gx, gy, gz;
            if (mmcReadGauss(gx, gy, gz)) {
                float hd = atan2f(gy, gx) * AP_RAD2DEG;
                if (hd < 0) hd += 360.0f;
                if (check(SelPress)) { heading0 = hd; haveZero = true; }
                if (!haveZero) { heading0 = hd; haveZero = true; }
                float rel = hd - heading0;
                while (rel < 0)     rel += 360.0f;
                while (rel >= 360)  rel -= 360.0f;
                int bin = ((int)(rel / (360.0f / AP_BINS) + 0.5f)) % AP_BINS;

                int rssi = -100;
                if (a == ANT_CC1101) rssi = apReadCC1101(4);
#ifdef HAVE_WIFI_RSSI
                else if (a == ANT_WIFI) rssi = apReadWifi(1);
#endif
#ifdef HAVE_NRF24
                else if (a == ANT_NRF24) rssi = apReadNRF(4);
#endif
                if (!g_sweep[a].set[bin] || rssi > g_sweep[a].bin[bin]) {
                    g_sweep[a].bin[bin] = rssi;
                    g_sweep[a].set[bin] = true;
                }
            }
            if (millis() - lastDraw > 120) { apRedraw(a); lastDraw = millis(); }
            vTaskDelay(10 / portTICK_PERIOD_MS);
        } else {
            // stepped: 12 × 30-degree manual captures
            int targetDeg = stepIdx * 30;
            tft.fillRect(0, H - 26, W, 12, bg);
            tft.setTextColor(pri, bg);
            if (stepIdx < 12) {
                char s[40];
                snprintf(s, sizeof(s), "Point to %d deg, SEL (%d/12)", targetDeg, stepIdx + 1);
                tft.drawString(s, 4, H - 26);
            } else {
                tft.setTextColor(sec, bg);
                tft.drawString("Sweep done. ESC to continue.", 4, H - 26);
            }
            if (check(SelPress) && stepIdx < 12) {
                int rssi = -100;
                if (a == ANT_CC1101) rssi = apReadCC1101(12);
#ifdef HAVE_WIFI_RSSI
                else if (a == ANT_WIFI) rssi = apReadWifi(2);
#endif
#ifdef HAVE_NRF24
                else if (a == ANT_NRF24) rssi = apReadNRF(8);
#endif
                int bin = (stepIdx * 3) % AP_BINS;
                g_sweep[a].bin[bin] = rssi;
                g_sweep[a].set[bin] = true;
                stepIdx++;
                apRedraw(a);
            }
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
    }
    g_sweep[a].done = true;
}

// ── Overlay summary screen ────────────────────────────────────────────────────
static void apOverlay() {
    int W = tft.width(), H = tft.height();
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;

    apDrawBg("OVERLAY", false);
    apDrawGrid();

    // Legend top-right
    int lx = RCX + RR + 10; if (lx < 212) lx = 212;
    int ly = AP_HDR + 4;
    for (int a = 0; a < AP_MAX_ANT; a++) {
        if (!g_sweep[a].enabled || !g_sweep[a].done) continue;
        tft.setTextColor(ANT_COLORS[a], bg);
        tft.drawString(ANT_NAMES[a], lx, ly); ly += 12;
    }

    apRedraw(-1); // draw all sweeps

    tft.setTextColor(sec, bg);
    tft.drawString("[ESC] exit", 4, H - 13);

    while (!check(EscPress)) vTaskDelay(30 / portTICK_PERIOD_MS);
}

// ── Main entry point ──────────────────────────────────────────────────────────
void deuceAntennaPlot() {
    if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) {
        displayError("Antenna plot needs CC1101", true);
        return;
    }

    // Initialise sweep state
    for (int a = 0; a < AP_MAX_ANT; a++) {
        apResetSweep(a);
        g_sweep[a].enabled = true;
    }
    g_sweep[ANT_CC1101].freq = 433.92f;
    g_sweep[ANT_WIFI].freq   = 2400.0f;
    g_sweep[ANT_NRF24].freq  = 2476.0f;

    // ── Antenna + frequency selection ────────────────────────────────────────
    // Let the user pick CC1101 frequency first
    apPickFreq();
    // Then the antenna toggle screen
    apSelectAntennas();

    // Require at least one enabled
    int enabledCount = 0;
    for (int a = 0; a < AP_MAX_ANT; a++) if (g_sweep[a].enabled) enabledCount++;
    if (enabledCount == 0) return;

    bool mag = mmcBegin();

    // ── Run each enabled antenna ─────────────────────────────────────────────
    float savedFreq = bruceConfigPins.rfFreq;

    for (int a = 0; a < AP_MAX_ANT; a++) {
        if (!g_sweep[a].enabled) continue;

        if (a == ANT_CC1101) {
            bruceConfigPins.rfFreq = g_sweep[a].freq;
            if (!initRfModule("rx", g_sweep[a].freq)) {
                displayError("CC1101 init failed", true);
                continue;
            }
            setMHZ(g_sweep[a].freq);
            apRunSweep(a, mag);
            deinitRfModule();
        }
#ifdef HAVE_WIFI_RSSI
        else if (a == ANT_WIFI) {
            WiFi.mode(WIFI_STA);
            apRunSweep(a, mag);
            WiFi.mode(WIFI_OFF);
        }
#endif
#ifdef HAVE_NRF24
        else if (a == ANT_NRF24) {
            apRunSweep(a, mag);
        }
#endif
        else {
            // antenna type present in enum but not compiled in -- skip silently
            g_sweep[a].done = false;
        }
    }

    bruceConfigPins.rfFreq = savedFreq;

    // ── Overlay if more than one sweep completed ──────────────────────────────
    int doneCount = 0;
    for (int a = 0; a < AP_MAX_ANT; a++) if (g_sweep[a].done) doneCount++;
    if (doneCount > 1) apOverlay();
}
