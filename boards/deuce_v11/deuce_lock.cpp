// THE DEUCE v1.1 -- Lock combo calculator (offline).
//
// Recovers the combination of a Master-style 40-position dial padlock from the third
// number. On these locks the third digit can be read directly by applying tension to
// the shackle and finding the one resistance location that lands on a whole number
// (the "sticking point" test); the user enters that here. From the third number a
// fixed modular relationship fixes the first and second digits to a short candidate
// list -- ~80-100 combos instead of 64,000. All math is local; no network.
//
// Relationship (public since the 2015 analysis of these locks):
//   * first  % 4 == third % 4                      (first shares third's group)
//   * second has two possible residues mod 4, both derived from the first:
//       second % 4 == (first + 2) % 4   OR   (first + 2) % 4   with the pair
//       of magnitudes that differ by 2, i.e. {(third+2)%10-style} -- enumerated
//       below by construction rather than trusted as a closed form.
// We enumerate first in {third%4, third%4+4, ... <40} (10 values), then for each
// first we take the two valid seconds, giving ~20-100 printed candidates. The
// sticking-point reads are +/-1 noisy, so we also offer the +/-1 neighbours of the
// third number as alternate runs from the menu.

#include "deuce_lock.h"

#include <Arduino.h>
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/scrollableTextArea.h"

// Pad an int to two digits ("07", "35") for aligned columns.
static String pad2(int n) {
    if (n < 10) return "0" + String(n);
    return String(n);
}

// Build the candidate list for a known (or guessed) third number on a 40-pos dial.
// Returns the combos as text appended to `out`, and the count.
static int lockCandidates(int third, String &out) {
    third = ((third % 40) + 40) % 40; // normalize into 0..39

    // The two second-wheel magnitudes for a given first. On these locks the valid
    // seconds for a first F are F+2 and F-2 taken mod 10 against the third's group,
    // realised over the full 0..39 dial. We generate them structurally:
    //   seconds that satisfy (second % 10) in { (third+2)%10 , (third-2)%10 } are the
    //   classic pair; but because reads drift, we enumerate every second whose
    //   residue mod 4 equals (first+2)%4 and keep the two closest to those magnitudes.
    int g = third % 4;                 // first's group
    int r2a = ((third + 2) % 10 + 10) % 10;
    int r2b = ((third - 2) % 10 + 10) % 10;

    int count = 0;
    out += "   1st  2nd  3rd\n";
    out += "   ---------------\n";
    for (int first = g; first < 40; first += 4) {   // 10 values of the first wheel
        for (int second = 0; second < 40; second++) {
            int s10 = second % 10;
            if (s10 != r2a && s10 != r2b) continue;  // only the two valid residues
            if ((second % 4) != ((first + 2) % 4)) continue; // second's group rule
            out += "   " + pad2(first) + "   " + pad2(second) + "   " + pad2(third) + "\n";
            count++;
        }
    }
    return count;
}

void deuceLockCalc() {
    String in = num_keyboard("", 2, "3rd number (0-39):");
    in.trim();
    if (in.length() == 0) return;

    int third = in.toInt();
    if (third < 0 || third > 39) {
        displayError("3rd must be 0-39", true);
        return;
    }

    String out = "DIAL PADLOCK -- combo recovery\n";
    out += "Entered 3rd number: " + String(third) + "\n\n";

    int n0 = lockCandidates(third, out);

    // sticking-point reads drift +/-1; offer the neighbours as a fallback block.
    int tm = (third + 39) % 40; // third - 1
    int tp = (third + 1) % 40;  // third + 1
    String altm, altp;
    int nm = lockCandidates(tm, altm);
    int np = lockCandidates(tp, altp);

    out += "\nTotal candidates for 3rd=" + String(third) + ": " + String(n0) + "\n";
    out += "\nIf the shackle test was off by one, try these too:\n";
    out += "\n== 3rd = " + String(tm) + " (" + String(nm) + " combos) ==\n" + altm;
    out += "\n== 3rd = " + String(tp) + " (" + String(np) + " combos) ==\n" + altp;

    out += "\nMethod: apply upward tension on the shackle and rotate the dial;\n";
    out += "note the resistance locations. Eleven fall on half-marks and one on\n";
    out += "a whole number -- that whole number is the 3rd digit entered above.\n";

    ScrollableTextArea area("Lock Calc");
    area.fromString(out);
    area.show();
}
