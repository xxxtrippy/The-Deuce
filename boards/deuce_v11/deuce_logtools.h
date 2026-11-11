#pragma once
// THE DEUCE v1.1 -- GPS logging tools.
//   Site Survey : geotagged temp/humidity/heading/field rows -> /deuce/survey.csv  (Others)
//   RF Coverage : geotagged CC1101/WiFi/NRF RSSI rows -> /deuce/rfmap.csv + live trail (GPS)
void deuceSiteSurvey();
void deuceRFMap();
