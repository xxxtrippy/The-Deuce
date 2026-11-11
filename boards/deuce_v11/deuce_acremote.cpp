// THE DEUCE v1.1 -- universal IR air-conditioner remote. See .h.

#include "deuce_acremote.h"

#include <Arduino.h>
#include <globals.h>

#include <IRac.h>
#include <IRsend.h>

#include "core/display.h"
#include "modules/ir/ir_utils.h"

struct AcBrand { const char *name; decode_type_t proto; };

// Common, widely-supported A/C protocols. isProtocolSupported() filters out any
// not compiled into this build of the library at runtime.
static const AcBrand AC_BRANDS[] = {
    {"Coolix / generic", COOLIX},
    {"Daikin",           DAIKIN},
    {"Fujitsu",          FUJITSU_AC},
    {"Gree",             GREE},
    {"Hitachi",          HITACHI_AC},
    {"Kelvinator",       KELVINATOR},
    {"LG",               LG},
    {"Midea",            MIDEA},
    {"Mitsubishi",       MITSUBISHI_AC},
    {"Panasonic",        PANASONIC_AC},
    {"Samsung",          SAMSUNG_AC},
    {"Sanyo",            SANYO_AC},
    {"TCL 112",          TCL112AC},
    {"Toshiba",          TOSHIBA_AC},
    {"Trotec",           TROTEC},
    {"Whirlpool",        WHIRLPOOL_AC},
};
static const int AC_NBRANDS = sizeof(AC_BRANDS) / sizeof(AC_BRANDS[0]);

// mode cycle, with Off folded in as the last entry (sends power=false)
struct ModeOpt { const char *name; stdAc::opmode_t mode; bool power; };
static const ModeOpt MODES[] = {
    {"COOL", stdAc::opmode_t::kCool, true},
    {"HEAT", stdAc::opmode_t::kHeat, true},
    {"DRY",  stdAc::opmode_t::kDry,  true},
    {"FAN",  stdAc::opmode_t::kFan,  true},
    {"AUTO", stdAc::opmode_t::kAuto, true},
    {"OFF",  stdAc::opmode_t::kOff,  false},
};
static const int NMODES = sizeof(MODES) / sizeof(MODES[0]);

struct FanOpt { const char *name; stdAc::fanspeed_t fan; };
static const FanOpt FANS[] = {
    {"AUTO", stdAc::fanspeed_t::kAuto},
    {"LOW",  stdAc::fanspeed_t::kLow},
    {"MED",  stdAc::fanspeed_t::kMedium},
    {"HIGH", stdAc::fanspeed_t::kHigh},
    {"MAX",  stdAc::fanspeed_t::kMax},
};
static const int NFANS = sizeof(FANS) / sizeof(FANS[0]);

void deuceAcRemote() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;

    // ---- brand picker ----
    std::vector<Option> brands;
    std::vector<int> brandMap;
    for (int i = 0; i < AC_NBRANDS; i++) {
        if (IRac::isProtocolSupported(AC_BRANDS[i].proto)) {
            brands.push_back({AC_BRANDS[i].name, []() {}});
            brandMap.push_back(i);
        }
    }
    if (brands.empty()) { displayError("No AC protocols built", true); return; }
    int pick = loopOptions(brands, MENU_TYPE_SUBMENU, "AC Brand");
    if (pick < 0 || pick >= (int)brandMap.size()) return;
    int brand = brandMap[pick];

    // ---- control state ----
    int modeIdx = 0;   // COOL
    int fanIdx = 0;    // AUTO
    int tempC = 22;
    String status = "Ready";
    bool redraw = true;

    setup_ir_pin(bruceConfigPins.irTx, OUTPUT);

    while (true) {
        if (check(EscPress)) break;
        if (check(UpPress))   { if (tempC < 30) tempC++; redraw = true; }
        if (check(DownPress)) { if (tempC > 16) tempC--; redraw = true; }
        if (check(NextPress)) { modeIdx = (modeIdx + 1) % NMODES; redraw = true; }
        if (check(PrevPress)) { fanIdx = (fanIdx + 1) % NFANS; redraw = true; }

        if (check(SelPress)) {
            status = "Sending...";
            redraw = true;
            // draw the sending state immediately
            tft.fillRect(0, H - 28, W, 14, bg);
            tft.setTextColor(TFT_YELLOW, bg); tft.setTextSize(1);
            tft.drawString("Sending...", 8, H - 26);

            IRac ac(bruceConfigPins.irTx);
            ac.next.protocol = AC_BRANDS[brand].proto;
            ac.next.model = -1;
            ac.next.power = MODES[modeIdx].power;
            ac.next.mode = MODES[modeIdx].mode;
            ac.next.degrees = tempC;
            ac.next.celsius = true;
            ac.next.fanspeed = FANS[fanIdx].fan;
            ac.next.swingv = stdAc::swingv_t::kOff;
            ac.next.swingh = stdAc::swingh_t::kOff;
            ac.next.quiet = false; ac.next.turbo = false; ac.next.econo = false;
            ac.next.light = false; ac.next.filter = false; ac.next.clean = false;
            ac.next.beep = false; ac.next.sleep = -1; ac.next.clock = -1;
            bool ok = ac.sendAc();
            status = ok ? "Sent OK" : "Send failed";
            redraw = true;
        }

        if (redraw) {
            redraw = false;
            tft.fillScreen(bg);
            tft.setTextColor(pri, bg); tft.setTextSize(1);
            tft.drawCentreString("IR A/C REMOTE", W / 2, 3, 1);
            tft.drawFastHLine(0, 15, W, sec);
            tft.setTextColor(sec, bg);
            tft.drawString(AC_BRANDS[brand].name, 8, 20);

            // big temperature
            tft.setTextSize(5); tft.setTextColor(MODES[modeIdx].power ? pri : sec, bg);
            char t[8]; snprintf(t, sizeof(t), "%d", tempC);
            tft.drawString(t, 24, 40);
            int tw = tft.textWidth(t);
            tft.setTextSize(2); tft.drawString("C", 24 + tw + 6, 48);

            // mode + fan
            tft.setTextSize(2);
            uint16_t mc = MODES[modeIdx].power ? TFT_GREEN : TFT_RED;
            tft.setTextColor(mc, bg);
            tft.drawRightString(MODES[modeIdx].name, W - 10, 44, 1);
            tft.setTextColor(sec, bg); tft.setTextSize(1);
            char fl[16]; snprintf(fl, sizeof(fl), "fan: %s", FANS[fanIdx].name);
            tft.drawRightString(fl, W - 10, 66, 1);

            tft.setTextColor(status.startsWith("Sent") ? TFT_GREEN : (status.startsWith("Send f") ? TFT_RED : sec), bg);
            tft.drawString(status.c_str(), 8, H - 40);

            tft.setTextColor(sec, bg);
            tft.drawString("[UP/DN] temp  [NEXT] mode", 4, H - 24);
            tft.drawString("[PREV] fan  [SEL] send  [ESC]", 4, H - 12);
        }
        vTaskDelay(25 / portTICK_PERIOD_MS);
    }
    digitalWrite(bruceConfigPins.irTx, LED_OFF);
}
