#pragma once
// THE DEUCE v1.1 -- antenna pattern plotter (live, measured).
//
// Tune the CC1101 to a frequency with a steady source present, then rotate the
// unit through 360 degrees: the tool records received signal strength (RSSI) by
// bearing and draws a polar plot of the antenna's real directivity -- the
// measured twin of a modeled radiation pattern. Boresight (0 deg) is straight out
// the RIGHT side of the unit in landscape; press to zero it to your grip. If the
// MMC5983MA magnetometer is fitted it fills bearings automatically as you rotate;
// otherwise it steps you through 12 x 30 deg captures. Shows peak dBm/bearing,
// front/back ratio and HPBW. CC1101 required. In the RF menu.
void deuceAntennaPlot();
