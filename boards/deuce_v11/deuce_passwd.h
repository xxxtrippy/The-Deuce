#pragma once
// THE DEUCE v1.1 -- password generator + strength meter.
//   Generate: strong random passwords (length + character sets adjustable),
//             seeded from the ESP32 hardware RNG + ADC noise.
//   Check   : type a password and get an entropy estimate + verdict.
// Others menu.
void deucePassGen();
