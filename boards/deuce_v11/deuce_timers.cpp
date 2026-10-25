// THE DEUCE v1.1 -- Stopwatch + Countdown timer, rendered in the big seg14 LCD font.
//
// Input (same idiom as the Health Checks): check(SelPress) refreshes touchPoint/EscPress
// on this board. A tap on the TOP two-thirds is start/stop (or pause/resume); a tap on
// the BOTTOM third resets. The GPIO36 Back button (EscPress) exits.

#include "deuce_timers.h"
#include "deuce_seg14.h"

#include <Arduino.h>
#include "core/display.h"
#include "core/mykeyboard.h"
#include <globals.h>

// -------- shared drawing --------
static void fmt2(char *b, int v) {
    if (v < 0) v = 0;
    if (v > 99) v = 99;
    b[0] = '0' + v / 10;
    b[1] = '0' + v % 10;
    b[2] = 0;
}

static void drawColon(int cx, int cyCenter, int chh, uint16_t col) {
    int r = 3, off = chh / 6;
    tft.fillRect(cx - r, cyCenter - off - r, 2 * r, 2 * r, col);
    tft.fillRect(cx - r, cyCenter + off - r, 2 * r, 2 * r, col);
}

// HH:MM:SS centred horizontally, vertical centre at cyCenter. Clears its own row first.
static void drawBigTime(int cyCenter, int h, int m, int s, uint16_t col, uint16_t bg) {
    const int cw = 28, chh = 46, gap = 5, th = 4, colW = 18;
    char hh[3], mm[3], ss[3];
    fmt2(hh, h);
    fmt2(mm, m);
    fmt2(ss, s);
    int gw = deuceSeg14Width("00", cw, gap);
    int total = 3 * gw + 2 * colW;
    int cx = tftWidth / 2;
    int x = cx - total / 2;
    int yTop = cyCenter - chh / 2;
    tft.fillRect(0, yTop - 3, tftWidth, chh + 6, bg); // clear the digit row
    x += deuceSeg14At(String(hh), x, yTop, cw, chh, gap, th, col, bg);
    drawColon(x + colW / 2, cyCenter, chh, col);
    x += colW;
    x += deuceSeg14At(String(mm), x, yTop, cw, chh, gap, th, col, bg);
    drawColon(x + colW / 2, cyCenter, chh, col);
    x += colW;
    deuceSeg14At(String(ss), x, yTop, cw, chh, gap, th, col, bg);
}

static void timerHeader(const char *title) {
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(2);
    tft.drawCentreString(title, tftWidth / 2, 8, 1);
    tft.setTextSize(1);
}

static void timerHint(const char *msg) {
    tft.setTextColor(bruceConfig.secColor, bruceConfig.bgColor);
    tft.setTextSize(1);
    tft.drawCentreString(msg, tftWidth / 2, tftHeight - 14, 1);
}

// ------------------------------ Stopwatch ------------------------------
void deuceStopwatch() {
    timerHeader("STOPWATCH");
    timerHint("tap: start/stop   tap bottom: reset   BACK: exit");

    unsigned long elapsed = 0, startTs = 0;
    bool running = false;
    long lastShown = -1;
    touchPoint.Clear();
    delay(250); // swallow the opening press

    while (true) {
        check(SelPress); // refreshes touchPoint + EscPress on this board
        if (EscPress) {
            EscPress = false;
            break;
        }
        unsigned long now = millis();
        if (touchPoint.pressed) {
            int ty = touchPoint.y;
            touchPoint.Clear();
            if (ty > tftHeight * 2 / 3) { // bottom -> reset
                if (running) elapsed += now - startTs;
                elapsed = 0;
                running = false;
                lastShown = -1;
            } else { // top -> start/stop
                if (running) {
                    elapsed += now - startTs;
                    running = false;
                } else {
                    startTs = now;
                    running = true;
                }
                lastShown = -1; // force a colour refresh
            }
            delay(150); // debounce
            now = millis();
        }

        unsigned long cur = elapsed + (running ? now - startTs : 0);
        long totSec = cur / 1000;
        if (totSec != lastShown) {
            lastShown = totSec;
            drawBigTime(
                tftHeight / 2, (int)(totSec / 3600), (int)((totSec / 60) % 60), (int)(totSec % 60),
                running ? bruceConfig.priColor : bruceConfig.secColor, bruceConfig.bgColor
            );
        }
        delay(15);
    }
}

// ------------------------------ Countdown ------------------------------
void deuceCountdown() {
    // Enter up to 6 digits -> HHMMSS (each field 0..99, summed into total time).
    String in = num_keyboard("", 6, "Countdown HHMMSS:");
    in.trim();
    if (in.length() == 0) return;
    while (in.length() < 6) in = "0" + in;
    if (in.length() > 6) in = in.substring(in.length() - 6);
    int H = in.substring(0, 2).toInt();
    int M = in.substring(2, 4).toInt();
    int S = in.substring(4, 6).toInt();
    long totalMs = ((long)H * 3600L + (long)M * 60L + (long)S) * 1000L;
    if (totalMs <= 0) return;

    timerHeader("COUNTDOWN");
    timerHint("tap: pause/resume   tap bottom: restart   BACK: exit");

    long remainMs = totalMs;
    bool running = true;
    bool finished = false;
    long lastShown = -1;
    unsigned long last = millis();
    touchPoint.Clear();
    delay(250);

    while (true) {
        check(SelPress);
        if (EscPress) {
            EscPress = false;
            break;
        }
        if (touchPoint.pressed) {
            int ty = touchPoint.y;
            touchPoint.Clear();
            if (ty > tftHeight * 2 / 3) { // bottom -> restart
                remainMs = totalMs;
                running = true;
                finished = false;
                lastShown = -1;
            } else { // top -> pause/resume
                if (!finished) running = !running;
                lastShown = -1;
            }
            delay(150);
        }

        unsigned long now = millis();
        long dt = (long)(now - last);
        last = now;
        if (running && remainMs > 0) {
            remainMs -= dt;
            if (remainMs < 0) remainMs = 0;
        }

        long totSec = remainMs / 1000;
        if (totSec != lastShown) {
            lastShown = totSec;
            drawBigTime(
                tftHeight / 2, (int)(totSec / 3600), (int)((totSec / 60) % 60), (int)(totSec % 60),
                running ? bruceConfig.priColor : bruceConfig.secColor, bruceConfig.bgColor
            );
        }

        if (remainMs == 0 && !finished) { // hit zero -> blink the display
            finished = true;
            running = false;
            for (int i = 0; i < 6; i++) {
                uint16_t c = (i & 1) ? bruceConfig.bgColor : bruceConfig.priColor;
                drawBigTime(tftHeight / 2, 0, 0, 0, c, bruceConfig.bgColor);
                check(SelPress);
                delay(250);
                if (EscPress) {
                    EscPress = false;
                    return;
                }
            }
            drawBigTime(tftHeight / 2, 0, 0, 0, bruceConfig.priColor, bruceConfig.bgColor);
            lastShown = 0;
        }
        delay(15);
    }
}
