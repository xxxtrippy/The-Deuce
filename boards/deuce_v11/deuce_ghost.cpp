// THE DEUCE v1.1 -- "Ghost Radar": RF field-strength / activity detector (see .h for scope).
//
// How it works: the CC1101 is tuned across a uniform set of frequencies spanning
// the three sub-GHz bands. At each step we read RSSI (dBm) and compare it to a
// slow per-frequency noise-floor EMA; the excess ("activity") drives a 0-5 EMF
// meter and, when it crosses the sensitivity threshold, drops a fading blip on the
// radar. The radar sweep angle tracks the frequency index, so the rotating line IS
// the live spectrum scan -- a spike shows up as a blip at that bearing.

#include "deuce_ghost.h"

#include <Arduino.h>
#include <globals.h>
#include <math.h>

#include "core/display.h"
#include "deuce_seg14.h"
#include "modules/rf/rf_utils.h" // initRfModule/deinitRfModule/setMHZ + ELECHOUSE_cc1101

// --- sweep set --------------------------------------------------------------
#define GHOST_TWO_PI 6.28318530718f
#define GHOST_MAXF 64
static float g_freqs[GHOST_MAXF];
static int g_nf = 0;

static void ghostBuildFreqs() {
    int i = 0;
    for (float f = 300.0f; f <= 348.0f && i < GHOST_MAXF; f += 4.0f) g_freqs[i++] = f;
    for (float f = 387.0f; f <= 464.0f && i < GHOST_MAXF; f += 4.0f) g_freqs[i++] = f;
    for (float f = 779.0f; f <= 928.0f && i < GHOST_MAXF; f += 6.2f) g_freqs[i++] = f;
    g_nf = i;
}

// --- detection state --------------------------------------------------------
static float g_floor[GHOST_MAXF]; // per-freq noise-floor EMA (dBm)
static bool g_floorInit[GHOST_MAXF];
static int g_sensIdx = 1;                      // sensitivity preset index
static const float GHOST_SENS[3] = {4, 7, 11}; // spike threshold (dB over floor)
static const char *GHOST_SENS_NAME[3] = {"HIGH", "MED", "LOW"};

// --- blips ------------------------------------------------------------------
#define GHOST_BLIPS 28
struct GhostBlip {
    bool used;
    float ang;   // radians
    int rad;     // pixels from center
    uint32_t born;
};
static GhostBlip g_blips[GHOST_BLIPS];
static int g_blipHead = 0;
#define GHOST_BLIP_MS 2600

// --- geometry ---------------------------------------------------------------
#define GHOST_HDR 24
static int RCX, RCY, RR; // radar center + radius
static int g_level = 0;
static float g_peakAct = 0;
static float g_peakFreq = 0;
static int g_peakRssi = -120;

static uint16_t ghostLerp565(uint16_t a, uint16_t b, uint8_t t) {
    int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
    int r = ar + ((br - ar) * t) / 255;
    int g = ag + ((bg - ag) * t) / 255;
    int bl = ab + ((bb - ab) * t) / 255;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

static void ghostAddBlip(float ang, int rad) {
    g_blips[g_blipHead].used = true;
    g_blips[g_blipHead].ang = ang;
    g_blips[g_blipHead].rad = rad;
    g_blips[g_blipHead].born = millis();
    g_blipHead = (g_blipHead + 1) % GHOST_BLIPS;
}

static int ghostLevelFromAct(float act) {
    if (act < 3) return 0;
    if (act < 6) return 1;
    if (act < 10) return 2;
    if (act < 16) return 3;
    if (act < 24) return 4;
    return 5;
}

// --- static chrome ----------------------------------------------------------
static void ghostDrawFrame() {
    int W = tft.width(), H = tft.height();
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    tft.fillScreen(bg);

    tft.setTextSize(1);
    tft.setTextColor(pri, bg);
    tft.drawString("GHOST RADAR", 6, 6);
    String s = String("SENS:") + GHOST_SENS_NAME[g_sensIdx];
    tft.setTextColor(sec, bg);
    tft.drawString(s, W - 6 - tft.textWidth(s), 6);
    tft.drawFastHLine(0, GHOST_HDR - 2, W, sec);

    // radar lives on the left; meter panel on the right
    RR = 96;
    RCX = 8 + RR;                          // ~104
    RCY = GHOST_HDR + 6 + RR;              // ~126
    if (RCY + RR > H - 4) RCY = H - 4 - RR;
}

static void ghostDrawRadarBase() {
    uint16_t bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    tft.fillCircle(RCX, RCY, RR, bg);
    tft.drawCircle(RCX, RCY, RR, sec);
    tft.drawCircle(RCX, RCY, (RR * 2) / 3, sec);
    tft.drawCircle(RCX, RCY, RR / 3, sec);
    tft.drawLine(RCX - RR, RCY, RCX + RR, RCY, sec);
    tft.drawLine(RCX, RCY - RR, RCX, RCY + RR, sec);
}

static void ghostPointOnRadar(float ang, int rad, int &x, int &y) {
    x = RCX + (int)(rad * sinf(ang));
    y = RCY - (int)(rad * cosf(ang));
}

static void ghostDrawBlips() {
    uint16_t bg = bruceConfig.bgColor;
    uint16_t ghost = TFT_GREEN;
    uint32_t now = millis();
    for (int i = 0; i < GHOST_BLIPS; i++) {
        if (!g_blips[i].used) continue;
        uint32_t age = now - g_blips[i].born;
        if (age > GHOST_BLIP_MS) {
            g_blips[i].used = false;
            continue;
        }
        uint8_t t = (uint8_t)(age * 255 / GHOST_BLIP_MS); // 0 fresh -> 255 old
        uint16_t c = ghostLerp565(ghost, bg, t);
        int x, y;
        ghostPointOnRadar(g_blips[i].ang, g_blips[i].rad, x, y);
        int sz = age < 500 ? 4 : 3;
        tft.fillCircle(x, y, sz, c);
    }
}

static void ghostDrawSweep(float ang) {
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor;
    // fading trail behind the lead line
    for (int k = 8; k >= 0; k--) {
        float a = ang - k * 0.10f;
        uint8_t t = (uint8_t)(k * 26);
        uint16_t c = ghostLerp565(pri, bg, t);
        int x, y;
        ghostPointOnRadar(a, RR, x, y);
        tft.drawLine(RCX, RCY, x, y, c);
    }
}

// --- right-hand EMF meter panel --------------------------------------------
static void ghostDrawMeter() {
    int W = tft.width();
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    int px = RCX + RR + 14; // panel left edge
    if (px < 212) px = 212;

    // big level digit (Matrix font)
    tft.fillRect(px, GHOST_HDR + 8, W - px - 4, 42, bg);
    uint16_t lc = (g_level >= 5) ? TFT_RED : (g_level >= 3 ? 0xFD20 : TFT_GREEN);
    deuceSeg14At(String(g_level), px + 2, GHOST_HDR + 10, 20, 38, 5, 4, lc, bg);
    tft.setTextColor(sec, bg);
    tft.drawString("EMF", px + 44, GHOST_HDR + 30);

    // 5-segment bar
    int by = GHOST_HDR + 58, bw = W - px - 10, bh = 12, gap = 4;
    for (int s = 0; s < 5; s++) {
        int sy = by + (4 - s) * (bh + gap);
        uint16_t segc = (s >= 4) ? TFT_RED : (s >= 2 ? 0xFD20 : TFT_GREEN);
        bool lit = g_level >= (s + 1);
        if (lit) tft.fillRect(px, sy, bw, bh, segc);
        else {
            tft.fillRect(px, sy, bw, bh, bg);
            tft.drawRect(px, sy, bw, bh, sec);
        }
    }

    // peak readout
    int ty = by + 5 * (bh + gap) + 6;
    tft.fillRect(px, ty, W - px - 4, 26, bg);
    tft.setTextColor(pri, bg);
    char line1[20];
    snprintf(line1, sizeof(line1), "%d dBm", g_peakRssi);
    tft.drawString(line1, px, ty);
    char line2[20];
    snprintf(line2, sizeof(line2), "%d MHz", (int)(g_peakFreq + 0.5f));
    tft.setTextColor(sec, bg);
    tft.drawString(line2, px, ty + 12);
}

// ---------------------------------------------------------------------------
void deuceGhostRadar() {
    if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) {
        displayError("Ghost Radar needs a CC1101", true);
        return;
    }

    ghostBuildFreqs();
    for (int i = 0; i < GHOST_MAXF; i++) g_floorInit[i] = false;
    for (auto &b : g_blips) b.used = false;
    g_blipHead = 0;
    g_level = 0;
    g_peakAct = 0;
    g_peakFreq = 0;
    g_peakRssi = -120;

    float savedFreq = bruceConfigPins.rfFreq;
    if (!initRfModule("rx", g_freqs[0])) {
        bruceConfigPins.rfFreq = savedFreq;
        deinitRfModule();
        displayError("RF init failed", true);
        return;
    }

    ghostDrawFrame();
    ghostDrawRadarBase();
    ghostDrawMeter();

    int idx = 0;
    uint32_t lastFrame = millis();
    uint32_t lastDecay = millis();

    while (true) {
        if (check(EscPress)) break;
        if (check(SelPress)) {
            g_sensIdx = (g_sensIdx + 1) % 3;
            ghostDrawFrame();
            ghostDrawRadarBase();
            ghostDrawMeter();
        }

        // frame pacing ~33ms -> smooth sweep, ~1.5s per full revolution
        if (millis() - lastFrame < 30) {
            vTaskDelay(1 / portTICK_PERIOD_MS);
            continue;
        }
        lastFrame = millis();

        // sample one frequency
        float f = g_freqs[idx];
        setMHZ(f);
        vTaskDelay(2 / portTICK_PERIOD_MS);
        tft.drawPixel(0, 0, bruceConfig.bgColor); // nudge shared SPI (mirrors rf_scan)
        int rssi = ELECHOUSE_cc1101.getRssi();

        // per-freq noise-floor EMA; activity = excess over floor
        if (!g_floorInit[idx]) {
            g_floor[idx] = rssi;
            g_floorInit[idx] = true;
        }
        float activity = rssi - g_floor[idx];
        if (activity < 0) activity = 0;
        // let the floor track slowly upward/downward, but never chase a live spike up fast
        float aFloor = (rssi < g_floor[idx]) ? 0.20f : 0.02f;
        g_floor[idx] += (rssi - g_floor[idx]) * aFloor;

        float ang = (float)idx / (float)g_nf * GHOST_TWO_PI;

        if (activity >= GHOST_SENS[g_sensIdx]) {
            int rad = (int)((activity / 30.0f) * (RR - 6));
            if (rad < RR / 4) rad = RR / 4;
            if (rad > RR - 4) rad = RR - 4;
            ghostAddBlip(ang, rad);
            if (activity > g_peakAct) {
                g_peakAct = activity;
                g_peakFreq = f;
                g_peakRssi = rssi;
            }
        }

        // level = smoothed recent peak activity
        int lvlNow = ghostLevelFromAct(activity);
        if (lvlNow > g_level) g_level = lvlNow;

        // redraw radar disc + blips + sweep
        ghostDrawRadarBase();
        ghostDrawBlips();
        ghostDrawSweep(ang);

        // decay the meter/peak every ~700ms so it falls back when the room is quiet
        if (millis() - lastDecay > 700) {
            lastDecay = millis();
            if (g_level > 0) g_level--;
            g_peakAct *= 0.6f;
            if (g_peakAct < 2) {
                g_peakRssi = -120;
                g_peakFreq = 0;
            }
            ghostDrawMeter();
        }

        idx = (idx + 1) % g_nf;
    }

    bruceConfigPins.rfFreq = savedFreq;
    deinitRfModule();
}
