#pragma once
// THE DEUCE v1.1 -- Magnetometer suite (MMC5983MA).
//
// Others -> Magnetometer. Multiple views (SEL cycles):
//   - Compass      : rotating rose + heading + cardinal (NEXT = figure-8 cal)
//   - Field meter  : total |B| in uT, per-axis bars, scrolling graph (NEXT = reset min/max)
//   - Metal detect : deviation from a zeroed baseline + alert (NEXT = re-zero)
//   - Raw XYZ      : live gx/gy/gz + magnitude
//
// Uses the shared MMC5983 driver; no extra components required.
void deuceMagnetometer();
