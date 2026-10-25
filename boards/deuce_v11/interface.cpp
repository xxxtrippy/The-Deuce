#include "core/powerSave.h"
#include "core/utils.h"
#include <interface.h>
#include <math.h> // sqrtf for the 14-seg break insets

// THE DEUCE v1.1: text-only Claude chat (boards/deuce_v11/claude_chat.cpp). Opened by
// the "Claude" tile, which replaces the old Scripts tile on page 3.
void deuceClaudeLaunch();
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

// Title shown centered in the main-menu header.
#define DEUCE_TITLE "The Deuce"

// Physical BACK button on GPIO36 (input-only pin; external 10k pull-up to 3V3,
// button to GND -> idle HIGH, pressed LOW). Mapped to Bruce's EscPress so it backs
// out of submenus and the on-screen keyboard. On the root grid it does nothing
// (there's nothing to back out to), and the grid swallows it so it can't leak.
#define BACK_BTN_PIN     36
#define BACK_BTN_ACTIVE  LOW
#define BACK_DEBOUNCE_MS 30   // reading must hold steady this long before it counts

// --- Live touch export for the main-menu grid --------------------------------
// On USE_TFT_eSPI_TOUCH boards the input task does NOT call InputHandler(); Bruce
// polls touch by calling InputHandler() from check() (globals.h), which the menu
// loops invoke constantly. InputHandler keeps `touchPoint` edge-triggered -- one
// event per press -- for the keyboard and list menus. The main-menu grid also
// needs the *continuous* finger position (to tell a swipe from a tap), so
// InputHandler mirrors the live, mapped position into these. The grid calls
// InputHandler() itself each loop and reads these -- it never uses check().
static volatile bool gTouchLive = false;        // true while a finger is down
static volatile int  gTouchX = 0, gTouchY = 0;  // latest mapped screen position

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
    pinMode(BACK_BTN_PIN, INPUT);   // GPIO36 is input-only; relies on the external 10k pull-up
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

    // --- physical BACK button (GPIO36) ---------------------------------------
    // Debounced + edge-triggered: fires one EscPress per press. The raw reading
    // must hold steady for BACK_DEBOUNCE_MS before it's accepted (any bounce
    // restarts the timer), so contact chatter can't double-fire. Checked before
    // the touch read so it works even while a finger is held on the screen.
    static bool backArmed = true;
    static int backLastRaw = -1;       // last raw reading (-1 = uninitialized)
    static unsigned long backTs = 0;   // when the raw reading last changed
    int backRaw = digitalRead(BACK_BTN_PIN);
    if (backRaw != backLastRaw) {      // reading changed -> (re)start the settle timer
        backLastRaw = backRaw;
        backTs = millis();
    }
    if (millis() - backTs >= BACK_DEBOUNCE_MS) {   // reading has been stable long enough
        if (backRaw != BACK_BTN_ACTIVE) {
            backArmed = true;          // released (stable) -> ready for next press
        } else if (backArmed) {
            backArmed = false;         // consume this debounced press edge
            if (!wakeUpScreen()) { AnyKeyPress = true; EscPress = true; }
        }
    }

    digitalWrite(TFT_CS, HIGH);        // deselect display for the read
    uint16_t z1 = xptCmd(0xB0);
    uint16_t z2 = xptCmd(0xC0);
    int z = (int)z1 + (4095 - (int)z2);

    if (z < Z_RELEASE) {               // finger clearly up -> re-arm, report release
        armed = true;
        touchPoint.pressed = false;
        gTouchLive = false;            // grid: finger lifted
        return;
    }
    if (z < Z_THRESHOLD) return;       // in the dead-band: ignore (debounce)

    // Finger is down. Sample + map the position EVERY cycle so the grid can
    // follow a drag/swipe. (The edge-triggered touchPoint below is unchanged --
    // it still fires exactly once per press for the keyboard and list menus.)
    uint32_t sxr = 0, syr = 0;
    for (int i = 0; i < 4; i++) { sxr += xptCmd(0x90); syr += xptCmd(0xD0); }
    uint16_t rx = sxr / 4, ry = syr / 4;
    // 3-point affine map: handles rotation/axis-swap/mirror/scale/offset.
    int fw = tft.width(), fh = tft.height();
    int sx = (int)(calA * (float)rx + calB * (float)ry + calC);
    int sy = (int)(calD * (float)rx + calE * (float)ry + calF);
    if (sx < 0) sx = 0; else if (sx >= fw) sx = fw - 1;
    if (sy < 0) sy = 0; else if (sy >= fh) sy = fh - 1;
    gTouchX = sx; gTouchY = sy; gTouchLive = true;   // grid: live position

    if (!armed) return;                // same press still held -> don't re-fire tap
    armed = false;                     // consume this press edge

    if (!wakeUpScreen()) {
        AnyKeyPress = true;
        touchPoint.x = sx;
        touchPoint.y = sy;
        touchPoint.pressed = true;
        touchHeatMap(touchPoint);
    }
}

// =====================================================================
//  THE DEUCE main menu: a paged 2x2 touch grid (replaces Bruce's carousel).
//  - Header: clock (left) | "The Deuce v1.0" (center) | battery (right).
//  - 4 tiles per page, laid out TL/TR/BL/BR; horizontal SWIPE changes page
//    (wraps around); a TAP opens that app's existing submenu.
//  - No PREV/SEL/NEXT footer -- the grid uses the whole screen below the header.
//  - Colors are the live theme: priColor = text/header, secColor = page dots,
//    bgColor = background. Icons are a later pass; v1 is text, like the mockup.
//  Hooked in from MainMenu::begin() via a weak override (see main_menu.cpp).
// =====================================================================
struct DeuceTile {
    String label;
    std::function<void()> op;
};

// ======================= Matrix rain (menu background) =======================
// Full-screen rain behind the menu; color always follows the theme SECONDARY color.
// Labels/header sit in reserved, bordered boxes that the rain flows around (so the
// text stays crisp with no per-frame redraw). Settings persist to LittleFS
// /deucerain as "<on> <speed> <bright>". Adjusted via Config -> UI -> Matrix Rain.
static uint16_t blend565(uint8_t a, uint16_t fg, uint16_t bg); // defined later in file

static bool    rainOn = false;    // enabled?
static uint8_t rainSpeed = 1;     // 0 slow / 1 med / 2 fast
static uint8_t rainBright = 1;    // 0..2 -> three DIM head levels (never overpowers the UI)
static const uint8_t  RAIN_HEAD_A[3] = {105, 140, 180};  // head alphas (of 255): Low/Med/High
static const uint16_t RAIN_STEP_MS[3] = {120, 80, 45};  // frame interval per speed

#define RAIN_CELL 8
#define RAIN_MAXCOL 40
#define RAIN_MAXROW 30
static int rainCols = 0, rainRows = 0;
static int16_t rainHeadRow[RAIN_MAXCOL];
static uint8_t rainColDiv[RAIN_MAXCOL]; // per-column pacing divisor (1..3) for variety
static uint8_t rainColCnt[RAIN_MAXCOL];
static uint8_t rainBlock[RAIN_MAXCOL * RAIN_MAXROW]; // 1 = reserved (text) -> no rain
static uint8_t rainGlyph[RAIN_MAXCOL * RAIN_MAXROW]; // stored glyph index per cell (for fade)
static unsigned long rainStepTs = 0;
static const char RAIN_CHARS[] = "0123456789ABCDEFGHJKLMNPRSTUVWXYZ#$%&*+=<>?";
static const int RAIN_NCH = sizeof(RAIN_CHARS) - 1;

static void deuceRainLoad() {
    File f = LittleFS.open("/deucerain", "r");
    if (!f) return;
    rainOn = f.readStringUntil(' ').toInt() != 0;
    int sp = f.readStringUntil(' ').toInt();
    int br = f.readStringUntil('\n').toInt();
    f.close();
    rainSpeed = (sp < 0) ? 0 : (sp > 2 ? 2 : sp);
    rainBright = (br < 0) ? 0 : (br > 2 ? 2 : br);
}
static void deuceRainSave() {
    File f = LittleFS.open("/deucerain", "w");
    if (f) {
        f.printf("%d %d %d\n", rainOn ? 1 : 0, rainSpeed, rainBright);
        f.close();
    }
}
static void rainBegin() { // size the field + clear reserves/glyphs (call before marking)
    rainCols = tft.width() / RAIN_CELL;
    if (rainCols > RAIN_MAXCOL) rainCols = RAIN_MAXCOL;
    rainRows = tft.height() / RAIN_CELL;
    if (rainRows > RAIN_MAXROW) rainRows = RAIN_MAXROW;
    for (int i = 0; i < rainCols * rainRows; i++) {
        rainBlock[i] = 0;
        rainGlyph[i] = 0;
    }
}
static void rainReserve(int x, int y, int w, int h) { // mark a pixel rect as text (no rain)
    int c0 = x / RAIN_CELL, c1 = (x + w - 1) / RAIN_CELL;
    int r0 = y / RAIN_CELL, r1 = (y + h - 1) / RAIN_CELL;
    for (int c = c0; c <= c1; c++)
        for (int r = r0; r <= r1; r++)
            if (c >= 0 && c < rainCols && r >= 0 && r < rainRows) rainBlock[c * rainRows + r] = 1;
}
static void rainInit() { // stagger drop heads (some above the screen -> fade-in from top)
    for (int c = 0; c < rainCols; c++) {
        rainHeadRow[c] = -(int16_t)(random(0, rainRows + 1));
        rainColDiv[c] = 1 + random(0, 3);
        rainColCnt[c] = 0;
    }
    rainStepTs = millis();
}
static inline bool rainBlocked(int c, int r) {
    if (c < 0 || c >= rainCols || r < 0 || r >= rainRows) return true;
    return rainBlock[c * rainRows + r] != 0;
}
static void rainPaint(int c, int r, uint8_t gi, uint16_t color) {
    if (rainBlocked(c, r)) return;
    char s[2] = {RAIN_CHARS[gi], 0};
    tft.setTextSize(1);
    tft.setTextColor(color, bruceConfig.bgColor);
    tft.drawString(s, c * RAIN_CELL + 1, r * RAIN_CELL, 1);
}
// Vertical brightness ramp: dim at the very top (so the header/clock/icons stay
// legible) rising to full strength by row FADE_ROWS and below. Returns a 0..255 scale.
static uint8_t rainRowFactor(int r) {
    const int FADE_ROWS = 7;
    if (r < 0) return 0;
    if (r >= FADE_ROWS) return 255;
    return (uint8_t)(35 + (220 * r) / FADE_ROWS); // row0 ~dim -> row7 full
}
static void rainStep() { // advance one animation frame
    uint16_t sec = bruceConfig.secColor, bg = bruceConfig.bgColor;
    uint8_t aHead = RAIN_HEAD_A[rainBright];
    uint8_t aT1 = aHead * 3 / 5;
    uint8_t aT2 = aHead * 3 / 10;
    for (int c = 0; c < rainCols; c++) {
        if (++rainColCnt[c] < rainColDiv[c]) continue;
        rainColCnt[c] = 0;
        int h = rainHeadRow[c];
        if (h - 1 >= 0 && h - 1 < rainRows)
            rainPaint(c, h - 1, rainGlyph[c * rainRows + (h - 1)],
                      blend565((uint8_t)((int)aT1 * rainRowFactor(h - 1) / 255), sec, bg));
        if (h - 2 >= 0 && h - 2 < rainRows)
            rainPaint(c, h - 2, rainGlyph[c * rainRows + (h - 2)],
                      blend565((uint8_t)((int)aT2 * rainRowFactor(h - 2) / 255), sec, bg));
        if (h - 3 >= 0 && h - 3 < rainRows && !rainBlocked(c, h - 3))
            tft.fillRect(c * RAIN_CELL, (h - 3) * RAIN_CELL, RAIN_CELL, RAIN_CELL, bg);
        if (h >= 0 && h < rainRows) {
            uint8_t gi = random(0, RAIN_NCH);
            rainGlyph[c * rainRows + h] = gi;
            rainPaint(c, h, gi, blend565((uint8_t)((int)aHead * rainRowFactor(h) / 255), sec, bg));
        }
        if (++rainHeadRow[c] > rainRows + 3) {
            rainHeadRow[c] = -(int16_t)(random(0, rainRows / 2 + 1));
            rainColDiv[c] = 1 + random(0, 3);
        }
    }
}

// Config submenu, reached from Config -> UI -> Matrix Rain (see ConfigMenu.cpp).
void deuceRainMenu() {
    while (true) {
        std::vector<Option> o = {
            {String("Rain: ") + (rainOn ? "ON" : "OFF"),
             []() { rainOn = !rainOn; deuceRainSave(); }                                      },
            {String("Speed: ") + (rainSpeed == 0 ? "Slow" : (rainSpeed == 1 ? "Med" : "Fast")),
             []() { rainSpeed = (rainSpeed + 1) % 3; deuceRainSave(); }                       },
            {String("Brightness: ") + (rainBright == 0 ? "Low" : (rainBright == 1 ? "Med" : "High")),
             []() { rainBright = (rainBright + 1) % 3; deuceRainSave(); }                      },
            {"Back", []() {}                                                                   },
        };
        int sel = loopOptions(o, MENU_TYPE_SUBMENU, "Matrix Rain");
        if (sel == -1 || sel == (int)o.size() - 1) return;
    }
}

// Fill `out` with the current time, dropping a leading zero on the hour
// ("02:16:45" -> "2:16:45"); a two-digit hour naturally pushes the rest right.
static void deuceClockStr(char *out, size_t n) {
#if defined(HAS_RTC)
    updateTimeStr(_rtc.getTimeStruct());
#else
    updateTimeStr(rtc.getTimeStruct());
#endif
    const char *t = timeStr;
    if (t[0] == '0' && t[1] >= '0' && t[1] <= '9' && t[2] == ':') t++; // strip leading hour zero
    strncpy(out, t, n - 1);
    out[n - 1] = 0;
}

// Left edge of the header status zone = just right of the centered title.
static int deuceStatusLeft() { // call with text size already irrelevant; sets its own
    tft.setTextSize(2);
    return tft.width() / 2 + tft.textWidth(DEUCE_TITLE) / 2 + 8;
}

// Flash switch on GPIO2 (=SCL) grounded? The I2C bus idles SCL high via its pullups,
// so if several quick samples are ALL low, the switch is pulling it to GND. (Reading
// the pin level doesn't disturb I2C -- we never reconfigure the pad.) Rare false +
// only if an I2C transaction spans the whole ~5ms window.
static bool deuceFlashSwitchOn() {
    for (int i = 0; i < 16; i++) {
        if (digitalRead(SCL) == HIGH) return false;
        delayMicroseconds(300);
    }
    return true;
}

// 16x16 lightning bolt (flash-mode indicator), drawn in the same priColor as the icons.
static void drawBoltSmall(int x, int y) {
    tft.fillRect(x, y, 16, 16, bruceConfig.bgColor);
    uint16_t c = bruceConfig.priColor;
    tft.fillTriangle(x + 9, y + 1, x + 3, y + 9, x + 9, y + 9, c);   // upper half
    tft.fillTriangle(x + 7, y + 7, x + 13, y + 7, x + 6, y + 15, c); // lower half
}

// Right side of the header: battery PERCENTAGE at the far right (no icon), then the
// active connection icons (WiFi/SD/BLE/GPS/WebUI, priority order) placed to its left,
// as many as actually fit WITHOUT reaching the centered title. No rotation.
static void deuceDrawStatusRight() {
    const int W = tft.width();
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor;
    int statusLeft = deuceStatusLeft();
    tft.fillRect(statusLeft, 3, W - statusLeft, 20, bg); // clear only the zone past the title

    // percentage, right-aligned at the far edge
    int b = getBattery();
    char pct[8];
    snprintf(pct, sizeof(pct), "%d%%", b < 0 ? 0 : b);
    int pw = (int)strlen(pct) * 6; // size-1 glyph = 6px wide
    int pctX = W - 4 - pw;
    tft.setTextSize(1);
    tft.setTextColor(pri, bg);
    tft.setCursor(pctX, 9);
    tft.print(pct);

    // active connection icons in priority order (flash-switch bolt first, if on)
    typedef void (*IconFn)(int, int);
    IconFn fns[6];
    int nAct = 0;
    if (deuceFlashSwitchOn()) fns[nAct++] = drawBoltSmall;
    if (wifiConnected) fns[nAct++] = drawWifiSmall;
    if (sdcardMounted) fns[nAct++] = drawSdSmall;
    if (BLEConnected) fns[nAct++] = drawBLESmall;
    if (gpsConnected) fns[nAct++] = drawGpsSmall;
    if (isWebUIActive) fns[nAct++] = drawWebUISmall;

    int iconRight = pctX - 4;                        // right edge of the icon area
    int maxFit = (iconRight - statusLeft) / 18;      // 16px icon + 2px gap
    if (maxFit < 0) maxFit = 0;
    int shown = nAct < maxFit ? nAct : maxFit;
    for (int i = 0; i < shown; i++) fns[i](iconRight - 16 - i * 18, 5); // packed right-to-left
}

static void deuceDrawHeader() {
    const int W = tft.width();
    uint16_t pri = bruceConfig.priColor;
    uint16_t bg = bruceConfig.bgColor;
    tft.fillRect(0, 0, W, 27, bg);
    // clock (left) -- honors whatever 12/24h format you set in Settings
    if (clock_set) {
        char cs[16];
        deuceClockStr(cs, sizeof(cs));
        tft.setTextSize(1);
        tft.setTextColor(pri, bg);
        tft.setCursor(8, 10);
        tft.print(cs);
    }
    // right-side status (percentage + connection icons), drawn before the title; it
    // lives in the zone past the centered title, so it never overlaps "The Deuce".
    deuceDrawStatusRight();
    // title -- centered on the true screen midpoint, drawn last (on top).
    tft.setTextSize(2);
    int tw = tft.textWidth(DEUCE_TITLE);
    tft.setTextColor(pri, bg);
    tft.drawCentreString(DEUCE_TITLE, W / 2, 5, 1);
    // header underline
    tft.fillRect(0, 25, W, 2, pri);
    tft.setTextSize(1);

    // Rain on: the header text stays crisp (rain flows AROUND it), no black borders.
    if (rainOn) {
        if (clock_set) rainReserve(6, 8, 70, 12);        // clock
        rainReserve(W / 2 - tw / 2 - 3, 3, tw + 6, 20);  // title
        int sL = W / 2 + tw / 2 + 8;                     // status zone (right of title)
        rainReserve(sL, 3, W - sL, 21);
        rainReserve(0, 24, W, 4);                        // underline strip
    }
}

// Repaint ONLY the clock (left side) -- used for the once-a-second tick in the menu,
// so the seconds stay accurate without flickering the title or battery.
static void deuceDrawClock() {
    if (!clock_set) return;
    char cs[16];
    deuceClockStr(cs, sizeof(cs));
    tft.fillRect(8, 9, 66, 10, bruceConfig.bgColor); // clear old time (within the reserved box)
    tft.setTextSize(1);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setCursor(8, 10);
    tft.print(cs);
}

// Un-reserve + clear a rect so rain resumes there (used on a page swipe to release the
// previous page's label boxes before the new ones are reserved).
static void rainUnreserveClear(int x, int y, int w, int h) {
    tft.fillRect(x, y, w, h, bruceConfig.bgColor);
    int c0 = x / RAIN_CELL, c1 = (x + w - 1) / RAIN_CELL;
    int r0 = y / RAIN_CELL, r1 = (y + h - 1) / RAIN_CELL;
    for (int c = c0; c <= c1; c++)
        for (int r = r0; r <= r1; r++)
            if (c >= 0 && c < rainCols && r >= 0 && r < rainRows) rainBlock[c * rainRows + r] = 0;
}

// --------------------------------------------------------------------------
// 14-segment ("starburst") font -- used for the menu labels so they read like a
// retro LCD. Each glyph is a 14-bit mask; every lit segment is drawn as one wide
// line between its two endpoints in a cell cw wide x chh tall. Bit order:
//   0:a  1:b  2:c  3:d  4:e  5:f  6:g1 7:g2  8:h  9:i  10:j  11:k  12:l  13:m
//   a=top, b=top-right, c=bot-right, d=bottom, e=bot-left, f=top-left,
//   g1=mid-left, g2=mid-right, h=TL diag, i=top-centre vert, j=TR diag,
//   k=BL diag, l=bot-centre vert, m=BR diag. (Masks generated + eyeballed via
//   the seg14 preview, so the device matches that image glyph-for-glyph.)
static const uint16_t SEG14_AZ[26] = {
    0x00F7, 0x128F, 0x0039, 0x120F, 0x00F9, 0x00F1, 0x00BD, 0x00F6, 0x1209, 0x001E,
    0x2470, 0x0038, 0x0536, 0x2136, 0x003F, 0x00F3, 0x203F, 0x20F3, 0x00ED, 0x1201,
    0x003E, 0x0C30, 0x2836, 0x2D00, 0x1500, 0x0C09};
static const uint16_t SEG14_09[10] = {
    0x0C3F, 0x0406, 0x00DB, 0x008F, 0x00E6, 0x00ED, 0x00FD, 0x0007, 0x00FF, 0x00EF};

static uint16_t seg14Mask(char c) {
    if (c >= 'a' && c <= 'z') c -= 32;
    if (c >= 'A' && c <= 'Z') return SEG14_AZ[c - 'A'];
    if (c >= '0' && c <= '9') return SEG14_09[c - '0'];
    return 0; // space / unknown -> blank cell
}

// Inter-segment "break": each segment is shortened by this many px at BOTH ends so a
// dark notch opens at every junction, like a real segmented LCD. 1.5 reads best at this
// glyph size (verified against the on-device-scale mock).
#define SEG14_BREAK 1.5f

// Draw one lit segment as a wide line. The break notch is applied ONLY at interior
// junctions: an endpoint sitting on the cell's top (yt) or bottom (yb) edge is never
// pulled inward, so every glyph keeps the exact same full height and the tops/bottoms
// line up. Notches therefore appear only at the middle cross and mid-side junctions.
static void seg14Seg(
    float x0, float y0, float x1, float y1, float yt, float yb, float wd, uint16_t col, uint16_t bg
) {
    float dx = x1 - x0, dy = y1 - y0;
    float L = sqrtf(dx * dx + dy * dy);
    if (L > 0.0f) {
        float b = SEG14_BREAK;
        if (L <= 2.0f * b + 0.5f) b = (L > 1.0f) ? (L - 1.0f) / 2.0f : 0.0f; // don't vanish short segs
        float ux = dx / L, uy = dy / L;
        // On the cell's top/bottom edge, inset HORIZONTALLY only -> the corner gap opens
        // but the segment still reaches the top/bottom line (uniform height). Interior
        // junctions get the full inset, so the middle notches stay too.
        if (fabsf(y0 - yt) < 0.5f || fabsf(y0 - yb) < 0.5f) {
            x0 += ux * b;
        } else {
            x0 += ux * b;
            y0 += uy * b;
        }
        if (fabsf(y1 - yt) < 0.5f || fabsf(y1 - yb) < 0.5f) {
            x1 -= ux * b;
        } else {
            x1 -= ux * b;
            y1 -= uy * b;
        }
    }
    tft.drawWideLine(x0, y0, x1, y1, wd, col, bg);
}

// Draw one glyph, top-left at (x,y), cell cw x chh, line thickness th.
static void seg14Char(char c, int x, int y, int cw, int chh, int th, uint16_t col, uint16_t bg) {
    uint16_t m = seg14Mask(c);
    if (!m) return;
    float xl = x, xm = x + cw / 2.0f, xr = x + cw;
    float yt = y, ym = y + chh / 2.0f, yb = y + chh;
    float wd = (float)th;
#define SEG14L(bit, x0, y0, x1, y1)                                                                 \
    if (m & (1u << (bit))) seg14Seg((x0), (y0), (x1), (y1), yt, yb, wd, col, bg)
    SEG14L(0, xl, yt, xr, yt);  // a  top
    SEG14L(1, xr, yt, xr, ym);  // b  top-right
    SEG14L(2, xr, ym, xr, yb);  // c  bot-right
    SEG14L(3, xl, yb, xr, yb);  // d  bottom
    SEG14L(4, xl, ym, xl, yb);  // e  bot-left
    SEG14L(5, xl, yt, xl, ym);  // f  top-left
    SEG14L(6, xl, ym, xm, ym);  // g1 mid-left
    SEG14L(7, xm, ym, xr, ym);  // g2 mid-right
    SEG14L(8, xl, yt, xm, ym);  // h  TL diag
    SEG14L(9, xm, yt, xm, ym);  // i  top-centre
    SEG14L(10, xr, yt, xm, ym); // j  TR diag
    SEG14L(11, xl, yb, xm, ym); // k  BL diag
    SEG14L(12, xm, ym, xm, yb); // l  bot-centre
    SEG14L(13, xr, yb, xm, ym); // m  BR diag
#undef SEG14L
}

static int seg14Advance(char c, int cw, int gap) {
    if (c == ' ') return (cw * 3) / 5 + gap; // spaces are narrower
    return cw + gap;
}
static int seg14Width(const String &s, int cw, int gap) {
    int w = 0;
    for (size_t i = 0; i < s.length(); i++) w += seg14Advance(s[i], cw, gap);
    if (w > 0) w -= gap; // drop the trailing gap
    return w;
}
// Draw a string centred on cx with its top at y. Shrinks the cell to `avail` px wide
// so long labels (e.g. BLUETOOTH) still fit inside their tile.
static void seg14CentreFit(
    const String &s, int cx, int cyCenter, int avail, uint16_t col, uint16_t bg, int &outX, int &outY,
    int &outW, int &outH
) {
    int cw = 14, chh = 22, gap = 6, th = 3; // default size; gap 6 gives even, comfortable
                                            // spacing even between dense glyphs (O, N, G...)
    int lw = seg14Width(s, cw, gap);
    if (lw > avail && lw > 0) { // scale down to fit the tile
        float sc = (float)avail / lw;
        cw = (int)(cw * sc);
        if (cw < 6) cw = 6;
        chh = (int)(chh * sc);
        if (chh < 12) chh = 12;
        gap = (int)(gap * sc);
        if (gap < 2) gap = 2;
        th = (chh >= 18) ? 3 : 2;
        lw = seg14Width(s, cw, gap);
    }
    int y = cyCenter - chh / 2;
    int x = cx - lw / 2;
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c != ' ') seg14Char(c, x, y, cw, chh, th, col, bg);
        x += seg14Advance(c, cw, gap);
    }
    outX = cx - lw / 2 - 2;
    outY = y - 2;
    outW = lw + 4;
    outH = chh + 4; // bounding box (for the rain reserve)
}

// Draw one page's labels + dots. With rain on, each label reserves a TIGHT box so the
// rain flows right up to the text but never into it -- an INVISIBLE barrier (no border
// drawn). Labels are drawn ONCE, never per frame, so this is the lightest option.
static void deuceDrawTiles(int page, const std::vector<DeuceTile> &tiles, int pages) {
    const int W = tft.width();
    const int H = tft.height();
    const int TOP = 28;
    uint16_t pri = bruceConfig.priColor, sec = bruceConfig.secColor, bg = bruceConfig.bgColor;
    int n = (int)tiles.size();
    int halfW = W / 2, cellH = (H - TOP) / 2;
    int avail = halfW - 20; // 10px margin each side so long labels (BLUETOOTH) aren't edge-to-edge
    for (int slot = 0; slot < 4; slot++) {
        int ti = page * 4 + slot;
        if (ti >= n) continue;
        int cx = (slot & 1) * halfW + halfW / 2;
        int cy = TOP + (slot >> 1) * cellH + cellH / 2;
        int bx, by, bw, bh;
        seg14CentreFit(tiles[ti].label, cx, cy, avail, pri, bg, bx, by, bw, bh);
        if (rainOn) rainReserve(bx, by, bw, bh);
    }
    if (pages > 1) {
        int dotY = H - 8, spacing = 14;
        int startX = W / 2 - ((pages - 1) * spacing) / 2;
        if (rainOn) rainReserve(startX - 6, dotY - 5, (pages - 1) * spacing + 12, 11);
        for (int p = 0; p < pages; p++) {
            int dx = startX + p * spacing;
            if (p == page) tft.fillCircle(dx, dotY, 3, pri);
            else tft.drawCircle(dx, dotY, 3, sec);
        }
    }
    tft.setTextSize(1);
}

static void deuceDrawGrid(int page, const std::vector<DeuceTile> &tiles, int pages) {
    const int W = tft.width();
    const int H = tft.height();
    const int TOP = 28;
    tft.fillRect(0, TOP, W, H - TOP, bruceConfig.bgColor);
    deuceDrawTiles(page, tiles, pages);
}

// Page swipe: release the old label boxes back to rain, clear them, draw the new page.
// Rain state (drop positions) is untouched, so it keeps flowing across the swipe.
static void deuceSwipeRedraw(int page, const std::vector<DeuceTile> &tiles, int pages) {
    const int W = tft.width();
    const int H = tft.height();
    const int TOP = 28;
    uint16_t bg = bruceConfig.bgColor;
    int halfW = W / 2, cellH = (H - TOP) / 2;
    int clrW = halfW - 6; // cover the full label width (incl. BLUETOOTH)
    for (int slot = 0; slot < 4; slot++) {
        int cx = (slot & 1) * halfW + halfW / 2;
        int cy = TOP + (slot >> 1) * cellH + cellH / 2;
        if (rainOn) rainUnreserveClear(cx - clrW / 2, cy - 15, clrW, 30);
        else tft.fillRect(cx - clrW / 2, cy - 15, clrW, 30, bg);
    }
    if (pages > 1) {
        int dotY = H - 8, spacing = 14;
        int startX = W / 2 - ((pages - 1) * spacing) / 2;
        if (rainOn) rainUnreserveClear(startX - 6, dotY - 5, (pages - 1) * spacing + 12, 11);
        else tft.fillRect(startX - 6, dotY - 5, (pages - 1) * spacing + 12, 11, bg);
    }
    deuceDrawTiles(page, tiles, pages);
}

// Strong override of the weak stub in main_menu.cpp. Returns true once a tile is
// tapped (its submenu has run) so loop() re-enters begin() and we repaint; keeps
// looping internally while you just swipe between pages.
bool deuceMainMenuGrid(std::vector<Option> &opts, int &idx) {
    (void)idx;
    static bool rainLoaded = false; // load persisted rain settings once (first menu entry)
    if (!rainLoaded) {
        deuceRainLoad();
        rainLoaded = true;
    }
    // Curated tile order (your mockup). Reuses Bruce's show/hide filtering: an
    // item appears only if it's present & enabled in `opts`.
    std::vector<DeuceTile> tiles;
    std::vector<String> placed;
    auto add = [&](const char *name, const char *disp) {
        for (auto &o : opts)
            if (o.enabled && o.label == name) {
                tiles.push_back({String(disp), o.operation});
                placed.push_back(String(name));
                return;
            }
    };
    //  internal name       tile label
    add("WiFi", "WiFi");                 // page 1: TL  TR
    add("BLE", "BT");                    //         BL  BR
    add("NRF24", "NRF");
    add("RF", "RF");
    add("RFID", "RFID");                 // page 2
    add("IR", "IR");
    add("GPS", "GPS");
    add("Others", "Others");             // swapped with Clock
    add("Files", "Files");               // page 3
    // Claude AI takes the old Scripts slot (page 3, TR). The JS interpreter now lives
    // inside the Files menu, so we mark it "placed" to keep it off the auto-append page.
    tiles.push_back({String("Claude"), []() { deuceClaudeLaunch(); }});
    placed.push_back(String("JS Interpreter"));
    add("Clock", "Clock");               // swapped with Others
    add("Config", "Config");
    // Keep any other ENABLED item reachable (FM / LoRa / Ethernet / Connect, ...)
    // by appending it after the 12 -> it lands on an extra page. Hide it in
    // Config -> UI -> (hide apps) for a clean 3 pages.
    for (auto &o : opts) {
        if (!o.enabled) continue;
        bool have = false;
        for (auto &p : placed)
            if (p == o.label) {
                have = true;
                break;
            }
        if (!have) {
            tiles.push_back({o.label, o.operation});
            placed.push_back(o.label);
        }
    }

    int n = (int)tiles.size();
    if (n == 0) return false; // nothing enabled -> fall back to Bruce's carousel
    int pages = (n + 3) / 4;
    static int page = 0; // persists across submenu returns
    if (page >= pages) page = 0;

    const int W = tft.width();
    const int H = tft.height();
    const int TOP = 28;
    const int SWIPE_ARM = 16;              // horiz travel that COMMITS to a swipe (kills the tap)
    const int SWIPE_MIN = 40;              // net horiz travel actually needed to flip a page
    const unsigned long TAP_MAX_MS = 500;  // a tap must press+release within this; longer = hold

    bool redraw = true;               // full redraw (resets rain) -- entry / op-return
    bool pageDirty = false;           // page changed via swipe (keeps rain running)
    unsigned long clockTs = millis(); // last 1s clock + status tick
    // gesture state
    bool prevLive = gTouchLive; // if a finger is already down, wait for release first
    bool tracking = false;
    bool swipeCommitted = false; // set once the finger moves -> release will NOT tap
    int sx0 = 0, sy0 = 0, maxdx = 0, maxdy = 0;
    unsigned long t0 = 0;
    returnToMenu = false;

    while (true) {
        // CRITICAL: on USE_TFT_eSPI_TOUCH boards the input *task* never calls
        // InputHandler() -- Bruce normally polls touch through check() (see
        // globals.h), which loopOptions calls constantly. This custom loop
        // doesn't use check(), so we must poll the touch chip ourselves here, or
        // gTouch*/touchPoint never update and the screen appears dead.
        InputHandler();
        EscPress = false; // Back is a no-op at the root; swallow it so it can't
                          // leak into the next submenu you open.
        if (redraw) {
            if (rainOn) rainBegin();              // size field + clear reserves (before marking)
            deuceDrawHeader();                    // these mark their text boxes as reserved
            deuceDrawGrid(page, tiles, pages);
            if (rainOn) rainInit();               // stagger the drops (full reset)
            redraw = false;
            pageDirty = false;
            clockTs = rainStepTs = millis();
        } else {
            if (pageDirty) { // swipe: swap the page content WITHOUT resetting the rain
                deuceSwipeRedraw(page, tiles, pages);
                pageDirty = false;
            }
            unsigned long nowMs = millis();
            if (nowMs - clockTs >= 1000) { // once a second: tick clock + refresh status
                deuceDrawClock();
                deuceDrawStatusRight(); // redraw %, battery, active icons (no rotation)
                clockTs = nowMs;
            }
            if (rainOn && nowMs - rainStepTs >= RAIN_STEP_MS[rainSpeed]) {
                rainStep(); // labels sit in reserved cells, so rain never paints over them
                rainStepTs = nowMs;
            }
        }

        bool live = gTouchLive;
        int tx = gTouchX, ty = gTouchY;
        if (live && !prevLive) { // press begins
            tracking = true;
            swipeCommitted = false;
            sx0 = tx;
            sy0 = ty;
            t0 = millis();
            maxdx = 0;
            maxdy = 0;
        }
        if (live && tracking) { // record furthest travel; the instant the finger
            int dx = tx - sx0, dy = ty - sy0;          // moves horizontally past
            if (abs(dx) > abs(maxdx)) maxdx = dx;       // SWIPE_ARM we lock into a
            if (abs(dy) > abs(maxdy)) maxdy = dy;       // swipe so release can't tap.
            if (abs(maxdx) >= SWIPE_ARM) swipeCommitted = true;
        }
        if (!live && prevLive && tracking) { // release -> classify gesture
            tracking = false;
            int adx = abs(maxdx), ady = abs(maxdy);
            unsigned long dt = millis() - t0;
            if (swipeCommitted) {
                // committed to a swipe (the finger moved): flip the page only if it
                // traveled far enough horizontally; otherwise do nothing -- never a tap.
                if (pages > 1 && adx >= SWIPE_MIN && adx > ady) {
                    // finger right->left (maxdx<0) = NEXT page; left->right = PREV page.
                    // (To invert the feel, swap the two lines below.)
                    if (maxdx < 0) page = (page + 1) % pages;
                    else page = (page - 1 + pages) % pages;
                    pageDirty = true; // repaint page content, keep rain flowing (no reset)
                }
            } else if (dt <= TAP_MAX_MS && adx < SWIPE_ARM && ady < SWIPE_ARM) {
                // quick, nearly-stationary press+release = a tap. A longer hold with
                // no movement falls through here and is ignored (an "incoming swipe"
                // that never swiped), so a held finger never fires a tile.
                if (sy0 >= TOP) {
                    int col = (sx0 < W / 2) ? 0 : 1;
                    int row = ((sy0 - TOP) < (H - TOP) / 2) ? 0 : 1;
                    int ti = page * 4 + (row * 2 + col);
                    if (ti < n) {
                        tiles[ti].op(); // open that app's submenu (runs to completion)
                        return true;    // loop() re-enters begin() -> grid repaints
                    }
                }
            }
        }
        prevLive = live;
        delay(5); // keep the loop responsive; InputHandler() self-throttles to 15ms
    }
    return true;
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
