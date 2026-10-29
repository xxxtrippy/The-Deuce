#pragma once
// THE DEUCE v1.1 -- MMC5983MA 3-axis magnetometer driver (I2C, addr 0x30).
//
// Minimal register driver for the ghost-radar magnetic (EMF) channel. Rides the
// shared I2C bus via bus_HAL's acquireI2CBus()/releaseI2CBus(), so it coexists
// with the PN532 / DS3231 / Si4713 already on SDA 0 / SCL 2. Auto-detected by its
// product-ID register, so a build without the sensor fitted simply skips it.
//
// Wiring (bare chip): SDA 0, SCL 2, 3V3, GND, and CS tied HIGH (I2C mode). A
// breakout only needs SDA/SCL/3V3/GND (disable its onboard pull-ups).
#include <stdint.h>

bool mmcBegin();   // probe + init; true if an MMC5983MA answered at 0x30
bool mmcPresent(); // last known presence
// Read the field on each axis in Gauss (Earth ~ 0.5 G). Returns false on I2C error.
bool mmcReadGauss(float &gx, float &gy, float &gz);
void mmcSetReset(); // degauss the AMR bridge (periodic offset refresh)
