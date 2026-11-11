#pragma once
// THE DEUCE v1.1 -- clock synchronisation.
//   GPS Time Sync : set the system clock (and DS3231) from satellite UTC time.
//   NTP Time Sync : set the system clock (and DS3231) from an internet time
//                   server over WiFi.
// Both set the ESP32 system clock in UTC (so TOTP / Sun Finder / Trip Computer
// get a correct epoch) and also write the battery-backed DS3231 so the time
// survives a power cycle.
// GPS Time Sync lives in the GPS menu; NTP Time Sync lives in the WiFi menu.
void deuceGpsTimeSync();
void deuceNtpTimeSync();
