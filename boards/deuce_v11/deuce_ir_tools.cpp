// THE DEUCE v1.1 -- IR Intervalometer.
//
// Fires a camera's IR shutter-release code at a fixed interval for a set number of shots
// (0 = run forever). Uses Bruce's sendRawCommand(), which drives the configured IR TX pin
// (IR -> Config -> Ir TX Pin). Built-in raw sequences for a few common bodies, plus a
// Custom option to paste any raw (space-separated microseconds) + carrier frequency.

#include "deuce_ir_tools.h"

#include <Arduino.h>
#include "core/display.h"
#include "core/mykeyboard.h"
#include "modules/ir/custom_ir.h"
#include <globals.h>
#include <vector>

struct CamIR {
    const char *name;
    uint16_t khz;
    const char *raw; // space-separated microseconds
};

// Immediate-shutter raw codes (from the well-known multi-camera IR references).
static const CamIR CAMERAS[] = {
    {"Nikon", 38, "2000 27830 390 1580 410 3580 400"},
    {"Canon", 33, "480 7330 480"},
    {"Pentax", 38, "13000 3000 1000 1000 1000 1000 1000 1000 1000 1000 1000 1000 1000 1000 1000"},
};
static const int CAM_COUNT = sizeof(CAMERAS) / sizeof(CAMERAS[0]);

void deuceIntervalometer() {
    uint16_t khz = 38;
    String raw = "";
    String camName = "";

    // 1) pick the camera / code
    {
        std::vector<Option> o;
        for (int i = 0; i < CAM_COUNT; i++) {
            int idx = i;
            o.push_back({CAMERAS[i].name, [idx, &khz, &raw, &camName]() {
                             khz = CAMERAS[idx].khz;
                             raw = CAMERAS[idx].raw;
                             camName = CAMERAS[idx].name;
                         }});
        }
        o.push_back({"Custom raw", [&khz, &raw, &camName]() {
                         long f = num_keyboard("38", 3, "Carrier kHz:").toInt();
                         khz = (f >= 30 && f <= 60) ? (uint16_t)f : 38;
                         raw = keyboard("", 220, "Raw us (space-sep):");
                         raw.trim();
                         camName = "Custom";
                     }});
        o.push_back({"Back", []() {}});
        int sel = loopOptions(o, MENU_TYPE_SUBMENU, "Camera");
        if (sel == -1) return;
    }
    if (raw.length() == 0) return; // backed out / no code entered

    // 2) interval + shot count
    long intervalS = num_keyboard("5", 7, "Interval sec:").toInt();
    if (intervalS < 1) intervalS = 1;
    long shots = num_keyboard("0", 7, "Shots (0=inf):").toInt();
    if (shots < 0) shots = 0;

    // 3) run
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(2);
    tft.drawCentreString("INTERVALOMETER", tftWidth / 2, 8, 1);
    tft.setTextSize(1);
    tft.drawCentreString(camName + "  every " + String(intervalS) + "s", tftWidth / 2, 32, 1);
    tft.setTextColor(bruceConfig.secColor, bruceConfig.bgColor);
    tft.drawCentreString("tap / BACK: stop", tftWidth / 2, tftHeight - 14, 1);

    long done = 0;
    unsigned long intervalMs = (unsigned long)intervalS * 1000UL;
    unsigned long nextFire = millis(); // first shot immediately
    long lastRemain = -1, lastDone = -1;
    touchPoint.Clear();
    delay(250);

    while (true) {
        check(SelPress);
        if (EscPress) {
            EscPress = false;
            break;
        }
        if (touchPoint.pressed) {
            touchPoint.Clear();
            break;
        }

        unsigned long now = millis();
        if ((long)(now - nextFire) >= 0 && (shots == 0 || done < shots)) {
            sendRawCommand(khz, raw, true); // fire the shutter
            done++;
            nextFire = now + intervalMs;
            lastDone = -1; // force redraw
        }

        if (shots > 0 && done >= shots) {
            tft.fillRect(0, tftHeight / 2 - 30, tftWidth, 60, bruceConfig.bgColor);
            tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
            tft.setTextSize(3);
            tft.drawCentreString("DONE", tftWidth / 2, tftHeight / 2 - 12, 1);
            tft.setTextSize(1);
            while (true) {
                check(SelPress);
                if (EscPress) {
                    EscPress = false;
                    return;
                }
                if (touchPoint.pressed) {
                    touchPoint.Clear();
                    return;
                }
                delay(30);
            }
        }

        int remain = ((long)(nextFire - now) > 0) ? (int)((nextFire - now) / 1000) : 0;
        if (done != lastDone || remain != lastRemain) {
            lastDone = done;
            lastRemain = remain;
            tft.fillRect(0, tftHeight / 2 - 30, tftWidth, 60, bruceConfig.bgColor);
            tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
            tft.setTextSize(3);
            String shotStr = String(done) + (shots > 0 ? ("/" + String(shots)) : "");
            tft.drawCentreString(shotStr, tftWidth / 2, tftHeight / 2 - 24, 1);
            tft.setTextSize(2);
            tft.drawCentreString("next " + String(remain) + "s", tftWidth / 2, tftHeight / 2 + 8, 1);
            tft.setTextSize(1);
        }
        delay(40);
    }
}
