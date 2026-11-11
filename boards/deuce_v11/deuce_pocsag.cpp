// THE DEUCE v1.1 -- POCSAG pager decoder (EXPERIMENTAL). See .h.

#include "deuce_pocsag.h"

#include <Arduino.h>
#include <ELECHOUSE_CC1101_SRC_DRV.h>
#include <globals.h>

#include "core/display.h"
#include "modules/rf/rf_utils.h"

// ── CC1101 async-FSK setup ────────────────────────────────────────────────────
// Register addresses (numeric, so this does not depend on driver macro names).
#define REG_IOCFG0   0x02
#define REG_PKTCTRL0 0x08
#define REG_MDMCFG4  0x10
#define REG_MDMCFG3  0x11
#define REG_MDMCFG2  0x12
#define REG_DEVIATN  0x15
#define STR_SIDLE    0x36
#define STR_SRX      0x34
#define STR_SFRX     0x3A

// Put the already-initialised CC1101 into asynchronous 2-FSK RX with raw sliced
// data on GDO0. Deviation ~4.5 kHz and a mid data-rate suit POCSAG 512/1200/2400.
static void cc1101AsyncFsk() {
    ELECHOUSE_cc1101.SpiStrobe(STR_SIDLE);
    ELECHOUSE_cc1101.SpiWriteReg(REG_IOCFG0, 0x0D);    // GDO0 = async serial data out
    ELECHOUSE_cc1101.SpiWriteReg(REG_PKTCTRL0, 0x32);  // async serial mode, infinite len, no CRC
    ELECHOUSE_cc1101.SpiWriteReg(REG_MDMCFG2, 0x00);   // 2-FSK, no manchester, sync mode none
    // ~4.5 kHz deviation
    ELECHOUSE_cc1101.SpiWriteReg(REG_DEVIATN, 0x34);
    // data-rate / RX-BW: a nominal ~4.8 kBaud, ~58 kHz BW (async slicer follows demod)
    ELECHOUSE_cc1101.SpiWriteReg(REG_MDMCFG4, 0xC7);
    ELECHOUSE_cc1101.SpiWriteReg(REG_MDMCFG3, 0x83);
    ELECHOUSE_cc1101.SpiStrobe(STR_SFRX);
    ELECHOUSE_cc1101.SpiStrobe(STR_SRX);
}

// ── bit recovery ──────────────────────────────────────────────────────────────
// Busy-sample GDO0 at OVERSAMPLE x the baud for one window, then convert the
// level runs into bits (run length / samples-per-bit, rounded).
//
// One window captures TARGET_BITS bits worth of signal (a POCSAG sync word plus
// one 544-bit batch, with margin). The capture is a blocking busy-wait, so the
// window is kept to ~1.5 s even at 512 baud, well under the task watchdog, and
// the main loop yields between windows.
#define OVERSAMPLE 6
#define TARGET_BITS 800
#define MAXBITS    TARGET_BITS

// Returns number of bits recovered into `bits` (0/1 per byte).
static int sampleWindow(int gdo, uint32_t baud, uint8_t *bits, int maxBits) {
    uint32_t spb = baud * OVERSAMPLE;                 // samples/sec
    uint32_t stepUs = 1000000UL / spb;
    if (stepUs == 0) stepUs = 1;
    static uint8_t raw[TARGET_BITS * OVERSAMPLE];
    int maxSamples = (int)sizeof(raw);

    uint32_t t = micros();
    for (int i = 0; i < maxSamples; i++) {
        uint32_t due = t + (uint32_t)i * stepUs;
        while ((int32_t)(micros() - due) < 0) { /* spin */ }
        raw[i] = (uint8_t)digitalRead(gdo);
    }

    // run-length -> bits
    int nb = 0, i = 0;
    const float sPerBit = (float)OVERSAMPLE;
    while (i < maxSamples && nb < maxBits) {
        int j = i + 1;
        while (j < maxSamples && raw[j] == raw[i]) j++;
        int runLen = j - i;
        int nbits = (int)(runLen / sPerBit + 0.5f);
        if (nbits < 1) nbits = 1;
        for (int k = 0; k < nbits && nb < maxBits; k++) bits[nb++] = raw[i];
        i = j;
    }
    return nb;
}

// ── POCSAG decode ─────────────────────────────────────────────────────────────
#define POCSAG_SYNC 0x7CD215D8UL
#define POCSAG_IDLE 0x7A89C197UL

static int popcount32(uint32_t v) { int c = 0; while (v) { c += v & 1; v >>= 1; } return c; }

// pack 32 bits (MSB first) from a bit array at offset
static uint32_t pack32(const uint8_t *bits, int off) {
    uint32_t v = 0;
    for (int k = 0; k < 32; k++) v = (v << 1) | (bits[off + k] & 1);
    return v;
}

// decode numeric payload nibble (POCSAG numeric uses reversed BCD)
static char numMap(uint8_t v) {
    static const char *m = "0123456789*U -)("; // standard POCSAG numeric set
    return m[v & 0x0F];
}
static uint8_t rev4(uint8_t v) {
    return ((v & 1) << 3) | ((v & 2) << 1) | ((v & 4) >> 1) | ((v & 8) >> 3);
}

struct Pocsag { uint32_t ric; uint8_t func; String numeric; String alpha; bool valid; };

// Parse one batch (16 codewords) starting right AFTER a sync word at bit offset `o`.
// Appends message content; returns true if at least one address was found.
static bool parseBatch(const uint8_t *bits, int o, int nbits, Pocsag &out) {
    bool gotAddr = false;
    // alphanumeric assembly state (7-bit LSB-first across 20-bit data chunks)
    uint32_t alphaAcc = 0; int alphaBits = 0;
    for (int cw = 0; cw < 16; cw++) {
        int off = o + cw * 32;
        if (off + 32 > nbits) break;
        uint32_t w = pack32(bits, off);
        if (w == POCSAG_IDLE) continue;
        if (w & 0x80000000UL) {
            // message codeword: 20 data bits [30:11]
            uint32_t data = (w >> 11) & 0xFFFFF;
            // numeric: 5 nibbles, MSB-first, reversed-BCD
            for (int n = 0; n < 5; n++) {
                uint8_t nib = (data >> (16 - n * 4)) & 0x0F;
                out.numeric += numMap(rev4(nib));
            }
            // alphanumeric: 20 bits MSB-first into 7-bit chars LSB-first
            for (int b = 19; b >= 0; b--) {
                uint32_t bit = (data >> b) & 1;
                alphaAcc |= (bit << alphaBits);
                alphaBits++;
                if (alphaBits == 7) {
                    char c = (char)(alphaAcc & 0x7F);
                    if (c >= 32 && c < 127) out.alpha += c; else out.alpha += '.';
                    alphaAcc = 0; alphaBits = 0;
                }
            }
        } else {
            // address codeword: 18-bit address [30:13], function [12:11]
            uint32_t addr = (w >> 13) & 0x3FFFF;
            uint8_t func = (w >> 11) & 0x03;
            uint32_t ric = (addr << 3) | (cw >> 1); // frame position = low 3 bits
            out.ric = ric; out.func = func; gotAddr = true;
        }
    }
    if (gotAddr) out.valid = true;
    return gotAddr;
}

// Hunt for a sync word (<=2 bit errors) and parse the following batch.
static bool decodePocsag(const uint8_t *bits, int nbits, Pocsag &out) {
    for (int i = 0; i + 32 + 512 <= nbits; i++) {
        uint32_t w = pack32(bits, i);
        if (popcount32(w ^ POCSAG_SYNC) <= 2) {
            Pocsag p{}; p.ric = 0; p.func = 0; p.valid = false;
            if (parseBatch(bits, i + 32, nbits, p)) {
                p.numeric.trim(); p.alpha.trim();
                out = p;
                return true;
            }
        }
    }
    return false;
}

// ── UI ────────────────────────────────────────────────────────────────────────
void deucePocsag() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;

    const uint32_t BAUDS[3] = { 512, 1200, 2400 };
    int baudIdx = 1;

    deinitRfModule();
    if (!initRfModule("rx", bruceConfigPins.rfFreq)) { displayError("RF init failed", true); return; }
    cc1101AsyncFsk();

    int gdo = bruceConfigPins.CC1101_bus.io0;
    if (gdo <= 0) { deinitRfModule(); displayError("No GDO0 pin set", true); return; }
    pinMode(gdo, INPUT);

    static uint8_t bits[MAXBITS];
    int caught = 0;
    String lastRic = "--", lastMsg = "(none yet)"; int lastFunc = 0;
    bool redraw = true;
    uint32_t lastHdr = 0;

    while (true) {
        if (check(EscPress)) break;
        if (check(NextPress)) { baudIdx = (baudIdx + 1) % 3; redraw = true; }

        // one capture + decode pass
        int nb = sampleWindow(gdo, BAUDS[baudIdx], bits, MAXBITS);
        Pocsag p;
        if (nb > 600 && decodePocsag(bits, nb, p)) {
            caught++;
            char r[16]; snprintf(r, sizeof(r), "%lu", (unsigned long)p.ric);
            lastRic = r; lastFunc = p.func;
            if (p.func == 0 && p.numeric.length()) lastMsg = p.numeric;
            else if (p.alpha.length()) lastMsg = p.alpha;
            else if (p.numeric.length()) lastMsg = p.numeric;
            else lastMsg = "(addr only)";
            redraw = true;
        }

        uint32_t now = millis();
        if (redraw || now - lastHdr > 500) {
            lastHdr = now;
            tft.fillScreen(bg);
            tft.setTextColor(TFT_ORANGE, bg); tft.setTextSize(1);
            tft.drawCentreString("POCSAG  *EXPERIMENTAL*", W / 2, 3, 1);
            tft.drawFastHLine(0, 15, W, sec);
            tft.setTextColor(sec, bg);
            char l[48];
            snprintf(l, sizeof(l), "%lu baud   caught: %d", (unsigned long)BAUDS[baudIdx], caught);
            tft.drawString(l, 6, 20);
            tft.setTextColor(pri, bg);
            snprintf(l, sizeof(l), "RIC: %s   fn: %d", lastRic.c_str(), lastFunc);
            tft.drawString(l, 6, 38);
            tft.setTextColor(sec, bg); tft.drawString("msg:", 6, 54);
            tft.setTextColor(pri, bg);
            // wrap the message
            String m = lastMsg; int x = 6, y = 66;
            for (size_t i = 0; i < m.length() && y < H - 26; ) {
                String chunk = m.substring(i, i + 30);
                tft.drawString(chunk.c_str(), x, y);
                i += 30; y += 12;
            }
            tft.setTextColor(sec, bg);
            tft.drawString("[NEXT] baud  [ESC] exit", 4, H - 22);
            tft.drawString("2-FSK; tune on real signals", 4, H - 11);
            redraw = false;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS); // feed watchdog between captures
    }
    ELECHOUSE_cc1101.SpiStrobe(STR_SIDLE);
    deinitRfModule();
}
