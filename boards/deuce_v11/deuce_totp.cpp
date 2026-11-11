// THE DEUCE v1.1 -- TOTP authenticator. See .h.

#include "deuce_totp.h"

#include <Arduino.h>
#include <SD.h>
#include <globals.h>
#include <ESP32Time.h>
#include <mbedtls/md.h>

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"

#define TOTP_CSV "/deuce/totp.csv"
#define TOTP_MAX 16

struct TotpAcct { String label; uint8_t secret[64]; int secLen; };

// ── base32 decode (RFC 4648, no padding required) ─────────────────────────────
static int base32Decode(const String &in, uint8_t *out, int outMax) {
    int buffer = 0, bits = 0, count = 0;
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        if (c == '=' || c == ' ' || c == '-') continue;
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a';
        else if (c >= '2' && c <= '7') v = c - '2' + 26;
        else continue;
        buffer = (buffer << 5) | v;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            if (count < outMax) out[count++] = (buffer >> bits) & 0xFF;
        }
    }
    return count;
}

// ── compute 6-digit TOTP for a counter ────────────────────────────────────────
static uint32_t totpCode(const uint8_t *key, int keyLen, uint64_t counter) {
    uint8_t msg[8];
    for (int i = 7; i >= 0; i--) { msg[i] = counter & 0xFF; counter >>= 8; }
    uint8_t hash[20];
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA1);
    mbedtls_md_hmac(info, key, keyLen, msg, 8, hash);
    int off = hash[19] & 0x0F;
    uint32_t bin = ((hash[off] & 0x7F) << 24) | ((hash[off + 1] & 0xFF) << 16) |
                   ((hash[off + 2] & 0xFF) << 8) | (hash[off + 3] & 0xFF);
    return bin % 1000000;
}

static int loadAccts(TotpAcct *a, int maxN) {
    if (!sdcardMounted) return 0;
    File f = SD.open(TOTP_CSV);
    if (!f) return 0;
    int n = 0;
    while (f.available() && n < maxN) {
        String line = f.readStringUntil('\n');
        line.trim();
        if (line.length() == 0) continue;
        int comma = line.indexOf(',');
        if (comma < 0) continue;
        a[n].label = line.substring(0, comma);
        String b32 = line.substring(comma + 1); b32.trim();
        a[n].secLen = base32Decode(b32, a[n].secret, sizeof(a[n].secret));
        if (a[n].secLen > 0) n++;
    }
    f.close();
    return n;
}

static void addAcct() {
    String label = keyboard("", 20, "Account name");
    if (label.length() == 0) return;
    String secret = keyboard("", 64, "Base32 secret");
    if (secret.length() == 0) return;
    // validate
    uint8_t tmp[64];
    if (base32Decode(secret, tmp, sizeof(tmp)) <= 0) {
        displayError("Bad base32 secret", true);
        return;
    }
    if (!sdcardMounted) { displayError("No SD card", true); return; }
    SD.mkdir("/deuce");
    File f = SD.open(TOTP_CSV, FILE_APPEND);
    if (!f) { displayError("Write failed", true); return; }
    label.replace(",", " "); secret.replace(",", "");
    f.print(label); f.print(","); f.println(secret);
    f.close();
    displaySuccess("Account added", true);
}

void deuceTotp() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    TotpAcct accts[TOTP_MAX];
    int n = loadAccts(accts, TOTP_MAX);
    int lastStep = -1, lastSec = -1;
    bool redraw = true;

    while (true) {
        if (check(EscPress)) break;
        if (check(SelPress)) { addAcct(); n = loadAccts(accts, TOTP_MAX); redraw = true; lastStep = -1; }

        uint32_t epoch = rtc.getEpoch();
        uint64_t step = epoch / 30;
        int secLeft = 30 - (epoch % 30);

        if (step != (uint64_t)lastStep || redraw) {
            tft.fillScreen(bg);
            tft.setTextColor(pri, bg); tft.setTextSize(1);
            tft.drawCentreString("TOTP", W / 2, 3, 1);
            tft.drawFastHLine(0, 15, W, sec);
            if (epoch < 1600000000UL) {
                tft.setTextColor(TFT_ORANGE, bg);
                tft.drawCentreString("RTC not set -- codes invalid", W / 2, 40, 1);
            } else if (n == 0) {
                tft.setTextColor(sec, bg);
                tft.drawCentreString("No accounts.", W / 2, 50, 1);
                tft.drawCentreString("[SEL] to add one", W / 2, 66, 1);
            } else {
                int y = 22;
                tft.setTextSize(2);
                for (int i = 0; i < n && y < H - 26; i++) {
                    uint32_t code = totpCode(accts[i].secret, accts[i].secLen, step);
                    tft.setTextColor(sec, bg); tft.setTextSize(1);
                    tft.drawString(accts[i].label.c_str(), 6, y);
                    char cs[8]; snprintf(cs, sizeof(cs), "%03lu %03lu",
                                         (unsigned long)(code / 1000), (unsigned long)(code % 1000));
                    tft.setTextColor(pri, bg); tft.setTextSize(2);
                    tft.drawRightString(cs, W - 6, y + 8, 1);
                    tft.setTextSize(1);
                    y += 30;
                }
            }
            tft.setTextSize(1);
            tft.setTextColor(sec, bg);
            tft.drawString("[SEL] add  [ESC] exit", 4, H - 11);
            lastStep = step; redraw = false; lastSec = -1;
        }

        // countdown bar (cheap redraw each second)
        if (secLeft != lastSec && n > 0 && epoch >= 1600000000UL) {
            int bw = (W - 8) * secLeft / 30;
            tft.fillRect(4, H - 20, W - 8, 4, bg);
            tft.fillRect(4, H - 20, bw, 4, secLeft <= 5 ? TFT_RED : TFT_GREEN);
            lastSec = secLeft;
        }
        vTaskDelay(120 / portTICK_PERIOD_MS);
    }
}
