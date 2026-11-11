#pragma once
// THE DEUCE v1.1 -- 433 MHz weather-sensor receiver.
//   Decodes the very common "Nexus" OOK family (Nexus / Sencor / Digoo /
//   many no-name 433.92 MHz outdoor temp+humidity sensors): 36-bit PWM-gap
//   frames carrying id, channel, battery, temperature and humidity.
//   Two matching consecutive frames are required before a reading is shown
//   (the protocol has no CRC). Temperature displayed in both C and F.
// RF menu.
void deuceWeather433();
