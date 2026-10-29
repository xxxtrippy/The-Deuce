#pragma once
// THE DEUCE v1.1 -- offline resistor color-code calculator.
//
// Pick 4/5/6 bands, then choose each band's color from a menu (in reading order).
// Decodes resistance, tolerance and (6-band) temperature coefficient, and detects
// when the colors were entered from the wrong end (reversed), showing the
// corrected value. Pure decode logic in deuce_resistor_core.h (host-verified).
// In the Others menu. Fully offline.
void deuceResistorCalc();
