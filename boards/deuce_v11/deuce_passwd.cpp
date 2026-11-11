// THE DEUCE v1.1 -- password generator + strength meter. See .h.

#include "deuce_passwd.h"

#include <Arduino.h>
#include <esp_system.h>
#include <globals.h>
#include <math.h>

#include "core/display.h"
#include "core/mykeyboard.h"

static uint32_t prng() {
    uint32_t r = esp_random();
    r ^= ((uint32_t)analogRead(39) & 0x1F) << 3;
    r ^= (uint32_t)micros();
    return r;
}

static const char *LOWER = "abcdefghijklmnopqrstuvwxyz";
static const char *UPPER = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const char *DIGIT = "0123456789";
static const char *SYMB  = "!@#$%^&*()-_=+[]{};:,.?";

static String genPass(int len, bool lo, bool up, bool dg, bool sy) {
    String pool;
    if (lo) pool += LOWER;
    if (up) pool += UPPER;
    if (dg) pool += DIGIT;
    if (sy) pool += SYMB;
    if (pool.length() == 0) pool = LOWER;
    String p;
    for (int i = 0; i < len; i++) p += pool[prng() % pool.length()];
    return p;
}

// rough entropy estimate for a typed password
static float entropyBits(const String &p) {
    bool lo = false, up = false, dg = false, sy = false;
    for (size_t i = 0; i < p.length(); i++) {
        char c = p[i];
        if (c >= 'a' && c <= 'z') lo = true;
        else if (c >= 'A' && c <= 'Z') up = true;
        else if (c >= '0' && c <= '9') dg = true;
        else sy = true;
    }
    int pool = (lo ? 26 : 0) + (up ? 26 : 0) + (dg ? 10 : 0) + (sy ? 30 : 0);
    if (pool < 2) pool = 2;
    return p.length() * (log((double)pool) / log(2.0));
}
static const char *verdict(float bits) {
    if (bits < 28) return "VERY WEAK";
    if (bits < 36) return "WEAK";
    if (bits < 60) return "REASONABLE";
    if (bits < 128) return "STRONG";
    return "VERY STRONG";
}

void deucePassGen() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;
    int mode = 0; // 0 generate, 1 check
    int len = 16; bool lo = true, up = true, dg = true, sy = true;
    int toggle = 0; // which charset NEXT edits in generate mode
    String pass = genPass(len, lo, up, dg, sy);
    String chk = ""; float chkBits = 0;
    bool redraw = true;

    while (true) {
        if (check(EscPress)) break;
        if (check(PrevPress)) { mode ^= 1; redraw = true; }

        if (mode == 0) {
            if (check(UpPress)) { if (len < 48) len++; pass = genPass(len, lo, up, dg, sy); redraw = true; }
            if (check(DownPress)) { if (len > 4) len--; pass = genPass(len, lo, up, dg, sy); redraw = true; }
            if (check(NextPress)) { toggle = (toggle + 1) % 4; redraw = true; }
            if (check(SelPress)) {
                // SEL toggles the highlighted set, then regenerates
                if (toggle == 0) lo = !lo; else if (toggle == 1) up = !up;
                else if (toggle == 2) dg = !dg; else sy = !sy;
                pass = genPass(len, lo, up, dg, sy); redraw = true;
            }
        } else {
            if (check(SelPress)) { chk = keyboard("", 64, "Password to check"); chkBits = entropyBits(chk); redraw = true; }
        }

        if (redraw) {
            redraw = false;
            tft.fillScreen(bg);
            tft.setTextColor(pri, bg); tft.setTextSize(1);
            tft.drawCentreString(mode == 0 ? "PASSWORD GEN" : "STRENGTH CHECK", W / 2, 4, 1);
            tft.drawFastHLine(0, 16, W, sec);
            if (mode == 0) {
                // wrapped password
                tft.setTextColor(pri, bg);
                int cpl = (W - 12) / 6; int y = 24;
                for (size_t i = 0; i < pass.length(); i += cpl) { tft.drawString(pass.substring(i, i + cpl), 6, y); y += 12; }
                float bits = entropyBits(pass);
                char l[40]; snprintf(l, sizeof(l), "len %d   ~%d bits  %s", len, (int)bits, verdict(bits));
                tft.setTextColor(sec, bg); tft.drawString(l, 6, 64);
                // charset toggles
                const char *names[4] = {"a-z", "A-Z", "0-9", "sym"};
                bool st[4] = {lo, up, dg, sy};
                int x = 6;
                for (int i = 0; i < 4; i++) {
                    uint16_t c = st[i] ? TFT_GREEN : TFT_DARKGREY;
                    if (i == toggle) tft.drawRect(x - 2, 78, 34, 14, pri);
                    tft.setTextColor(c, bg); tft.drawString(names[i], x, 80); x += 40;
                }
                tft.setTextColor(sec, bg);
                tft.drawString("[UP/DN] len [NEXT] set [SEL] tgl", 4, H - 22);
                tft.drawString("[PREV] check mode  [ESC]", 4, H - 11);
            } else {
                tft.setTextColor(pri, bg);
                if (chk.length()) {
                    int cpl = (W - 12) / 6; int y = 26;
                    for (size_t i = 0; i < chk.length(); i += cpl) { tft.drawString(chk.substring(i, i + cpl), 6, y); y += 12; }
                    tft.setTextSize(2);
                    uint16_t vc = chkBits < 36 ? TFT_RED : chkBits < 60 ? TFT_ORANGE : TFT_GREEN;
                    tft.setTextColor(vc, bg); tft.drawCentreString(verdict(chkBits), W / 2, 60, 1);
                    tft.setTextSize(1); tft.setTextColor(sec, bg);
                    char l[32]; snprintf(l, sizeof(l), "~%d bits entropy", (int)chkBits);
                    tft.drawCentreString(l, W / 2, 88, 1);
                } else {
                    tft.setTextColor(sec, bg); tft.drawCentreString("[SEL] enter a password", W / 2, 50, 1);
                }
                tft.setTextColor(sec, bg);
                tft.drawString("[SEL] check [PREV] gen mode [ESC]", 4, H - 11);
            }
        }
        vTaskDelay(30 / portTICK_PERIOD_MS);
    }
}
