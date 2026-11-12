// THE DEUCE v1.1 -- hardware UART tools. See .h.

#include "deuce_uart.h"

#include <Arduino.h>
#include <WiFi.h>
#include <globals.h>

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/wifi/wifi_common.h"

#define UART_RX 32   // <- target TX
#define UART_TX 33   // -> target RX

static HardwareSerial UARTtap(1);

static const long BAUDS[] = {0 /*auto*/, 9600, 19200, 38400, 57600, 115200, 74880, 230400, 460800};
static const int NBAUDS = sizeof(BAUDS) / sizeof(BAUDS[0]);

// ── 3.3V warning gate ─────────────────────────────────────────────────────────
static bool warnGate() {
    int W = tft.width(), H = tft.height();
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextColor(TFT_ORANGE, bruceConfig.bgColor);
    tft.drawCentreString("!! 3.3V LOGIC ONLY !!", W / 2, 18, 1);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawString("RX = GPIO32  (target TX)", 10, 44);
    tft.drawString("TX = GPIO33  (target RX)", 10, 58);
    tft.drawString("GND = common ground", 10, 72);
    tft.setTextColor(bruceConfig.secColor, bruceConfig.bgColor);
    tft.drawString("ESP32 is NOT 5V tolerant.", 10, 94);
    tft.drawString("Use a level shifter/divider", 10, 108);
    tft.drawString("on 5V+ lines or you'll fry", 10, 122);
    tft.drawString("the pin.", 10, 136);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.drawCentreString("[SEL] continue   [ESC] cancel", W / 2, H - 16, 1);
    while (true) {
        if (check(SelPress)) return true;
        if (check(EscPress)) return false;
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

// ── auto-baud: try common rates, keep the one with the most printable bytes ───
static long autoBaud() {
    const long cand[] = {115200, 9600, 57600, 38400, 19200, 74880, 230400, 460800};
    long best = 115200; float bestScore = -1;
    int W = tft.width(), H = tft.height();
    for (unsigned i = 0; i < sizeof(cand) / sizeof(cand[0]); i++) {
        tft.fillRect(0, H / 2 - 8, W, 16, bruceConfig.bgColor);
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        char m[32]; snprintf(m, sizeof(m), "auto-baud: %ld...", cand[i]);
        tft.drawCentreString(m, W / 2, H / 2 - 4, 1);
        UARTtap.begin(cand[i], SERIAL_8N1, UART_RX, UART_TX);
        uint32_t t0 = millis(); int total = 0, printable = 0;
        while (millis() - t0 < 450) {
            while (UARTtap.available()) {
                int c = UARTtap.read(); total++;
                if ((c >= 32 && c < 127) || c == '\r' || c == '\n' || c == '\t') printable++;
            }
            vTaskDelay(5 / portTICK_PERIOD_MS);
        }
        UARTtap.end();
        float score = total < 4 ? -1 : (float)printable / total + total * 0.001f;
        if (score > bestScore) { bestScore = score; best = cand[i]; }
        if (check(EscPress)) break;
    }
    return best;
}

// ── UART console ──────────────────────────────────────────────────────────────
void deuceUartConsole() {
    if (!warnGate()) return;
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;

    int baudIdx = 5; // 115200
    long baud = 115200;
    int eolMode = 0; // 0 CR, 1 LF, 2 CRLF, 3 none
    const char *EOL[] = {"CR", "LF", "CRLF", "none"};
    bool hexView = false;
    std::vector<String> lines; lines.push_back("");
    int scroll = 0; // lines from bottom
    bool redraw = true, needBegin = true;

    auto startUart = [&]() {
        UARTtap.end();
        if (BAUDS[baudIdx] == 0) baud = autoBaud();
        else baud = BAUDS[baudIdx];
        UARTtap.begin(baud, SERIAL_8N1, UART_RX, UART_TX);
        redraw = true;
    };

    while (true) {
        if (check(EscPress)) break;
        if (needBegin) { startUart(); needBegin = false; }

        if (check(NextPress)) { baudIdx = (baudIdx + 1) % NBAUDS; needBegin = true; continue; }
        if (check(PrevPress)) { hexView = !hexView; redraw = true; }
        if (check(UpPress)) { if (scroll < (int)lines.size() - 1) scroll++; redraw = true; }
        if (check(DownPress)) { if (scroll > 0) scroll--; redraw = true; }
        if (check(SelPress)) {
            String msg = keyboard("", 120, "Send (EOL via PREV in idle)");
            if (msg.length()) {
                UARTtap.print(msg);
                if (eolMode == 0) UARTtap.print('\r');
                else if (eolMode == 1) UARTtap.print('\n');
                else if (eolMode == 2) UARTtap.print("\r\n");
                lines.back() += "<" + msg + ">";
            }
            redraw = true;
        }

        // drain incoming
        int got = 0;
        while (UARTtap.available() && got < 512) {
            int c = UARTtap.read(); got++;
            if (hexView) {
                char hx[4]; snprintf(hx, sizeof(hx), "%02X ", (uint8_t)c);
                lines.back() += hx;
                if (lines.back().length() > 48) lines.push_back("");
            } else {
                if (c == '\n') lines.push_back("");
                else if (c == '\r') { /* ignore */ }
                else if (c >= 32 && c < 127) lines.back() += (char)c;
                else lines.back() += '.';
                if (lines.back().length() > 52) lines.push_back("");
            }
            if (lines.size() > 300) lines.erase(lines.begin());
        }
        if (got) { scroll = 0; redraw = true; }

        if (redraw) {
            redraw = false;
            tft.fillScreen(bg);
            tft.setTextColor(pri, bg); tft.setTextSize(1);
            char hdr[40];
            if (BAUDS[baudIdx] == 0) snprintf(hdr, sizeof(hdr), "UART  auto=%ld  %s", baud, hexView ? "HEX" : "ASCII");
            else snprintf(hdr, sizeof(hdr), "UART  %ld  %s", baud, hexView ? "HEX" : "ASCII");
            tft.drawString(hdr, 4, 3);
            tft.drawFastHLine(0, 14, W, sec);

            int rows = (H - 28) / 10;
            int endLine = (int)lines.size() - scroll;
            int startLine = endLine - rows; if (startLine < 0) startLine = 0;
            int y = 17;
            for (int i = startLine; i < endLine && i < (int)lines.size(); i++) {
                tft.setTextColor(lines[i].indexOf('<') >= 0 ? TFT_GREEN : pri, bg);
                tft.drawString(lines[i], 4, y); y += 10;
            }
            tft.setTextColor(sec, bg);
            tft.drawString("[SEL] send [NEXT] baud [PREV] hex", 4, H - 21);
            tft.drawString("[UP/DN] scroll  [ESC] exit", 4, H - 11);
        }
        vTaskDelay(12 / portTICK_PERIOD_MS);
    }
    UARTtap.end();
    pinMode(UART_RX, INPUT); pinMode(UART_TX, INPUT);
}

// ── serial <-> WiFi TCP bridge ────────────────────────────────────────────────
void deuceSerialBridge() {
    if (!warnGate()) return;
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;

    // baud pick
    std::vector<Option> bo;
    for (int i = 1; i < NBAUDS; i++) { char b[12]; snprintf(b, sizeof(b), "%ld", BAUDS[i]); bo.push_back({String(b).c_str(), []() {}}); }
    int bi = loopOptions(bo, MENU_TYPE_SUBMENU, "Baud");
    if (bi < 0) return;
    long baud = BAUDS[bi + 1];
    int port = 23;

    if (WiFi.status() != WL_CONNECTED && !wifiConnectMenu()) return;

    UARTtap.end();
    UARTtap.begin(baud, SERIAL_8N1, UART_RX, UART_TX);
    WiFiServer server(port);
    server.begin();
    server.setNoDelay(true);

    String ip = WiFi.localIP().toString();
    WiFiClient client;
    uint32_t rxCount = 0, txCount = 0;
    bool redraw = true; uint32_t lastDraw = 0;

    while (true) {
        if (check(EscPress)) break;

        if (!client || !client.connected()) {
            WiFiClient incoming = server.available();
            if (incoming) { client = incoming; client.setNoDelay(true); redraw = true; }
        }

        // bridge both directions
        if (client && client.connected()) {
            while (client.available()) { UARTtap.write(client.read()); txCount++; }
            while (UARTtap.available()) { int c = UARTtap.read(); client.write((uint8_t)c); rxCount++; }
        } else {
            while (UARTtap.available()) { UARTtap.read(); } // drain while nobody listening
        }

        uint32_t now = millis();
        if (redraw || now - lastDraw > 400) {
            lastDraw = now; redraw = false;
            tft.fillScreen(bg);
            tft.setTextColor(pri, bg); tft.setTextSize(1);
            tft.drawCentreString("SERIAL <-> WIFI BRIDGE", W / 2, 4, 1);
            tft.drawFastHLine(0, 16, W, sec);
            tft.setTextColor(sec, bg);
            char l[48];
            snprintf(l, sizeof(l), "%ld baud  (RX32 TX33)", baud); tft.drawString(l, 8, 24);
            tft.setTextColor(pri, bg); tft.setTextSize(2);
            tft.drawCentreString(ip, W / 2, 42, 1);
            tft.setTextSize(1); tft.setTextColor(sec, bg);
            snprintf(l, sizeof(l), "port %d   nc %s %d", port, ip.c_str(), port);
            tft.drawCentreString(l, W / 2, 66, 1);
            bool conn = client && client.connected();
            tft.setTextColor(conn ? TFT_GREEN : TFT_ORANGE, bg);
            tft.drawCentreString(conn ? "CLIENT CONNECTED" : "waiting for client...", W / 2, 84, 1);
            tft.setTextColor(sec, bg);
            snprintf(l, sizeof(l), "->target %lu   target-> %lu", (unsigned long)txCount, (unsigned long)rxCount);
            tft.drawCentreString(l, W / 2, 102, 1);
            tft.drawString("[ESC] stop", 4, H - 11);
        }
        vTaskDelay(3 / portTICK_PERIOD_MS);
    }
    if (client) client.stop();
    server.stop();
    UARTtap.end();
    pinMode(UART_RX, INPUT); pinMode(UART_TX, INPUT);
}
