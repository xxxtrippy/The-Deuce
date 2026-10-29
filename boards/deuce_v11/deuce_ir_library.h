#pragma once
// THE DEUCE v1.1 -- IR appliance library. Browses a structured IR code database on SD
// by Category -> Brand -> Function and transmits the selected code through Bruce's IR
// senders (NEC/NECext/RC5/RC6/Samsung/SIRC/Kaseikyo/raw). DB lives at /ir_library.txt;
// a small starter DB is written on first run if none exists. Added to the IR menu.
void deuceIrLibrary();
