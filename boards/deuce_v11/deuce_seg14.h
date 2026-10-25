#pragma once
#include <Arduino.h>
// Big segmented-LCD text helpers, shared from boards/deuce_v11/interface.cpp.
// Draw a seg14 string LEFT-aligned at (xLeft,yTop); returns its pixel width.
int deuceSeg14At(const String &s, int xLeft, int yTop, int cw, int chh, int gap, int th,
                 uint16_t col, uint16_t bg);
// Width a seg14 string would occupy at the given cell size (no drawing).
int deuceSeg14Width(const String &s, int cw, int gap);
