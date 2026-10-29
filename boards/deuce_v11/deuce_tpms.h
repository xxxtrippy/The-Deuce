#pragma once
// THE DEUCE v1.1 -- TPMS tire-pressure sensor reader (passive, receive-only).
//
// Listens on 315 MHz (US default) or 433.92 MHz with the CC1101 and decodes the
// periodic broadcasts from vehicle tire-pressure sensors, showing sensor ID,
// pressure, temperature and battery/flags. Supports Toyota, Ford, Nissan and
// Schrader/GM(Chevy) sensors (OEM + GM aftermarket). Decode logic lives in
// deuce_tpms_core.h and is host-verified bit-for-bit against rtl_433 samples.
//
// Passive only: there is no 125 kHz LF exciter, so sensors cannot be force-woken;
// they transmit on their own schedule (while driving/rotating, on a pressure
// change, or periodically at rest). Added to the RF menu. CC1101 required.
void deuceTpmsReader();
