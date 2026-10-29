// THE DEUCE v1.1 -- MMC5983MA magnetometer driver (see .h).

#include "deuce_mmc5983.h"

#include <Arduino.h>

#include "core/bus_HAL.h" // acquireI2CBus / releaseI2CBus (TwoWire)

#define MMC_ADDR 0x30
#define MMC_REG_XOUT0 0x00
#define MMC_REG_STATUS 0x08
#define MMC_REG_CTRL0 0x09
#define MMC_REG_CTRL1 0x0A
#define MMC_REG_PRODID 0x2F
#define MMC_PRODID 0x30

// Control0 bits
#define MMC_TM_M 0x01  // take magnetic measurement
#define MMC_SET 0x08   // SET coil pulse
#define MMC_RESET 0x10 // RESET coil pulse
// Control1 bits
#define MMC_SW_RST 0x80 // software reset
// Status
#define MMC_MEAS_M_DONE 0x01

// 18-bit output: 2^17 counts == null field; 16384 LSB per Gauss.
#define MMC_NULL 131072.0f
#define MMC_LSB_PER_G 16384.0f

static bool g_present = false;

static bool mmcWrite(TwoWire *w, uint8_t reg, uint8_t val) {
    w->beginTransmission(MMC_ADDR);
    w->write(reg);
    w->write(val);
    return w->endTransmission() == 0;
}

static bool mmcRead(TwoWire *w, uint8_t reg, uint8_t *buf, uint8_t n) {
    w->beginTransmission(MMC_ADDR);
    w->write(reg);
    if (w->endTransmission(false) != 0) return false; // repeated start
    uint8_t got = w->requestFrom((uint8_t)MMC_ADDR, n, (uint8_t) true);
    if (got < n) return false;
    for (uint8_t i = 0; i < n; i++) buf[i] = w->read();
    return true;
}

bool mmcBegin() {
    TwoWire *w = acquireI2CBus();
    g_present = false;
    if (w) {
        uint8_t id = 0;
        if (mmcRead(w, MMC_REG_PRODID, &id, 1) && id == MMC_PRODID) {
            mmcWrite(w, MMC_REG_CTRL1, MMC_SW_RST); // soft reset
            delay(12);                              // ~10ms boot
            mmcWrite(w, MMC_REG_CTRL0, MMC_SET);    // null the bridge
            delay(2);
            g_present = true;
        }
    }
    releaseI2CBus();
    return g_present;
}

bool mmcPresent() { return g_present; }

void mmcSetReset() {
    if (!g_present) return;
    TwoWire *w = acquireI2CBus();
    if (w) {
        mmcWrite(w, MMC_REG_CTRL0, MMC_SET);
        delay(1);
        mmcWrite(w, MMC_REG_CTRL0, MMC_RESET);
        delay(1);
    }
    releaseI2CBus();
}

bool mmcReadGauss(float &gx, float &gy, float &gz) {
    if (!g_present) return false;
    TwoWire *w = acquireI2CBus();
    bool ok = false;
    if (w && mmcWrite(w, MMC_REG_CTRL0, MMC_TM_M)) {
        for (int i = 0; i < 20; i++) { // poll done (~<10ms)
            uint8_t st = 0;
            if (mmcRead(w, MMC_REG_STATUS, &st, 1) && (st & MMC_MEAS_M_DONE)) {
                ok = true;
                break;
            }
            delay(1);
        }
    }
    if (ok) {
        uint8_t b[7];
        if (mmcRead(w, MMC_REG_XOUT0, b, 7)) {
            uint32_t x = ((uint32_t)b[0] << 10) | ((uint32_t)b[1] << 2) | ((b[6] >> 6) & 0x03);
            uint32_t y = ((uint32_t)b[2] << 10) | ((uint32_t)b[3] << 2) | ((b[6] >> 4) & 0x03);
            uint32_t z = ((uint32_t)b[4] << 10) | ((uint32_t)b[5] << 2) | ((b[6] >> 2) & 0x03);
            gx = ((float)x - MMC_NULL) / MMC_LSB_PER_G;
            gy = ((float)y - MMC_NULL) / MMC_LSB_PER_G;
            gz = ((float)z - MMC_NULL) / MMC_LSB_PER_G;
        } else {
            ok = false;
        }
    }
    releaseI2CBus();
    return ok;
}
