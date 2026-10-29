// THE DEUCE v1.1 -- offline resistor color-code calculator (see .h).

#include "deuce_resistor.h"
#include "deuce_resistor_core.h"

#include <Arduino.h>
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/scrollableTextArea.h"

static const char *CN[13] = {"Black", "Brown", "Red",    "Orange", "Yellow", "Green", "Blue",
                             "Violet", "Grey", "White", "Gold",   "Silver", "None"};

static const char *roleName(int band, int n) {
    int nd = (n == 4) ? 2 : 3;
    if (band < nd) return "Digit";
    if (band == nd) return "Multiplier";
    if (band == nd + 1) return "Tolerance";
    return "Temp Coef";
}

void deuceResistorCalc() {
    // 1) band count
    int n = 4;
    options = {
        {"4 band", [&]() { n = 4; }},
        {"5 band", [&]() { n = 5; }},
        {"6 band", [&]() { n = 6; }},
    };
    loopOptions(options, MENU_TYPE_SUBMENU, "Resistor: bands");

    int nd = (n == 4) ? 2 : 3;
    int tolIdx = nd + 1; // index of the tolerance band

    // 2) pick each band's color (in reading order)
    int seq[6];
    for (int b = 0; b < n; b++) {
        int pick = -2;
        options.clear();
        for (int c = 0; c < 12; c++) {
            int cc = c;
            options.push_back({CN[c], [&pick, cc]() { pick = cc; }});
        }
        if (b == tolIdx) options.push_back({"None (20%)", [&pick]() { pick = 12; }});
        options.push_back({"< Cancel", [&pick]() { pick = -1; }});

        String title = "Band " + String(b + 1) + "/" + String(n) + ": " + roleName(b, n);
        loopOptions(options, MENU_TYPE_SUBMENU, title.c_str());
        if (pick < 0) return; // cancelled / esc
        seq[b] = pick;
    }

    // 3) decode
    ResResult r = res_eval(seq, n);

    String out = "RESISTOR  (" + String(n) + " band)\n";
    out += "You entered:\n  ";
    for (int b = 0; b < n; b++) {
        out += CN[seq[b]];
        if (b < n - 1) out += " - ";
    }
    out += "\n\n";

    if (!r.valid) {
        out += "** Invalid combination **\n\n";
        out += "These colors don't decode as a\n";
        out += "resistor in either direction.\n";
        out += "Check the bands and try again.\n";
    } else {
        if (r.backwards) {
            out += "** Looks read BACKWARDS **\n";
            out += "Decoding from the other end:\n\n";
        }
        char val[32];
        res_format(r.ohms, val, sizeof(val));
        out += "Value: " + String(val) + "\n";
        if (r.haveTol) {
            String tp = String(r.tolPct, (r.tolPct < 1.0) ? 2 : (r.tolPct == (int)r.tolPct ? 0 : 1));
            out += "Tolerance: +/- " + tp + " %\n";
            char lo[32], hi[32];
            res_format(r.ohms * (1.0 - r.tolPct / 100.0), lo, sizeof(lo));
            res_format(r.ohms * (1.0 + r.tolPct / 100.0), hi, sizeof(hi));
            out += "Range: " + String(lo) + "  to  " + String(hi) + "\n";
        }
        if (r.haveTempco) out += "Temp coef: " + String(r.tempco) + " ppm/K\n";
        if (r.backwards) {
            out += "\nReading order (corrected):\n  ";
            for (int b = n - 1; b >= 0; b--) {
                out += CN[seq[b]];
                if (b > 0) out += " - ";
            }
            out += "\n";
        }
    }

    ScrollableTextArea area("Resistor Calc");
    area.fromString(out);
    area.show();
}
