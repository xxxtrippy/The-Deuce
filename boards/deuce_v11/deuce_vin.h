#pragma once
// THE DEUCE v1.1 -- VIN decoder. Offline: validate (check digit), WMI -> manufacturer,
// country/region, model year. Online (NHTSA vPIC, when WiFi is up): full make/model/
// engine/plant. Added to the Others menu.
void deuceVinDecoder();
