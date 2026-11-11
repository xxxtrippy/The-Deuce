#pragma once
// THE DEUCE v1.1 -- logging / security combo tools.
//   Weather Log : SHT41 + RTC + SD, interval temp/humidity logging + trend  (Others)
//   Tripwire    : MMC5983 magnet/door trip -> alert + /deuce/tripwire.csv    (Others)
//   NFC Log     : PN532 tag UIDs + timestamps -> /deuce/nfclog.csv           (RFID menu)
void deuceWeatherLog();
void deuceTripwire();
void deuceNfcLog();
