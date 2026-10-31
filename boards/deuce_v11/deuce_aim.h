#pragma once
// THE DEUCE v1.1 -- live AIM screen.
//
// Real-time signal-strength bars for all three antennas at once, so you can
// bend/aim a whip and watch its band climb toward a target:
//   - CC1101  sub-GHz : true RSSI (dBm) from the CC1101 register
//   - ESP32   2.4 GHz : RSSI (dBm) of the loudest AP (async WiFi scan)
//   - NRF24   2.4 GHz : carrier-detect duty % (proxy -- not true dBm)
//
// Each band has a live bar + a peak-hold tick (SEL resets peaks). Heading is
// shown when the MMC5983 magnetometer is fitted, so you know which way the
// unit is pointing while you aim. Pick the CC1101 band on entry. In the RF menu.
void deuceAimScreen();
