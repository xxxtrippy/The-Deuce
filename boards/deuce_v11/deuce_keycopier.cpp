// THE DEUCE v1.1 -- Key Copier: mechanical key bitting visualizer (offline). See .h.

#include "deuce_keycopier.h"

#include <Arduino.h>
#include <globals.h>

#include "core/display.h"
#include "core/mykeyboard.h"

struct Keyway {
    const char *name;
    const char *code; // short tag drawn on the key
    int cuts;
    int minD;
    int maxD;
};

// Common keyways. Depths are single-digit bitting numbers (shallow->deep).
static const Keyway KW[] = {
    {"KW1 Kwikset",   "KW1", 5, 1, 7},
    {"SC1 Schlage",   "SC1", 6, 0, 9},
    {"WR5 Weiser",    "WR5", 5, 1, 7},
    {"KW10 Kwikset",  "KW10", 6, 1, 7},
    {"Y1 Yale",       "Y1",  5, 1, 9},
    {"Car edge 4-depth", "CAR", 8, 1, 4},
    {"Custom",        "KEY", 0, 0, 9},
};
static const int NKW = sizeof(KW) / sizeof(KW[0]);

static void drawKey(const char *code, int cuts, int minD, int maxD, const int *d) {
    int W = tft.width(), H = tft.height();
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    tft.fillScreen(bg);
    tft.setTextSize(1);
    tft.setTextColor(pri, bg);
    tft.drawString("KEY COPIER", 6, 6);
    tft.setTextColor(sec, bg);
    tft.drawString(code, W - 6 - tft.textWidth(code), 6);
    tft.drawFastHLine(0, 22, W, sec);

    int x0 = 24, x1 = W - 24, span = x1 - x0;
    int spine = 96;   // top (shallow) line
    int maxDrop = 64; // pixels for the deepest cut
    int cutHalf = (span / cuts) / 3;

    // cut-number labels + down arrow over the first cut
    tft.setTextColor(pri, bg);
    for (int i = 0; i < cuts; i++) {
        int cx = x0 + span * (2 * i + 1) / (2 * cuts);
        char n[4];
        snprintf(n, sizeof(n), "%d", d[i]);
        tft.drawString(n, cx - tft.textWidth(n) / 2, spine - 44);
        if (i == 0) {
            tft.drawLine(cx, spine - 34, cx, spine - 26, pri);
            tft.drawLine(cx, spine - 26, cx - 3, spine - 30, pri);
            tft.drawLine(cx, spine - 26, cx + 3, spine - 30, pri);
        }
    }

    // shoulder (left) + spine (top straight edge)
    tft.drawLine(x0, spine - 18, x0, spine, pri); // shoulder
    // cut (bottom) profile as a zig-zag of flats + valleys
    int px = x0, py = spine;
    for (int i = 0; i < cuts; i++) {
        int cx = x0 + span * (2 * i + 1) / (2 * cuts);
        int denom = (maxD - minD);
        if (denom < 1) denom = 1;
        int valley = spine + (d[i] - minD) * maxDrop / denom;
        tft.drawLine(px, py, cx - cutHalf, spine, pri); // flat up to the cut
        tft.drawLine(cx - cutHalf, spine, cx, valley, pri);
        tft.drawLine(cx, valley, cx + cutHalf, spine, pri);
        px = cx + cutHalf;
        py = spine;
    }
    tft.drawLine(px, py, x1, spine, pri);          // flat to the tip
    tft.drawLine(x1, spine, x1 + 14, spine + 24, pri); // tip taper (clamped by screen)
    // top spine line + bow hint
    tft.drawLine(x0, spine - 18, x1, spine - 18, pri);
    tft.fillCircle(x0 - 8, spine - 9, 7, sec);

    // bitting code readout
    String code_s;
    for (int i = 0; i < cuts; i++) {
        code_s += String(d[i]);
        if (i < cuts - 1) code_s += "-";
    }
    tft.setTextSize(2);
    tft.setTextColor(pri, bg);
    tft.drawString(code_s, 10, H - 54);
    tft.setTextSize(1);
    tft.setTextColor(sec, bg);
    tft.drawString(String(cuts) + " cuts, depths " + String(minD) + "-" + String(maxD), 10, H - 30);
    tft.drawString("[SEL] new   [ESC] exit", 10, H - 14);
}

void deuceKeyCopier() {
    while (true) {
        // 1) keyway
        int idx = 0;
        options.clear();
        for (int i = 0; i < NKW; i++) {
            int ii = i;
            options.push_back({KW[i].name, [&idx, ii]() { idx = ii; }});
        }
        options.push_back({"< Exit", [&idx]() { idx = -1; }});
        loopOptions(options, MENU_TYPE_SUBMENU, "Key: blank / keyway");
        if (idx < 0) return;

        int cuts = KW[idx].cuts, minD = KW[idx].minD, maxD = KW[idx].maxD;
        const char *code = KW[idx].code;

        if (cuts == 0) { // Custom
            String cs = num_keyboard("", 2, "Number of cuts (3-12):");
            cuts = cs.toInt();
            if (cuts < 3 || cuts > 12) {
                displayError("Cuts must be 3-12", true);
                continue;
            }
            String ms = num_keyboard("", 1, "Deepest depth number (max 9):");
            maxD = ms.toInt();
            if (maxD < 1 || maxD > 9) maxD = 9;
            minD = 0;
        }

        // 2) bitting
        String prompt = String(cuts) + " cuts, each " + String(minD) + "-" + String(maxD);
        String b = num_keyboard("", cuts, prompt.c_str());
        b.trim();
        if ((int)b.length() != cuts) {
            displayError("Enter exactly " + String(cuts) + " digits", true);
            continue;
        }
        int d[12];
        bool okvals = true;
        for (int i = 0; i < cuts; i++) {
            d[i] = b[i] - '0';
            if (d[i] < minD || d[i] > maxD) okvals = false;
        }
        if (!okvals) {
            displayError("Each cut must be " + String(minD) + "-" + String(maxD), true);
            continue;
        }

        // 3) draw + wait
        drawKey(code, cuts, minD, maxD, d);
        while (true) {
            if (check(EscPress)) return;
            if (check(SelPress)) break; // new key
            delay(20);
        }
    }
}
