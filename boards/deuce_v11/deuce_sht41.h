#pragma once
// THE DEUCE v1.1 -- SHT41 temperature + humidity sensor (I2C addr 0x44).
//
// Shares the I2C bus (SDA 0 / SCL 2) with the PN532 / DS3231 / MMC5983 via
// bus_HAL's acquireI2CBus()/releaseI2CBus(). Auto-detected at boot.
//
// Provides: the live reading used by the status-bar temperature, and the
// Others -> SHT41 dashboard (live T/RH, dew point, abs. humidity, heat index,
// dual scrolling graphs, and a T-vs-RH comfort map).

bool sht41Begin();                        // probe + init; true if it answered
bool sht41Present();
bool sht41Read(float &tempC, float &rh);  // high-precision single-shot
bool sht41CachedTempC(float &tempC);      // throttled temp for the status bar
void deuceSht41();                         // Others -> SHT41 dashboard
