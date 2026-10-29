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
#include "deuce_mmc5983.h"        // optional magnetometer (true magnetic EMF channel)
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
// Baselines are captured by a calibration pass (ghostCalibrate) so the device's
// OWN emissions -- display/SPI switching noise on the RF side, and the static PCB
// magnetic field on the mag side -- are measured first and subtracted, rather
// than being shown as phantom activity.
static float g_floor[GHOST_MAXF];   // per-freq self/ambient RSSI baseline (dBm)
static float g_rfNoise[GHOST_MAXF]; // per-freq self-noise envelope (dB) -> min spike threshold
static int g_sensIdx = 1;                      // sensitivity preset index
static const float GHOST_SENS[3] = {4, 7, 11}; // spike threshold (dB over floor)
static const char *GHOST_SENS_NAME[3] = {"HIGH", "MED", "LOW"};

// --- blips ------------------------------------------------------------------
#define GHOST_BLIPS 28
struct GhostBlip {
    bool used;
    bool mag;    // true = magnetic (red), false = RF (green)
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

// magnetometer (optional, auto-detected)
static bool g_magPresent = false;
static float g_magOx = 0, g_magOy = 0, g_magOz = 0; // calibrated baseline offsets (Gauss)
static float g_magNoise = 0;                        // calibrated self-noise floor (mG)
static float g_magPeak = 0;                         // live peak deviation (mG)

static uint16_t ghostLerp565(uint16_t a, uint16_t b, uint8_t t) {
    int ar = (a >> 11) & 31, ag = (a >> 5) & 63, ab = a & 31;
    int br = (b >> 11) & 31, bg = (b >> 5) & 63, bb = b & 31;
    int r = ar + ((br - ar) * t) / 255;
    int g = ag + ((bg - ag) * t) / 255;
    int bl = ab + ((bb - ab) * t) / 255;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

static void ghostAddBlip(float ang, int rad, bool mag) {
    g_blips[g_blipHead].used = true;
    g_blips[g_blipHead].mag = mag;
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
    const char *src = g_magPresent ? "MAG+RF" : "RF";
    String s = String(src) + "  SENS:" + GHOST_SENS_NAME[g_sensIdx];
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
    uint32_t now = millis();
    for (int i = 0; i < GHOST_BLIPS; i++) {
        if (!g_blips[i].used) continue;
        uint32_t age = now - g_blips[i].born;
        if (age > GHOST_BLIP_MS) {
            g_blips[i].used = false;
            continue;
        }
        uint8_t t = (uint8_t)(age * 255 / GHOST_BLIP_MS); // 0 fresh -> 255 old
        uint16_t c = ghostLerp565(g_blips[i].mag ? TFT_RED : TFT_GREEN, bg, t);
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

    // peak readout: magnetic (mG) when the sensor is fitted, plus RF peak
    int ty = by + 5 * (bh + gap) + 6;
    tft.fillRect(px, ty, W - px - 4, 38, bg);
    char line1[24], line2[24];
    if (g_magPresent) {
        tft.setTextColor(TFT_RED, bg);
        snprintf(line1, sizeof(line1), "%.1f mG", g_magPeak);
        tft.drawString(line1, px, ty);
        tft.setTextColor(TFT_GREEN, bg);
        snprintf(line2, sizeof(line2), "%d dBm", g_peakRssi);
        tft.drawString(line2, px, ty + 13);
    } else {
        tft.setTextColor(pri, bg);
        snprintf(line1, sizeof(line1), "%d dBm", g_peakRssi);
        tft.drawString(line1, px, ty);
        tft.setTextColor(sec, bg);
        snprintf(line2, sizeof(line2), "%d MHz", (int)(g_peakFreq + 0.5f));
        tft.drawString(line2, px, ty + 13);
    }
}

// ---------------------------------------------------------------------------
// Calibration: characterise the device's OWN baseline before scanning. Runs with
// the radio in RX and the display active -- i.e. the same operating state as the
// live scan -- so the self-noise it captures is exactly what will be present then.
//   RF  : per-freq mean RSSI -> g_floor[], and a self-noise envelope (3*sigma)
//         -> g_rfNoise[], so jittery self-emitting frequencies need a bigger spike
//         to register. Live activity is measured as excess over g_floor[].
//   MAG : per-axis mean field -> g_magOx/y/z (the device's static PCB field becomes
//         the zero point), and magnitude sigma -> g_magNoise (mG) subtracted live.
// ---------------------------------------------------------------------------
static void ghostCalibrate() {
    int W = tft.width(), H = tft.height();
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    tft.fillScreen(bg);
    tft.setTextSize(1);
    tft.setTextColor(pri, bg);
    tft.drawString("CALIBRATING", W / 2 - tft.textWidth("CALIBRATING") / 2, H / 2 - 36);
    tft.setTextColor(sec, bg);
    const char *sub = "learning the Deuce's own signals";
    tft.drawString(sub, W / 2 - tft.textWidth(sub) / 2, H / 2 - 22);
    const char *sub2 = "keep clear of sources";
    tft.drawString(sub2, W / 2 - tft.textWidth(sub2) / 2, H / 2 - 10);

    int bx = W / 2 - 90, bw = 180, by = H / 2 + 8, bh = 14;
    tft.drawRect(bx, by, bw, bh, sec);

    const int PASSES = 8;
    // RF accumulators
    static float sum[GHOST_MAXF], sumsq[GHOST_MAXF];
    for (int i = 0; i < g_nf; i++) { sum[i] = 0; sumsq[i] = 0; }
    // mag accumulators
    double msx = 0, msy = 0, msz = 0, msm = 0, msm2 = 0;
    int mcount = 0;

    int steps = PASSES * g_nf, done = 0;
    for (int p = 0; p < PASSES; p++) {
        for (int i = 0; i < g_nf; i++) {
            setMHZ(g_freqs[i]);
            vTaskDelay(2 / portTICK_PERIOD_MS);
            tft.drawPixel(0, 0, bg);
            int rssi = ELECHOUSE_cc1101.getRssi();
            sum[i] += rssi;
            sumsq[i] += (float)rssi * rssi;

            if (g_magPresent) {
                float gx, gy, gz;
                if (mmcReadGauss(gx, gy, gz)) {
                    msx += gx; msy += gy; msz += gz;
                    double m = sqrt((double)gx * gx + (double)gy * gy + (double)gz * gz) * 1000.0;
                    msm += m; msm2 += m * m;
                    mcount++;
                }
            }

            done++;
            int fillw = (bw - 2) * done / steps;
            tft.fillRect(bx + 1, by + 1, fillw, bh - 2, pri);
        }
        if (check(EscPress)) break; // allow bail-out
    }

    // finalise RF baselines
    for (int i = 0; i < g_nf; i++) {
        float mean = sum[i] / PASSES;
        float var = sumsq[i] / PASSES - mean * mean;
        if (var < 0) var = 0;
        float sigma = sqrtf(var);
        g_floor[i] = mean;
        float env = 3.0f * sigma; // self-noise envelope
        if (env < 3.0f) env = 3.0f;
        if (env > 20.0f) env = 20.0f;
        g_rfNoise[i] = env;
    }

    // finalise mag baseline
    if (g_magPresent && mcount > 0) {
        g_magOx = (float)(msx / mcount);
        g_magOy = (float)(msy / mcount);
        g_magOz = (float)(msz / mcount);
        double mean = msm / mcount;
        double var = msm2 / mcount - mean * mean;
        if (var < 0) var = 0;
        float sigma = (float)sqrt(var);
        float env = 3.0f * sigma;
        if (env < 1.0f) env = 1.0f;
        if (env > 15.0f) env = 15.0f;
        g_magNoise = env;
    }
}

// ---------------------------------------------------------------------------
void deuceGhostRadar() {
    if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) {
        displayError("Ghost Radar needs a CC1101", true);
        return;
    }

    ghostBuildFreqs();
    for (auto &b : g_blips) b.used = false;
    g_blipHead = 0;
    g_level = 0;
    g_peakAct = 0;
    g_peakFreq = 0;
    g_peakRssi = -120;

    // optional true-magnetic EMF channel (auto-detected on the shared I2C bus)
    g_magPresent = mmcBegin();
    g_magPeak = 0;

    float savedFreq = bruceConfigPins.rfFreq;
    if (!initRfModule("rx", g_freqs[0])) {
        bruceConfigPins.rfFreq = savedFreq;
        deinitRfModule();
        displayError("RF init failed", true);
        return;
    }

    // characterise the device's own baseline (RF self-noise + static mag field)
    // with the radio/display in their live state, then subtract it while scanning
    ghostCalibrate();

    ghostDrawFrame();
    ghostDrawRadarBase();
    ghostDrawMeter();

    int idx = 0;
    uint32_t lastFrame = millis();
    uint32_t lastDecay = millis();
    uint32_t lastSR = millis();

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

        // activity = excess over the CALIBRATED baseline (device self-noise + ambient)
        float activity = rssi - g_floor[idx];
        if (activity < 0) activity = 0;
        // let the floor track very slowly (thermal/ambient drift), never chase a spike up
        float aFloor = (rssi < g_floor[idx]) ? 0.10f : 0.01f;
        g_floor[idx] += (rssi - g_floor[idx]) * aFloor;

        // spike must clear BOTH the user sensitivity and this freq's self-noise envelope
        float rfThresh = GHOST_SENS[g_sensIdx];
        if (g_rfNoise[idx] > rfThresh) rfThresh = g_rfNoise[idx];

        float ang = (float)idx / (float)g_nf * GHOST_TWO_PI;

        if (activity >= rfThresh) {
            int rad = (int)((activity / 30.0f) * (RR - 6));
            if (rad < RR / 4) rad = RR / 4;
            if (rad > RR - 4) rad = RR - 4;
            ghostAddBlip(ang, rad, false); // RF = green
            if (activity > g_peakAct) {
                g_peakAct = activity;
                g_peakFreq = f;
                g_peakRssi = rssi;
            }
        }

        // level = smoothed recent peak activity
        int lvlNow = ghostLevelFromAct(activity);
        if (lvlNow > g_level) g_level = lvlNow;

        // --- magnetic (true EMF) channel -----------------------------------
        if (g_magPresent) {
            float gx, gy, gz;
            if (mmcReadGauss(gx, gy, gz)) {
                // deviation from the CALIBRATED device-self field (per-axis offsets)
                float rx = gx - g_magOx, ry = gy - g_magOy, rz = gz - g_magOz;
                float dev = sqrtf(rx * rx + ry * ry + rz * rz) * 1000.0f; // milligauss
                // slow offset drift correction while quiet (keeps self-field zeroed)
                if (dev < g_magNoise + 1.0f) {
                    g_magOx += (gx - g_magOx) * 0.01f;
                    g_magOy += (gy - g_magOy) * 0.01f;
                    g_magOz += (gz - g_magOz) * 0.01f;
                }

                // subtract the calibrated self-noise envelope, then threshold
                float mdev = dev - g_magNoise;
                if (mdev < 0) mdev = 0;
                float scale = GHOST_SENS[g_sensIdx] / 7.0f;
                int magLvl = 0;
                if (mdev > 1.5f * scale) magLvl = 1;
                if (mdev > 3.0f * scale) magLvl = 2;
                if (mdev > 6.0f * scale) magLvl = 3;
                if (mdev > 12.0f * scale) magLvl = 4;
                if (mdev > 20.0f * scale) magLvl = 5;
                if (magLvl > g_level) g_level = magLvl;

                if (magLvl >= 1) {
                    // bearing from the residual field direction -> points at the source
                    float bang = atan2f(ry, rx);
                    int mrad = (int)((mdev / 24.0f) * (RR - 6));
                    if (mrad < RR / 4) mrad = RR / 4;
                    if (mrad > RR - 4) mrad = RR - 4;
                    ghostAddBlip(bang, mrad, true); // magnetic = red
                    if (mdev > g_magPeak) g_magPeak = mdev;
                }
            }
        }

        // redraw radar disc + blips + sweep
        ghostDrawRadarBase();
        ghostDrawBlips();
        ghostDrawSweep(ang);

        // decay the meter/peak every ~700ms so it falls back when the room is quiet
        if (millis() - lastDecay > 700) {
            lastDecay = millis();
            if (g_level > 0) g_level--;
            g_peakAct *= 0.6f;
            g_magPeak *= 0.6f;
            if (g_peakAct < 2) {
                g_peakRssi = -120;
                g_peakFreq = 0;
            }
            ghostDrawMeter();
        }

        // periodic degauss keeps the AMR bridge offset honest
        if (g_magPresent && millis() - lastSR > 5000) {
            lastSR = millis();
            mmcSetReset();
        }

        idx = (idx + 1) % g_nf;
    }

    bruceConfigPins.rfFreq = savedFreq;
    deinitRfModule();
}
