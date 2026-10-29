#pragma once
// THE DEUCE v1.1 -- Lock combo calculator (offline).
//
// Master-style 40-position dial padlock recovery. Given the third number read off
// the lock (the "sticking point" method narrows the third wheel to one value), the
// first and second numbers follow from a fixed modular relationship, cutting 64,000
// combinations down to ~80-100 candidates. Added to the Others menu.
void deuceLockCalc();
