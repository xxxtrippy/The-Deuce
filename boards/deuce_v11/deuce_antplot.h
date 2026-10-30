#pragma once
// THE DEUCE v1.1 -- unified multi-antenna pattern plotter.
//
// Measures and overlays radiation patterns for all three antennas on the build:
//   - CC1101  sub-GHz  (selectable: 315 / 433.92 / 868 / 915 MHz)
//   - ESP32   2.4 GHz  (WiFi RSSI from fixed AP scan)
//   - NRF24   2.4 GHz  (channel-0 RSSI / carrier detect)
//
// Each antenna can be enabled / disabled independently. You can run a single
// antenna, any two, or all three back-to-back. Results are displayed as:
//   - Live polar plot per antenna (updates while rotating)
//   - Colour-coded overlay of all completed sweeps on one plot
//   - Stats panel: peak dBm, bearing, F/B ratio, HPBW, estimated gain
//
// Bearing is auto-filled by the MMC5983MA magnetometer when fitted; otherwise
// 12-step manual capture (every 30 deg).  Boresight (0 deg) = screen right.
//
// After all sweeps finish a summary screen shows all three patterns overlaid.
// In the RF menu.
void deuceAntennaPlot();
