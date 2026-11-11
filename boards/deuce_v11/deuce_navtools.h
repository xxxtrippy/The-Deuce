#pragma once
// THE DEUCE v1.1 -- GPS navigation tools (GPS + MMC5983 + RTC + SHT41).
//   Waypoint Navigator : mark/enter a target, get distance + a compass arrow to it
//   Sun Finder         : sun azimuth/elevation + sunrise/sunset + arrow to the sun
//   Trip Computer      : speed / max / heading / trip distance / clock / cabin temp
// All live under the GPS menu.
void deuceWaypointNav();
void deuceSunFinder();
void deuceTripComputer();
