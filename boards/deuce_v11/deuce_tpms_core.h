#pragma once
// THE DEUCE v1.1 -- TPMS decode core (pure, host-testable, no Arduino deps).
//
// Faithful ports of rtl_433's bit primitives and five TPMS protocol decoders:
//   Toyota   (FSK, differential-Manchester, 9 bytes, CRC-8 poly 0x07 init 0x80)
//   Ford     (FSK, Manchester, 8 bytes, 8-bit sum checksum)          [Continental/VDO]
//   Nissan   (FSK, Manchester, 37 bits, sum2N checksum)
//   Schrader (OOK Manchester, 68 bits, CRC-8 poly 0x07 init 0xf0)    [FCC MRXGG4]
//   Schrader EG53MA4 (OOK Manchester, 120 bits, byte-sum checksum)   [GM/Chevy/Opel OEM]
//   GM-Aftermarket   (OOK Manchester, 130 bits, byte-sum checksum)   [GM/Chevy aftermarket]
//
// Each protocol exposes two entry points sharing one parse body:
//   tpms_*_from_bytes(b): the post-Manchester parse + integrity check -- fed the
//       exact decoded payload rtl_433's decode_fn receives. This is what the host
//       tests pin against ground-truth sample packets.
//   tpms_*(raw): the full on-air pipeline (preamble search + Manchester/
//       differential-Manchester decode) that the firmware feeds captured bits.
//
// References (bit-faithful): rtl_433 src/bitbuffer.c and src/devices/{tpms_toyota,
// tpms_ford,tpms_nissan,schraeder,tpms_gm}.c (GPL-2.0-or-later). Field formulas,
// preambles, checksum polys and bit layouts mirror those sources exactly.

#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Decoded reading, SI-normalised (kPa + deg C). Pressure/temp validity flagged.
// ---------------------------------------------------------------------------
struct TpmsReading {
    const char *model;    // static string
    uint32_t id_hi;       // high id bits (GM id is 40-bit; most are <=32)
    uint32_t id_lo;       // low 32 id bits
    bool have_pressure;
    float pressure_kpa;
    bool have_temp;
    float temp_c;
    bool have_batt;
    bool batt_ok;
    bool have_flags;
    uint32_t flags;
    bool valid;
};

static inline void tpms_reading_init(TpmsReading *r) { memset(r, 0, sizeof(*r)); }

// ---------------------------------------------------------------------------
// Minimal MSB-first bit buffer (max 40 bytes = 320 bits, ample for TPMS).
// Mirrors the subset of rtl_433's bitbuffer_t API the decoders use.
// ---------------------------------------------------------------------------
#define TB_MAX_BYTES 40
struct TB {
    uint8_t bb[TB_MAX_BYTES];
    unsigned nbits;
};

static inline void tb_init(TB *t) {
    memset(t->bb, 0, sizeof(t->bb));
    t->nbits = 0;
}

static inline void tb_add_bit(TB *t, uint8_t bit) {
    if (t->nbits >= TB_MAX_BYTES * 8) return;
    if (bit) t->bb[t->nbits >> 3] |= (uint8_t)(0x80 >> (t->nbits & 7));
    t->nbits++;
}

static inline uint8_t tb_bit_at(const uint8_t *bytes, unsigned bit) {
    return (uint8_t)((bytes[bit >> 3] >> (7 - (bit & 7))) & 1);
}

// Port of bitbuffer_search: first bit index at/after `start` where `pattern`
// (pattern_bits long, MSB-first) matches; returns nbits if not found.
static inline unsigned
tb_search(const TB *t, unsigned start, const uint8_t *pattern, unsigned pattern_bits) {
    unsigned len = t->nbits;
    unsigned ipos = start, ppos = 0;
    while (ipos < len && ppos < pattern_bits) {
        if (tb_bit_at(t->bb, ipos) == tb_bit_at(pattern, ppos)) {
            ppos++;
            ipos++;
            if (ppos == pattern_bits) return ipos - pattern_bits;
        } else {
            ipos -= ppos;
            ipos++;
            ppos = 0;
        }
    }
    return len;
}

// Port of bitbuffer_extract_bytes (bit-aligned copy of `len` bits from `pos`).
static inline void tb_extract_bytes(const TB *t, unsigned pos, uint8_t *out, unsigned len) {
    if (len == 0) return;
    for (unsigned i = 0; i < len; i++) {
        uint8_t bit = (pos + i < t->nbits) ? tb_bit_at(t->bb, pos + i) : 0;
        if (i & 7) out[i >> 3] |= bit << (7 - (i & 7));
        else out[i >> 3] = bit << 7;
    }
}

static inline void tb_invert(TB *t) {
    unsigned bytes = (t->nbits + 7) >> 3;
    for (unsigned i = 0; i < bytes; i++) t->bb[i] = ~t->bb[i];
}

// Port of bitbuffer_manchester_decode (01->1, 10->0; 00/11 = coding violation
// -> stop). Returns the input bit index consumed.
static inline unsigned tb_manchester_decode(const TB *in, unsigned start, TB *out, unsigned maxbits) {
    unsigned len = in->nbits;
    unsigned ipos = start;
    if (maxbits && len > start + (maxbits * 2)) len = start + (maxbits * 2);
    while (ipos < len) {
        uint8_t b1 = tb_bit_at(in->bb, ipos++);
        uint8_t b2 = tb_bit_at(in->bb, ipos++);
        if (b1 == b2) break;
        tb_add_bit(out, b2);
    }
    return ipos;
}

// Port of bitbuffer_differential_manchester_decode.
static inline unsigned
tb_diff_manchester_decode(const TB *in, unsigned start, TB *out, unsigned maxbits) {
    unsigned len = in->nbits;
    unsigned ipos = start;
    uint8_t bit1, bit2 = 0;
    if (maxbits && len > start + (maxbits * 2)) len = start + (maxbits * 2);
    // sync: consume short pulses until the clock is established
    while (ipos < len) {
        bit1 = tb_bit_at(in->bb, ipos++);
        bit2 = tb_bit_at(in->bb, ipos++);
        uint8_t bit3 = (ipos < in->nbits) ? tb_bit_at(in->bb, ipos) : 0;
        if (bit1 != bit2) {
            if (bit2 != bit3) {
                tb_add_bit(out, 0);
            } else {
                bit2 = bit1;
                ipos -= 1;
                break;
            }
        } else {
            bit2 = 1 - bit1;
            ipos -= 2;
            break;
        }
    }
    while (ipos < len) {
        bit1 = tb_bit_at(in->bb, ipos++);
        if (bit1 == bit2) break; // clock missing
        bit2 = tb_bit_at(in->bb, ipos++);
        if (bit1 == bit2) tb_add_bit(out, 1);
        else tb_add_bit(out, 0);
    }
    return ipos;
}

// ---------------------------------------------------------------------------
// Integrity primitives
// ---------------------------------------------------------------------------
static inline uint8_t tpms_crc8(const uint8_t *data, unsigned len, uint8_t poly, uint8_t init) {
    uint8_t crc = init;
    for (unsigned i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ poly);
            else crc = (uint8_t)(crc << 1);
        }
    }
    return crc;
}

static inline uint8_t tpms_add_bytes(const uint8_t *data, unsigned len) {
    unsigned s = 0;
    for (unsigned i = 0; i < len; i++) s += data[i];
    return (uint8_t)(s & 0xff);
}

static inline float tpms_psi_to_kpa(float psi) { return psi * 6.894757f; }
static inline float tpms_f_to_c(float f) { return (f - 32.0f) / 1.8f; }

// ===========================================================================
// Protocol parsers -- byte-level (post-Manchester) bodies
// ===========================================================================

// Toyota: 9 decoded bytes. CRC-8 poly 0x07 init 0x80 over b[0..7] == b[8].
static inline bool tpms_toyota_from_bytes(const uint8_t *b, TpmsReading *out) {
    if (tpms_crc8(b, 8, 0x07, 0x80) != b[8]) return false;
    unsigned pressure1 = ((b[4] & 0x7f) << 1) | (b[5] >> 7);
    unsigned pressure2 = (unsigned)(b[7] ^ 0xff);
    if (pressure1 != pressure2) return false;
    unsigned temp = ((b[5] & 0x7f) << 1) | (b[6] >> 7);
    tpms_reading_init(out);
    out->model = "Toyota";
    out->id_lo = (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
    out->have_pressure = true;
    out->pressure_kpa = tpms_psi_to_kpa(pressure1 * 0.25f - 7.0f);
    out->have_temp = true;
    out->temp_c = (float)temp - 40.0f;
    out->have_flags = true;
    out->flags = (b[4] & 0x80) | (b[6] & 0x7f);
    out->valid = true;
    return true;
}

// Ford: 8 decoded bytes. sum(b[0..6]) & 0xff == b[7].
static inline bool tpms_ford_from_bytes(const uint8_t *b, TpmsReading *out) {
    if (tpms_add_bytes(b, 7) != b[7]) return false;
    // flag plausibility (rtl_433 rejects unrecognised state bits)
    uint8_t st = b[6] & 0x4c;
    if (st != 0x08 && st != 0x04 && st != 0x44 && st != 0x00) {
        // 0x00 is "at rest with neither moving nor learn" seen in practice; keep it
        if (st != 0x00) return false;
    }
    if (b[6] & 0x90) return false; // 0x80/0x10 expected zero
    tpms_reading_init(out);
    out->model = "Ford";
    out->id_lo = (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
    int psibits = (((b[6] & 0x20) << 3) | b[4]);
    out->have_pressure = true;
    out->pressure_kpa = tpms_psi_to_kpa(psibits * 0.25f);
    if ((b[5] & 0x80) == 0) {
        out->have_temp = true;
        out->temp_c = (float)((b[5] & 0x7f) - 56);
    }
    out->have_flags = true;
    out->flags = b[6];
    out->valid = true;
    return true;
}

// Nissan: sum2N checksum == 0 over 5 decoded bytes (already inverted).
static inline uint8_t tpms_nissan_checksum(const uint8_t *b) {
    uint8_t chk = 0;
    for (int i = 0; i < 4; i++) {
        chk += b[i] >> 7;
        chk += b[i] >> 5;
        chk += b[i] >> 3;
        chk += b[i] >> 1;
        chk += (uint8_t)(b[i] << 1);
    }
    chk += b[4] >> 7;
    chk += b[4] >> 5;
    chk += b[4] >> 3;
    return (uint8_t)(~chk & 0x03);
}
static inline bool tpms_nissan_from_bytes(const uint8_t *b, TpmsReading *out) {
    if (tpms_nissan_checksum(b) != 0) return false;
    int pressure_raw = ((b[3] & 0x1F) << 3) | (b[4] >> 5);
    float psi = pressure_raw / 4.0f - 3.0f;
    tpms_reading_init(out);
    out->model = "Nissan";
    out->id_lo = (uint32_t)((b[0] & 0x1F) << 19) | (uint32_t)(b[1] << 11) | (uint32_t)(b[2] << 3) |
                 (uint32_t)(b[3] >> 5);
    out->have_pressure = true;
    out->pressure_kpa = tpms_psi_to_kpa(psi);
    out->have_flags = true;
    out->flags = b[0] >> 5; // mode
    out->valid = true;
    return true;
}

// Schrader base (MRXGG4): 8 decoded bytes (after dropping the sync nibble).
// CRC-8 poly 0x07 init 0xf0 over b[0..6] == b[7].
static inline bool tpms_schrader_from_bytes(const uint8_t *b, TpmsReading *out) {
    if (tpms_crc8(b, 7, 0x07, 0xf0) != b[7]) return false;
    tpms_reading_init(out);
    out->model = "Schrader";
    out->id_lo = (uint32_t)((b[1] & 0x0F) << 24) | (uint32_t)(b[2] << 16) | (uint32_t)(b[3] << 8) | b[4];
    out->have_pressure = true;
    out->pressure_kpa = b[5] * 25 * 0.1f; // 25 mbar/bit -> kPa
    out->have_temp = true;
    out->temp_c = (float)((int)b[6] - 50);
    out->have_flags = true;
    out->flags = (uint32_t)((b[0] & 0x0F) << 4) | (b[1] >> 4);
    out->valid = true;
    return true;
}

// Schrader EG53MA4 (GM/Chevy/Opel OEM): 10 decoded bytes (after dropping 40 bits).
// add_bytes(b[0..8]) & 0xff == b[9]. Temperature reported in Fahrenheit.
static inline bool tpms_schrader_eg53ma4_from_bytes(const uint8_t *b, TpmsReading *out) {
    if (!b[1] && !b[2] && !b[4] && !b[5] && !b[7] && !b[8]) return false; // all-zero sanity
    if (tpms_add_bytes(b, 9) != b[9]) return false;
    tpms_reading_init(out);
    out->model = "Schrader-GM";
    out->id_lo = (uint32_t)(b[4] << 16) | (uint32_t)(b[5] << 8) | b[6];
    out->have_pressure = true;
    out->pressure_kpa = b[7] * 25 * 0.1f;
    out->have_temp = true;
    out->temp_c = tpms_f_to_c((float)b[8]); // sensor reports degF
    out->have_flags = true;
    out->flags = (uint32_t)b[0] << 24 | (uint32_t)b[1] << 16 | (uint32_t)b[2] << 8 | b[3];
    out->valid = true;
    return true;
}

// GM Aftermarket: 17 extracted bytes from 130 decoded bits. b[0..5] zero preamble,
// sum(b[6..14]) & 0xff == b[15].
static inline bool tpms_gm_from_bytes(const uint8_t *b, TpmsReading *out) {
    for (int i = 0; i < 6; i++)
        if (b[i] != 0) return false;
    uint8_t sum = 0;
    int all_zero = 1;
    for (int i = 6; i < 15; i++) {
        sum += b[i];
        all_zero &= (b[i] == 0);
    }
    if (sum != b[15]) return false;
    if (all_zero && b[15] == 0) return false;
    tpms_reading_init(out);
    out->model = "GM";
    out->id_hi = b[8];
    out->id_lo = (uint32_t)b[9] << 24 | (uint32_t)b[10] << 16 | (uint32_t)b[11] << 8 | b[12];
    uint32_t flags = (uint32_t)(b[6] << 8) | b[7];
    out->have_pressure = true;
    out->pressure_kpa = b[13] * 2.75f;
    out->have_temp = true;
    out->temp_c = (float)((int)b[14] - 60);
    out->have_batt = true;
    out->batt_ok = !((flags >> 5) & 1);
    out->have_flags = true;
    out->flags = flags;
    out->valid = true;
    return true;
}

// ===========================================================================
// Protocol parsers -- full on-air pipeline (raw captured bits -> reading)
// ===========================================================================

static inline bool tpms_toyota(const TB *raw, TpmsReading *out) {
    const uint8_t preamble[2] = {0xa9, 0xe0}; // 12 bits
    unsigned bitpos = 0;
    while ((bitpos = tb_search(raw, bitpos, preamble, 12)) + 156 <= raw->nbits) {
        TB pkt;
        tb_init(&pkt);
        unsigned start = bitpos + 11;
        unsigned endp = tb_diff_manchester_decode(raw, start, &pkt, 80);
        if (endp - start >= 144 && pkt.nbits >= 72) {
            if (tpms_toyota_from_bytes(pkt.bb, out)) return true;
        }
        bitpos += 2;
    }
    return false;
}

static inline bool tpms_ford(const TB *raw_in, TpmsReading *out) {
    TB raw = *raw_in;
    tb_invert(&raw); // Ford decodes the inverted stream
    const uint8_t preamble[2] = {0xaa, 0xa9}; // 16 bits
    unsigned bitpos = 0;
    while ((bitpos = tb_search(&raw, bitpos, preamble, 16)) + 144 <= raw.nbits) {
        TB pkt;
        tb_init(&pkt);
        tb_manchester_decode(&raw, bitpos + 16, &pkt, 160);
        if (pkt.nbits >= 64) {
            if (tpms_ford_from_bytes(pkt.bb, out)) return true;
        }
        bitpos += 15;
    }
    return false;
}

static inline bool tpms_nissan(const TB *raw, TpmsReading *out) {
    const uint8_t preamble[5] = {0xf5, 0x55, 0x55, 0x55, 0xe0}; // 36 bits
    unsigned bitpos = 0;
    while ((bitpos = tb_search(raw, bitpos, preamble, 36)) + 77 <= raw->nbits) {
        TB pkt;
        tb_init(&pkt);
        tb_manchester_decode(raw, bitpos + 36, &pkt, 113);
        if (pkt.nbits >= 37) {
            tb_invert(&pkt); // G.E. Thomas convention
            if (tpms_nissan_from_bytes(pkt.bb, out)) return true;
        }
        bitpos += 1;
    }
    return false;
}

// The OOK Manchester trio (Schrader / EG53MA4 / GM) were Manchester-decoded by
// rtl_433's slicer before reaching the decode_fn. Here we Manchester-decode the
// captured raw stream ourselves, trying both polarities and a one-bit start
// offset so we are robust to the CC1101 slicer's phase/inversion on hardware.
typedef bool (*tpms_byte_fn)(const uint8_t *, TpmsReading *);

static inline bool
tpms_ook_mc_try(const TB *raw, unsigned need_decoded_bits, unsigned extract_pos, unsigned extract_bits,
                tpms_byte_fn fn, TpmsReading *out) {
    for (int inv = 0; inv < 2; inv++) {
        TB r = *raw;
        if (inv) tb_invert(&r);
        for (unsigned off = 0; off < 2; off++) {
            TB dec;
            tb_init(&dec);
            tb_manchester_decode(&r, off, &dec, need_decoded_bits + 4);
            if (dec.nbits < extract_pos + extract_bits) continue;
            uint8_t b[TB_MAX_BYTES];
            memset(b, 0, sizeof(b));
            TB tmp = dec; // extract from decoded stream
            tb_extract_bytes(&tmp, extract_pos, b, extract_bits);
            if (fn(b, out)) return true;
        }
    }
    return false;
}

static inline bool tpms_schrader(const TB *raw, TpmsReading *out) {
    // 68 decoded bits; drop 4 sync bits, parse 64 (8 bytes).
    return tpms_ook_mc_try(raw, 68, 4, 64, tpms_schrader_from_bytes, out);
}

static inline bool tpms_schrader_eg53ma4(const TB *raw, TpmsReading *out) {
    // 120 decoded bits; drop first 40, parse 80 (10 bytes).
    return tpms_ook_mc_try(raw, 120, 40, 80, tpms_schrader_eg53ma4_from_bytes, out);
}

static inline bool tpms_gm(const TB *raw, TpmsReading *out) {
    // 130 decoded bits; parse from bit 0 (16 extracted bytes = 128 bits + 2).
    return tpms_ook_mc_try(raw, 130, 0, 130, tpms_gm_from_bytes, out);
}

// Try every protocol against one captured frame. `is_fsk` selects which family
// to run (the radio can only be in one modulation at a time), 0 = OOK group.
static inline bool tpms_decode_any(const TB *raw, bool is_fsk, TpmsReading *out) {
    if (is_fsk) {
        if (tpms_toyota(raw, out)) return true;
        if (tpms_ford(raw, out)) return true;
        if (tpms_nissan(raw, out)) return true;
    } else {
        if (tpms_schrader(raw, out)) return true;
        if (tpms_schrader_eg53ma4(raw, out)) return true;
        if (tpms_gm(raw, out)) return true;
    }
    return false;
}
