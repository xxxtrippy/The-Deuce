#pragma once
// THE DEUCE v1.1 -- Faraday cage / RF-shielding tester.
//
// Uses the CC1101 to read live sub-GHz RSSI. You pick a frequency where a signal
// source is present (a key fob held down, a 315/433 remote, ambient ISM), capture
// a REFERENCE level outside the bag/cage, then place the device inside: the screen
// shows the live level, the attenuation vs the reference (dB), and a shielded /
// partial / leaky verdict with peak-hold. CC1101 required. In the RF menu.
void deuceFaradayTest();
