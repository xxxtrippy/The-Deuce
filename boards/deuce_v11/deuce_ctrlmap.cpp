// THE DEUCE v1.1 -- controller button -> menu-tab shortcut map. See .h.

#include "deuce_ctrlmap.h"

#include <Arduino.h>
#include <Preferences.h>
#include <globals.h>

#include "core/display.h"

// assignable buttons (A/B reserved for Select/Back, so start at X)
struct Btn { const char *name; int idx; };
static const Btn BTNS[] = {
    {"X", 2}, {"Y", 3}, {"LB", 4}, {"RB", 5}, {"View", 6}, {"Menu", 7},
};
static const int NBTN = sizeof(BTNS) / sizeof(BTNS[0]);

// jump targets (index values must match deuceHandleCtrlJump in deuce_blehid.cpp)
struct Tgt { const char *name; int jump; };
static const Tgt TGTS[] = {
    {"(none)", -1}, {"Settings", 0}, {"WiFi", 1}, {"Bluetooth", 2}, {"RF", 3},
    {"NRF24", 4}, {"IR", 5}, {"GPS", 6}, {"RFID/I2C", 7}, {"Others", 8},
    {"Clock", 9}, {"Files", 10},
};
static const int NTGT = sizeof(TGTS) / sizeof(TGTS[0]);

static int g_short[8];
static bool g_loaded = false;

static void ctrlLoad() {
    for (int i = 0; i < 8; i++) g_short[i] = -1;
    Preferences p;
    p.begin("deucectrl", true);
    for (int i = 0; i < NBTN; i++) {
        char k[4]; snprintf(k, sizeof(k), "b%d", BTNS[i].idx);
        g_short[BTNS[i].idx] = p.getInt(k, -1);
    }
    p.end();
    g_loaded = true;
}

int ctrlShortcutFor(int button) {
    if (!g_loaded) ctrlLoad();
    if (button < 0 || button > 7) return -1;
    return g_short[button];
}

static const char *tgtName(int jump) {
    for (int i = 0; i < NTGT; i++) if (TGTS[i].jump == jump) return TGTS[i].name;
    return "(none)";
}

void deuceControllerMenu() {
    if (!g_loaded) ctrlLoad();
    while (true) {
        std::vector<Option> opts;
        for (int i = 0; i < NBTN; i++) {
            String label = String(BTNS[i].name) + " -> " + tgtName(g_short[BTNS[i].idx]);
            opts.push_back({label.c_str(), []() {}});
        }
        opts.push_back({"Back", []() {}});
        int sel = loopOptions(opts, MENU_TYPE_SUBMENU, "Controller shortcuts");
        if (sel < 0 || sel >= NBTN) return; // Back or Esc

        // choose a target for the selected button
        std::vector<Option> t;
        for (int i = 0; i < NTGT; i++) t.push_back({TGTS[i].name, []() {}});
        String ttl = String(BTNS[sel].name) + " jumps to";
        int ts = loopOptions(t, MENU_TYPE_SUBMENU, ttl.c_str());
        if (ts < 0 || ts >= NTGT) continue;

        g_short[BTNS[sel].idx] = TGTS[ts].jump;
        Preferences p;
        p.begin("deucectrl", false);
        char k[4]; snprintf(k, sizeof(k), "b%d", BTNS[sel].idx);
        p.putInt(k, TGTS[ts].jump);
        p.end();
    }
}
