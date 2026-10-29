// THE DEUCE v1.1 -- TPMS tire-pressure sensor reader (passive, receive-only).
//
// Radio front-end + UI around the host-verified decoders in deuce_tpms_core.h.
//
// Pipeline: the CC1101 is put in asynchronous serial mode (demodulated bitstream
// on GDO0); the native RMT RX session (Bruce's RfRxSession) captures the edge
// timing of that stream as signed microsecond durations; we PCM-slice those back
// into an on-air bit vector and run every protocol decoder over it.
//
// The sensor families split by modulation, and the CC1101 can only be in one at a
// time, so the scanner alternates two radio profiles every ~1.6 s:
//   FSK profile  -> Toyota, Ford, Nissan   (FSK, ~19.2 kbps)
//   OOK profile  -> Schrader, Schrader-GM(EG53MA4), GM-aftermarket  (ASK/OOK Manchester)
//
// Register values (datarate / deviation / RX bandwidth) and the PCM bit-widths are
// best-effort defaults taken from rtl_433's device specs; they are the layer meant
// to be fine-tuned on real hardware. The decoders themselves are exact.

#include "deuce_tpms.h"
#include "deuce_tpms_core.h"

#include <Arduino.h>
#include <SD.h>
#include <globals.h>

#include "core/display.h"
#include "core/sd_functions.h"
#include "deuce_seg14.h"                     // deuceSeg14At / deuceSeg14Width (Matrix LCD font)
#include "modules/rf/protocols/rf_decoder.h" // RfRxSession
#include "modules/rf/rf_utils.h"             // initRfModule/deinitRfModule/setMHZ + ELECHOUSE_cc1101

// --- tuning -----------------------------------------------------------------
#define TPMS_PROFILE_MS 1600  // dwell per modulation profile before switching
#define TPMS_MAX_SENSORS 10   // on-screen sensor rows kept
#define TPMS_LOG_PATH "/tpms_log.txt"

// Candidate PCM bit-widths (us) per family. The decoders are preamble-anchored
// and CRC/checksum-checked, so trying a few widths is cheap and self-correcting.
static const int TPMS_FSK_WIDTHS[] = {52, 50, 54};
static const int TPMS_OOK_WIDTHS[] = {120, 123, 115};

// ---------------------------------------------------------------------------
// PCM slicer: signed durations (HIGH>0, LOW<0, us) -> on-air bits at width T.
// ---------------------------------------------------------------------------
static void tpmsPcmSlice(const std::vector<int> &dur, int T, TB *out) {
    tb_init(out);
    if (T <= 0) return;
    for (int d : dur) {
        int ad = d < 0 ? -d : d;
        uint8_t lvl = d > 0 ? 1 : 0;
        int n = (ad + T / 2) / T;
        for (int i = 0; i < n; i++) {
            if (out->nbits >= TB_MAX_BYTES * 8) return;
            tb_add_bit(out, lvl);
        }
    }
}

// Try to decode one capture against the active family, over several bit-widths
// and both polarities (the FSK slicer polarity and the OOK Manchester phase are
// hardware-dependent).
static bool tpmsTryDecode(const std::vector<int> &dur, bool fsk, TpmsReading *out) {
    const int *widths = fsk ? TPMS_FSK_WIDTHS : TPMS_OOK_WIDTHS;
    int nwidths = fsk ? (int)(sizeof(TPMS_FSK_WIDTHS) / sizeof(int))
                      : (int)(sizeof(TPMS_OOK_WIDTHS) / sizeof(int));
    for (int w = 0; w < nwidths; w++) {
        TB raw;
        tpmsPcmSlice(dur, widths[w], &raw);
        if (raw.nbits < 40) continue;
        if (tpms_decode_any(&raw, fsk, out)) return true;
        tb_invert(&raw);
        if (tpms_decode_any(&raw, fsk, out)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// CC1101 modulation profile. Applied after RfRxSession::begin() (which re-inits
// the module to the OOK scan defaults); the armed RMT just watches GDO0, so
// changing the radio's demod mode underneath it is fine.
// ---------------------------------------------------------------------------
static void tpmsApplyProfile(bool fsk) {
    ELECHOUSE_cc1101.setSidle();
    if (fsk) {
        ELECHOUSE_cc1101.setModulation(0); // 2-FSK
        ELECHOUSE_cc1101.setDRate(19.2);
        ELECHOUSE_cc1101.setDeviation(38.0);
        ELECHOUSE_cc1101.setRxBW(101.0);
    } else {
        ELECHOUSE_cc1101.setModulation(2); // ASK/OOK
        ELECHOUSE_cc1101.setDRate(8.4);
        ELECHOUSE_cc1101.setRxBW(203.0);
    }
    // Asynchronous serial mode: GDO0 carries the raw demodulated bitstream and the
    // packet/sync-word engine is bypassed, so no sync-word config is needed.
    ELECHOUSE_cc1101.setPktFormat(3);
    ELECHOUSE_cc1101.SetRx();
}

// ---------------------------------------------------------------------------
// Sensor table (dedupe by model+id, newest reading wins).
// ---------------------------------------------------------------------------
struct TpmsSlot {
    bool used;
    TpmsReading r;
    uint32_t last_ms;
    uint16_t hits;
};
static TpmsSlot g_slots[TPMS_MAX_SENSORS];

static void tpmsResetSlots() {
    for (auto &s : g_slots) {
        s.used = false;
        s.hits = 0;
    }
}

// Returns true if this was a newly-seen sensor (for one-shot SD logging).
static bool tpmsStore(const TpmsReading &r) {
    for (auto &s : g_slots) {
        if (s.used && s.r.model == r.model && s.r.id_hi == r.id_hi && s.r.id_lo == r.id_lo) {
            s.r = r;
            s.last_ms = millis();
            s.hits++;
            return false;
        }
    }
    // find a free slot, else evict the stalest
    int pick = -1;
    uint32_t oldest = 0xFFFFFFFF;
    for (int i = 0; i < TPMS_MAX_SENSORS; i++) {
        if (!g_slots[i].used) { pick = i; break; }
        if (g_slots[i].last_ms < oldest) {
            oldest = g_slots[i].last_ms;
            pick = i;
        }
    }
    g_slots[pick].used = true;
    g_slots[pick].r = r;
    g_slots[pick].last_ms = millis();
    g_slots[pick].hits = 1;
    return true;
}

static String tpmsIdStr(const TpmsReading &r) {
    char buf[20];
    if (r.id_hi) snprintf(buf, sizeof(buf), "%02X%08X", r.id_hi, r.id_lo);
    else snprintf(buf, sizeof(buf), "%08X", r.id_lo);
    return String(buf);
}

static void tpmsLog(const TpmsReading &r) {
    if (!sdcardMounted) return;
    File f = SD.open(TPMS_LOG_PATH, FILE_APPEND);
    if (!f) return;
    float psi = r.pressure_kpa / 6.894757f;
    f.printf(
        "%lu,%s,%s,%.1fkPa,%.1fpsi,", (unsigned long)millis(), r.model, tpmsIdStr(r).c_str(),
        r.pressure_kpa, psi
    );
    if (r.have_temp) f.printf("%.0fC,", r.temp_c);
    else f.print("-,");
    f.printf("flags=%X\n", r.flags);
    f.close();
}

// ---------------------------------------------------------------------------
// Graphical dashboard
//
// A 2x2 grid of gauge cards (one per sensor, newest 4), each with a colour-coded
// arc gauge, the pressure in the Deuce's 14-segment Matrix font, a battery glyph
// and temperature. The header carries freq/mode/count and an animated scan line;
// the empty state shows a pulsing radar. Only changed cards are repainted, so the
// screen stays flicker-free between updates.
// ---------------------------------------------------------------------------
#define TPMS_HDR 26
#define TPMS_GRID 4 // on-screen gauge cards
static const uint16_t TPMS_AMBER = 0xFD20;

// render state (reset on entry)
static uint32_t g_drawnId[TPMS_GRID];
static uint32_t g_drawnIdHi[TPMS_GRID];
static uint16_t g_drawnHits[TPMS_GRID];
static uint32_t g_flashUntil[TPMS_GRID];
static bool g_gridMode;
static int g_phase;

static void tpmsRenderReset() {
    for (int i = 0; i < TPMS_GRID; i++) {
        g_drawnId[i] = 0xFFFFFFFF;
        g_drawnIdHi[i] = 0xFFFFFFFF;
        g_drawnHits[i] = 0;
        g_flashUntil[i] = 0;
    }
    g_gridMode = false;
    g_phase = 0;
}

static uint16_t tpmsPsiColor(float psi) {
    if (psi < 25.0f || psi > 42.0f) return TFT_RED;
    if (psi < 30.0f || psi > 38.0f) return TPMS_AMBER;
    return TFT_GREEN;
}

static void tpmsCardRect(int i, int &x, int &y, int &w, int &h) {
    int W = tft.width(), H = tft.height();
    int M = 6;
    w = (W - 3 * M) / 2;
    h = (H - TPMS_HDR - 3 * M) / 2;
    int col = i % 2, row = i / 2;
    x = M + col * (w + M);
    y = TPMS_HDR + M + row * (h + M);
}

static void tpmsBattGlyph(int x, int y, bool known, bool ok) {
    uint16_t c = !known ? bruceConfig.secColor : (ok ? TFT_GREEN : TFT_RED);
    tft.drawRect(x, y, 13, 8, c);
    tft.fillRect(x + 13, y + 2, 2, 4, c);
    if (known) tft.fillRect(x + 2, y + 2, ok ? 9 : 3, 4, c);
}

// Animated 1px scan line just under the header.
static void tpmsScanLine(int phase) {
    int W = tft.width();
    uint16_t sec = bruceConfig.secColor, pri = bruceConfig.priColor;
    tft.drawFastHLine(0, TPMS_HDR - 2, W, sec);
    int seg = 46;
    int pos = (phase * 10) % (W + seg) - seg;
    int a = pos < 0 ? 0 : pos;
    int b = pos + seg > W ? W : pos + seg;
    if (b > a) tft.drawFastHLine(a, TPMS_HDR - 2, b - a, pri);
}

static void tpmsHeaderStatic(float freq, bool fsk, int total) {
    int W = tft.width();
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor;
    tft.fillRect(0, 0, W, TPMS_HDR, bg);
    tft.setTextSize(1);
    tft.setTextColor(pri, bg);
    tft.drawString("TPMS", 6, 6);
    String md = String(freq, 2) + " " + (fsk ? "FSK" : "OOK");
    tft.drawString(md, 52, 6);
    String cnt = String(total) + (total == 1 ? " tag" : " tags");
    tft.drawString(cnt, W - 6 - tft.textWidth(cnt), 6);
}

// Pulsing radar for the "no sensors yet" state. Redraws only the central box.
static void tpmsRadar(int phase) {
    int W = tft.width(), H = tft.height();
    int cx = W / 2, cy = TPMS_HDR + (H - TPMS_HDR) / 2;
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    int box = 150;
    tft.fillRect(cx - box / 2, cy - box / 2, box, box, bg);
    for (int k = 0; k < 3; k++) {
        int r = ((phase * 4) + k * 24) % 72 + 4;
        tft.drawCircle(cx, cy, r, r < 44 ? pri : sec);
    }
    tft.drawCircle(cx, cy, 2, pri);
    tft.setTextSize(1);
    tft.setTextColor(pri, bg);
    String s = "LISTENING";
    tft.drawString(s, cx - tft.textWidth(s) / 2, cy + 56);
    String s2 = "drive / rotate / tap a valve";
    tft.setTextColor(sec, bg);
    tft.drawString(s2, cx - tft.textWidth(s2) / 2, cy + 68);
}

static void tpmsDrawCard(int i, const TpmsReading &r, bool flash) {
    int x, y, w, h;
    tpmsCardRect(i, x, y, w, h);
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;

    tft.fillRoundRect(x, y, w, h, 6, bg);
    tft.drawRoundRect(x, y, w, h, 6, flash ? pri : sec);

    tft.setTextSize(1);
    tft.setTextColor(pri, bg);
    String t = String(r.model);
    if (t.length() > 11) t = t.substring(0, 11);
    tft.drawString(t, x + 8, y + 6);
    tpmsBattGlyph(x + w - 20, y + 6, r.have_batt, r.batt_ok);

    float psi = r.pressure_kpa / 6.894757f;
    uint16_t sc = tpmsPsiColor(psi);

    // arc gauge (0..50 psi over a 300deg sweep)
    int gcx = x + 30, gcy = y + h / 2 + 4, gr = 22, gir = 15;
    tft.drawArc(gcx, gcy, gr, gir, 30, 330, sec, bg, true);
    float cp = psi < 0 ? 0 : (psi > 50 ? 50 : psi);
    int sweep = 30 + (int)(cp / 50.0f * 300.0f);
    if (sweep > 30) tft.drawArc(gcx, gcy, gr, gir, 30, sweep, sc, bg, true);

    // big Matrix-font PSI value
    int pint = (int)(psi + 0.5f);
    String ps = String(pint);
    int cw = 15, chh = 30, gap = 4, th = 3;
    int pw = deuceSeg14Width(ps, cw, gap);
    int zoneL = x + 58, zoneR = x + w - 8;
    int px = zoneL + ((zoneR - zoneL) - pw) / 2;
    if (px < zoneL) px = zoneL;
    deuceSeg14At(ps, px, y + 12, cw, chh, gap, th, sc, bg);
    tft.setTextColor(sec, bg);
    tft.drawString("PSI", px + pw + 2, y + 12);

    // footer: id + temperature
    tft.setTextColor(pri, bg);
    String idl = "#" + tpmsIdStr(r);
    tft.drawString(idl, x + 8, y + h - 14);
    if (r.have_temp) {
        int tc = (int)(r.temp_c < 0 ? r.temp_c - 0.5f : r.temp_c + 0.5f);
        String ts = String(tc) + "C";
        tft.setTextColor(sc, bg);
        tft.drawString(ts, x + w - 8 - tft.textWidth(ts), y + h - 14);
    }
}

// Full repaint: background + header + (cards or radar handled by caller/anim).
static void tpmsFullPaint(float freq, bool fsk, int total) {
    tft.fillScreen(bruceConfig.bgColor);
    tpmsHeaderStatic(freq, fsk, total);
    tpmsScanLine(g_phase);
    for (int i = 0; i < TPMS_GRID; i++) {
        g_drawnId[i] = 0xFFFFFFFF;
        g_drawnIdHi[i] = 0xFFFFFFFF;
        g_drawnHits[i] = 0;
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
void deuceTpmsReader() {
    if (bruceConfigPins.rfModule != CC1101_SPI_MODULE) {
        displayError("TPMS needs a CC1101", true);
        return;
    }

    // Frequency pick (US sensors are overwhelmingly 315 MHz).
    float freq = 315.00;
    options = {
        {"315 MHz (US)", [&]() { freq = 315.00; }},
        {"433.92 MHz",   [&]() { freq = 433.92; }},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "TPMS Frequency");

    // Remember and set the working frequency (RfRxSession re-inits from this).
    float savedFreq = bruceConfigPins.rfFreq;
    bruceConfigPins.rfFreq = freq;

    tpmsResetSlots();
    tpmsRenderReset();
    int total = 0;
    bool fsk = true;

    RfRxSession rx;
    if (!rx.begin()) {
        bruceConfigPins.rfFreq = savedFreq;
        deinitRfModule();
        displayError("RF init failed", true);
        return;
    }
    setMHZ(freq);
    tpmsApplyProfile(fsk);
    tpmsFullPaint(freq, fsk, total);

    uint32_t lastSwitch = millis();
    uint32_t lastAnim = millis();

    while (true) {
        if (check(EscPress)) break;

        if (check(SelPress)) {
            // toggle frequency (avoid float-equality; split the two bands at 400)
            freq = (freq < 400.0f) ? 433.92f : 315.00f;
            bruceConfigPins.rfFreq = freq;
            rx.end();
            rx.begin();
            setMHZ(freq);
            tpmsApplyProfile(fsk);
            tpmsResetSlots();
            tpmsRenderReset();
            total = 0;
            tpmsFullPaint(freq, fsk, total);
            lastSwitch = millis();
        }

        // alternate modulation profile (header text only; cards stay put)
        if (millis() - lastSwitch > TPMS_PROFILE_MS) {
            fsk = !fsk;
            rx.end();
            rx.begin();
            setMHZ(freq);
            tpmsApplyProfile(fsk);
            lastSwitch = millis();
            tpmsHeaderStatic(freq, fsk, total);
        }

        std::vector<int> durations;
        if (rx.poll(durations)) {
            TpmsReading r;
            if (tpmsTryDecode(durations, fsk, &r) && r.valid) {
                bool isNew = tpmsStore(r);
                if (isNew) {
                    total++;
                    tpmsLog(r);
                    if (!g_gridMode) {
                        g_gridMode = true; // leave the radar, switch to the card grid
                        tpmsFullPaint(freq, fsk, total);
                    } else {
                        tpmsHeaderStatic(freq, fsk, total);
                    }
                }
            }
        }

        // repaint only the cards whose reading changed, flashing the border briefly
        if (g_gridMode) {
            uint32_t now = millis();
            for (int i = 0; i < TPMS_GRID; i++) {
                if (!g_slots[i].used) continue;
                const TpmsReading &r = g_slots[i].r;
                bool changed = (g_slots[i].hits != g_drawnHits[i] || r.id_lo != g_drawnId[i] ||
                                r.id_hi != g_drawnIdHi[i]);
                if (changed) {
                    tpmsDrawCard(i, r, true);
                    g_drawnHits[i] = g_slots[i].hits;
                    g_drawnId[i] = r.id_lo;
                    g_drawnIdHi[i] = r.id_hi;
                    g_flashUntil[i] = now + 250;
                } else if (g_flashUntil[i] && now >= g_flashUntil[i]) {
                    tpmsDrawCard(i, r, false); // drop the highlight
                    g_flashUntil[i] = 0;
                }
            }
        }

        // animation tick (~140ms): scan line always, radar while empty
        if (millis() - lastAnim > 140) {
            g_phase++;
            tpmsScanLine(g_phase);
            if (!g_gridMode) tpmsRadar(g_phase);
            lastAnim = millis();
        }
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }

    rx.end();
    bruceConfigPins.rfFreq = savedFreq;
    deinitRfModule();
}
