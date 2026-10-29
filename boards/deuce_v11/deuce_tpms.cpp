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
// Rendering
// ---------------------------------------------------------------------------
static void tpmsDraw(float freq, bool fsk, int total) {
    String title = "TPMS " + String(freq, 2) + " " + (fsk ? "FSK" : "OOK");
    drawMainBorderWithTitle(title);
    padprintln("");
    if (total == 0) {
        padprintln("Listening for sensors...");
        padprintln("");
        padprintln("Drive/rotate a wheel or");
        padprintln("tap the valve to wake one.");
        padprintln("");
        padprintln("[SEL] freq   [ESC] exit");
        return;
    }
    for (auto &s : g_slots) {
        if (!s.used) continue;
        float psi = s.r.pressure_kpa / 6.894757f;
        String line = String(s.r.model) + " " + tpmsIdStr(s.r);
        padprintln(line);
        String l2 = "  " + String(psi, 1) + "psi " + String(s.r.pressure_kpa, 0) + "kPa";
        if (s.r.have_temp) l2 += " " + String(s.r.temp_c, 0) + "C";
        if (s.r.have_batt) l2 += s.r.batt_ok ? " BattOK" : " BattLOW";
        padprintln(l2);
    }
    padprintln("");
    padprintln("[SEL] freq   [ESC] exit");
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
    tpmsDraw(freq, fsk, total);

    uint32_t lastSwitch = millis();
    uint32_t lastDraw = millis();

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
            total = 0;
            tpmsDraw(freq, fsk, total);
            lastSwitch = millis();
        }

        // alternate modulation profile
        if (millis() - lastSwitch > TPMS_PROFILE_MS) {
            fsk = !fsk;
            rx.end();
            rx.begin();
            setMHZ(freq);
            tpmsApplyProfile(fsk);
            lastSwitch = millis();
            tpmsDraw(freq, fsk, total);
        }

        std::vector<int> durations;
        if (rx.poll(durations)) {
            TpmsReading r;
            if (tpmsTryDecode(durations, fsk, &r) && r.valid) {
                bool isNew = tpmsStore(r);
                if (isNew) {
                    total++;
                    tpmsLog(r);
                }
                tpmsDraw(freq, fsk, total);
                lastDraw = millis();
            }
        }

        // keep the footer/help fresh even when idle
        if (millis() - lastDraw > 2000) {
            tpmsDraw(freq, fsk, total);
            lastDraw = millis();
        }
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }

    rx.end();
    bruceConfigPins.rfFreq = savedFreq;
    deinitRfModule();
}
