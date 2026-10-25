#include "core/powerSave.h"
#include "core/utils.h"
#include <interface.h>
#include <LittleFS.h>
#include "deuce_boot.h"     // 1-bit THE DEUCE logo for the custom boot animation

// =====================================================================
//  THE DEUCE — touch fix: read the XPT2046 over the DISPLAY'S hardware SPI
//  (tft.getSPIinstance()) instead of bit-banging the shared pins. The SPI
//  peripheral owns 18/23/19, so this actually clocks the chip. TOUCH_CS (21)
//  is driven for each touch transaction; TFT_CS (17) is held high so the
//  display ignores it. Calibration (2-corner) saved to /touchcal; first-time
//  calibration only runs when booted on BATTERY (skipped on USB).
// =====================================================================

static SPISettings TOUCH_SPI(2000000, MSBFIRST, SPI_MODE0);
#define Z_THRESHOLD 400    // pressure needed to count as a press
#define Z_RELEASE   220    // pressure must fall below this to re-arm (hysteresis)
#define CAL_INSET   20     // marks sit this many px in from the corners -- far enough to
                           // touch accurately; the mapping still extrapolates/clamps to the
                           // true panel edges, so full corner-to-corner coverage is kept.

// 3-point AFFINE touch calibration (handles rotation, axis swap, mirror, scale,
// offset -- unlike a 2-point diagonal cal, which is blind to an axis swap):
//   screen_x = calA*rawX + calB*rawY + calC
//   screen_y = calD*rawX + calE*rawY + calF
static float calA = 0, calB = 0, calC = 0, calD = 0, calE = 0, calF = 0;
static bool  calLoaded = false;

// Solve a*x + b*y + c = s for three (x,y)->s samples (Cramer's rule).
static bool solveAffine(float x1, float y1, float x2, float y2, float x3, float y3,
                        float s1, float s2, float s3, float &a, float &b, float &c) {
    float D = x1 * (y2 - y3) - y1 * (x2 - x3) + (x2 * y3 - x3 * y2);
    if (D > -1e-6f && D < 1e-6f) return false;   // degenerate (collinear) points
    a = (s1 * (y2 - y3) - y1 * (s2 - s3) + (s2 * y3 - s3 * y2)) / D;
    b = (x1 * (s2 - s3) - s1 * (x2 - x3) + (x2 * s3 - x3 * s2)) / D;
    c = (x1 * (y2 * s3 - y3 * s2) - y1 * (x2 * s3 - x3 * s2) + s1 * (x2 * y3 - x3 * y2)) / D;
    return true;
}

static uint16_t xptCmd(uint8_t cmd) {
    SPIClass &spi = tft.getSPIinstance();
    spi.beginTransaction(TOUCH_SPI);
    digitalWrite(TOUCH_CS, LOW);
    spi.transfer(cmd);
    uint16_t hi = spi.transfer(0x00);
    uint16_t lo = spi.transfer(0x00);
    digitalWrite(TOUCH_CS, HIGH);
    spi.endTransaction();
    return (uint16_t)(((hi << 8) | lo) >> 3) & 0x0FFF;
}

// One sample. Returns true (and averaged raw x/y) if pressure over threshold.
static bool xptRawSample(uint16_t &rx, uint16_t &ry) {
    digitalWrite(TFT_CS, HIGH);                 // keep display deselected
    uint16_t z1 = xptCmd(0xB0);
    uint16_t z2 = xptCmd(0xC0);
    int z = (int)z1 + (4095 - (int)z2);
    if (z < Z_THRESHOLD) return false;
    uint32_t sx = 0, sy = 0;
    const int N = 4;
    for (int i = 0; i < N; i++) { sx += xptCmd(0x90); sy += xptCmd(0xD0); }
    rx = sx / N; ry = sy / N;
    return true;
}

static void loadCal() {
    File f = LittleFS.open("/touchcal6", "r");   // v6: 3-point affine calibration
    if (!f) return;
    float v[6];
    for (int i = 0; i < 6; i++) v[i] = f.readStringUntil('\n').toFloat();
    f.close();
    if (v[0] != 0 || v[1] != 0) {   // sanity: X transform isn't all-zero
        calA = v[0]; calB = v[1]; calC = v[2]; calD = v[3]; calE = v[4]; calF = v[5];
        calLoaded = true;
    }
}

static void saveCal() {
    File f = LittleFS.open("/touchcal6", "w");   // v6: 3-point affine calibration
    if (f) {
        f.printf("%.6f\n%.6f\n%.6f\n%.6f\n%.6f\n%.6f\n", calA, calB, calC, calD, calE, calF);
        f.close();
    }
}

static void waitPress(uint16_t &rx, uint16_t &ry) {
    uint16_t x, y;
    while (xptRawSample(x, y)) delay(10);        // wait release
    while (!xptRawSample(x, y)) delay(10);        // wait press
    delay(60);
    xptRawSample(rx, ry);
    while (xptRawSample(x, y)) delay(10);        // wait release
}

static void drawTarget(int x, int y) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    // static, centered label for both touches
    tft.drawCentreString("Touch Calibration", tft.width() / 2, tft.height() / 2 - 4, 1);
    tft.fillRect(x - 8, y - 1, 17, 3, TFT_GREEN);
    tft.fillRect(x - 1, y - 8, 3, 17, TFT_GREEN);
}

// Global so the Config menu can trigger a recalibration.
void calibrateTouchNow() {
    int fw = tft.width(), fh = tft.height();     // true physical panel size
    // three non-collinear targets: top-left, top-right, bottom-left
    float X1 = CAL_INSET,          Y1 = CAL_INSET;
    float X2 = fw - 1 - CAL_INSET, Y2 = CAL_INSET;
    float X3 = CAL_INSET,          Y3 = fh - 1 - CAL_INSET;
    uint16_t r1x, r1y, r2x, r2y, r3x, r3y;
    drawTarget((int)X1, (int)Y1); waitPress(r1x, r1y);
    drawTarget((int)X2, (int)Y2); waitPress(r2x, r2y);
    drawTarget((int)X3, (int)Y3); waitPress(r3x, r3y);
    // solve the affine map raw(x,y) -> screen x and -> screen y
    bool ok1 = solveAffine(r1x, r1y, r2x, r2y, r3x, r3y, X1, X2, X3, calA, calB, calC);
    bool ok2 = solveAffine(r1x, r1y, r2x, r2y, r3x, r3y, Y1, Y2, Y3, calD, calE, calF);
    if (ok1 && ok2) { saveCal(); calLoaded = true; }
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawCentreString(ok1 && ok2 ? "Touch calibrated!" : "Cal failed - retry",
                         fw / 2, fh / 2 - 4, 1);
    delay(1000);
}

extern SPIClass sdcardSPI;   // Bruce's shared SD/radio SPI bus (defined in main.cpp)

void _setup_gpio() {
    bruceConfig.colorInverted = 0;
    pinMode(TFT_BL, OUTPUT);

    // --- RADIO BUS FIX ------------------------------------------------------
    // CC1101 and NRF24 borrow Bruce's `sdcardSPI` instance (they share MOSI 13
    // with the SD card, so acquireSPIBus() hands them &sdcardSPI). But because
    // this board defines USE_TFT_eSPI_TOUCH, setupSdCard() takes its "SD on the
    // default SPI bus" branch and NEVER calls sdcardSPI.begin(). The radios then
    // inherit an un-started bus and both read as "not found". Start the HSPI bus
    // here (before Bruce's first radio init) so both radios have a live clock.
    pinMode(CC1101_SS_PIN, OUTPUT); digitalWrite(CC1101_SS_PIN, HIGH);  // 22 idle high
    pinMode(NRF24_SS_PIN,  OUTPUT); digitalWrite(NRF24_SS_PIN,  HIGH);  // 27 idle high
    pinMode(NRF24_CE_PIN,  OUTPUT); digitalWrite(NRF24_CE_PIN,  LOW);   // 33 CE low
    pinMode(SDCARD_CS,     OUTPUT); digitalWrite(SDCARD_CS,     HIGH);  // 15 idle high
    sdcardSPI.begin(SDCARD_SCK, SDCARD_MISO, SDCARD_MOSI, SDCARD_CS);   // 14, 26, 13, 15
}

void _post_setup_gpio() {
    pinMode(TOUCH_CS, OUTPUT); digitalWrite(TOUCH_CS, HIGH);
    pinMode(TFT_CS, OUTPUT);
    // Nothing about theme/orientation is forced here anymore. Bruce loads your
    // saved rotation (begin_tft) and saved colors (config) before this runs, and
    // the Settings menus persist your choices -- so orientation AND UI colors now
    // stick to whatever you pick.
    loadCal();
    // Touch calibration is NOT run here -- it's handled inside deuceBootAnim() so
    // the device goes straight into the boot animation with no black-screen poll.
    // Calibration runs after the animation if there's no saved cal, or the moment
    // you hold BOOT (GPIO0) during the animation.
}

void _setBrightness(uint8_t b) { digitalWrite(TFT_BL, b > 5 ? HIGH : LOW); }

// Edge-triggered: fires ONE event per finger press. After a press is consumed,
// the finger must lift (pressure < Z_RELEASE) before another press can register.
// This is what stops the "spam -> crash": without it, a held finger injected a
// fresh key event every cycle and drove the menus into a runaway loop.
void InputHandler(void) {
    static unsigned long tm = 0;
    static bool armed = true;          // ready to accept a fresh press?
    if (millis() - tm < 15 && !LongPress) return;
    tm = millis();

    digitalWrite(TFT_CS, HIGH);        // deselect display for the read
    uint16_t z1 = xptCmd(0xB0);
    uint16_t z2 = xptCmd(0xC0);
    int z = (int)z1 + (4095 - (int)z2);

    if (z < Z_RELEASE) {               // finger clearly up -> re-arm, report release
        armed = true;
        touchPoint.pressed = false;
        return;
    }
    if (z < Z_THRESHOLD) return;       // in the dead-band: ignore (debounce)
    if (!armed) return;                // same press still held -> don't re-fire
    armed = false;                     // consume this press edge

    uint32_t sxr = 0, syr = 0;
    for (int i = 0; i < 4; i++) { sxr += xptCmd(0x90); syr += xptCmd(0xD0); }
    uint16_t rx = sxr / 4, ry = syr / 4;
    // 3-point affine map: handles rotation/axis-swap/mirror/scale/offset.
    int fw = tft.width(), fh = tft.height();
    int sx = (int)(calA * (float)rx + calB * (float)ry + calC);
    int sy = (int)(calD * (float)rx + calE * (float)ry + calF);
    if (sx < 0) sx = 0; else if (sx >= fw) sx = fw - 1;
    if (sy < 0) sy = 0; else if (sy >= fh) sy = fh - 1;

    if (!wakeUpScreen()) {
        AnyKeyPress = true;
        touchPoint.x = sx;
        touchPoint.y = sy;
        touchPoint.pressed = true;
        touchHeatMap(touchPoint);
    }
}

// 565 color blend: alpha 0..255 of fg over bg. Replaces TFT_eSPI::alphaBlend,
// which is private in Bruce's tft wrapper and can't be called from here.
static uint16_t blend565(uint8_t a, uint16_t fg, uint16_t bg) {
    uint16_t fr = (fg >> 11) & 0x1F, fgn = (fg >> 5) & 0x3F, fb = fg & 0x1F;
    uint16_t br = (bg >> 11) & 0x1F, bgn = (bg >> 5) & 0x3F, bb = bg & 0x1F;
    uint16_t r = (fr * a + br * (255 - a)) / 255;
    uint16_t g = (fgn * a + bgn * (255 - a)) / 255;
    uint16_t b = (fb * a + bb * (255 - a)) / 255;
    return (uint16_t)((r << 11) | (g << 5) | b);
}

// =====================================================================
//  Custom boot animation: sparse code field -> each 8x8 cell flips (flash)
//  to its own chunk of the THE DEUCE logo -> full logo. Drawn entirely with
//  tft primitives from the embedded 1-bit bitmap (no GIF/filesystem).
//  Colors come from the ACTIVE THEME: priColor = logo, secColor = code rain
//  + flash, bgColor = background. main.cpp calls this (weak override) BEFORE
//  its boot_screen(), so no "Bruce"/"PREDATORY FIRMWARE" text shows.
// =====================================================================
bool deuceBootAnim() {
    const int CW = 8;
    const int STRIDE = DEUCE_W / 8;              // bytes per bitmap row (40)
    int cols = DEUCE_W / CW;                     // 40
    int rows = DEUCE_H / CW;                     // 30
    int ox = (tftWidth  - DEUCE_W) / 2;          // center on screen (clips if smaller)
    int oy = (tftHeight - DEUCE_H) / 2;

    uint16_t pri = bruceConfig.priColor;         // logo color (theme primary)
    uint16_t sec = bruceConfig.secColor;         // code rain + flash (theme secondary)
    uint16_t bg  = bruceConfig.bgColor;          // background (theme background)

    pinMode(0, INPUT_PULLUP);                    // BOOT (GPIO0): hold during anim -> touch cal
    bool bootHeld = false;

    const int FIELD = 7, REVEAL = 22;
    static uint8_t rstep[40 * 30];
    randomSeed(micros());
    for (int i = 0; i < rows * cols; i++) rstep[i] = FIELD + (uint8_t)random(0, REVEAL);

    static const char CH[] = "01ABCDEF#$%&*+<>=/?Z";
    const int NCH = sizeof(CH) - 1;

    tft.fillScreen(bg);
    tft.setTextSize(1);

    int total = FIELD + REVEAL + 2;
    for (int s = 0; s < total; s++) {
        // sparse flickering code field over not-yet-revealed cells
        if (s < FIELD + REVEAL) {
            int draws = (s < FIELD) ? 240 : 110;
            for (int k = 0; k < draws; k++) {
                int cx = random(0, cols), cy = random(0, rows);
                if (s >= rstep[cy * cols + cx]) continue;   // in a revealed region
                int x = ox + cx * CW, y = oy + cy * CW;
                tft.fillRect(x, y, CW, CW, bg);
                if (random(0, 100) < 45) {                  // ~45% on -> sparse
                    // vary brightness of the secondary color toward the bg
                    uint16_t cc = blend565((uint8_t)(90 + random(0, 166)), sec, bg);
                    tft.setTextColor(cc, bg);
                    tft.setCursor(x + 1, y);
                    tft.print(CH[random(0, NCH)]);
                }
            }
        }
        // flash (secondary) at a cell's reveal step, then its logo chunk one step later
        for (int cy = 0; cy < rows; cy++) {
            for (int cx = 0; cx < cols; cx++) {
                int rs = rstep[cy * cols + cx];
                int x = ox + cx * CW, y = oy + cy * CW;
                if (rs == s) {
                    tft.fillRect(x, y, CW, CW, sec);                // flash in secondary
                } else if (rs == s - 1) {
                    uint8_t cell[8];
                    int base = (cy * CW) * STRIDE + cx;
                    for (int py = 0; py < 8; py++) cell[py] = DEUCE_BITS[base + py * STRIDE];
                    tft.fillRect(x, y, CW, CW, bg);
                    tft.drawXBitmap(x, y, cell, 8, 8, pri, bg);     // logo in primary
                }
            }
        }
        if (digitalRead(0) == LOW) { bootHeld = true; break; }   // BOOT held -> jump to cal
        delay(70);
    }

    bool willCal = bootHeld || !calLoaded;   // BOOT held during anim, or no saved cal
    if (!bootHeld) {                                                   // animation finished normally
        tft.drawXBitmap(ox, oy, DEUCE_BITS, DEUCE_W, DEUCE_H, pri, bg);   // crisp final logo
        if (!willCal) delay(1700);                                    // hold ~1s (skip if calibrating)
    }
    tft.fillScreen(bg);
    if (willCal) calibrateTouchNow();   // calibrate, then boot continues into Bruce
    return true;
}

void powerOff() {}
void checkReboot() {}
