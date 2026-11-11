// THE DEUCE v1.1 -- on-device "How To" walkthrough viewer. See .h.

#include "deuce_howto.h"

#include <Arduino.h>
#include <globals.h>

#include "core/display.h"
#include "deuce_seg14.h"   // deuceLiveTouch

// ===========================================================================
// Guide text.  Keep in sync with docs/WALKTHROUGH.txt.
// ===========================================================================
static const char GUIDE[] PROGMEM =
"THE DEUCE -- WALKTHROUGH\n"
"Scroll: UP / DOWN (page).  Exit: ESC/Back.\n"
"\n"
"== NAVIGATION ==\n"
"The main screen is a 2x2 touch grid over 3 pages. Tap a tile to open it; "
"swipe left/right to change page. The GPIO36 Back button leaves any screen. "
"The top header shows clock, battery %, and WiFi/SD/BLE/GPS/WebUI status. A "
"lightning bolt shows when the GPIO2 flash switch is on.\n"
"\n"
"== WIFI ==\n"
"Scan Hosts: list devices on the joined network (IP/MAC/vendor).\n"
"Sniffer: capture raw 2.4GHz WiFi frames; save to SD.\n"
"Channel Analyzer: live per-channel airtime/activity graph to find clear "
"channels.\n"
"Wifi Atks: deauth, beacon spam, and other frame attacks on a chosen AP -- "
"authorized targets only.\n"
"Evil Portal / Evil Wifi: host a captive-portal AP that serves a login page "
"and logs what is entered (research/awareness use only).\n"
"Brucegotchi: passive handshake/pwn collector, Pwnagotchi-style.\n"
"NetCut: ARP-based cut of a host on your LAN.\n"
"Responder / ReverseShell / SSH / TelNET / SOCKS4 / TCP client+listen / "
"Wireguard: network client/relay tools for authorized testing.\n"
"Start WiFi AP: bring up an access point from the Deuce.\n"
"Connect to Wifi: join a network (needed by Claude AI, trackers, radio, "
"health checks).\n"
"Change MAC: spoof the WiFi MAC.\n"
"WiFi Pass Recovery: read back saved network credentials.\n"
"WiFi Radio: SD-backed synced internet-radio station with a browser player "
"(see Others notes).\n"
"Device Finder: pick an AP, then home in on its live RSSI (warmer/colder).\n"
"Wake-on-LAN: enter a MAC, sends a magic packet to wake that device.\n"
"Speed Test: WiFi latency + download throughput (Cloudflare). SEL retests.\n"
"\n"
"== BLUETOOTH (BLE) ==\n"
"BLE Scan: list nearby BLE devices (name/addr/RSSI).\n"
"BLE Sniffer: capture advertising traffic.\n"
"BLE Spam / Bad BLE: flood advertising packets (pairing popups etc.).\n"
"BLE Keyboard / Media Cmds / Presenter: act as a BLE HID keyboard/remote to "
"control a paired computer or slides.\n"
"iBeacon: broadcast an iBeacon.\n"
"Ninebot: scooter-specific BLE commands.\n"
"\n"
"== RF / SUB-GHz (CC1101) ==\n"
"Needs the CC1101 fitted (shown on the RF menu title).\n"
"Scan/copy: find and capture a sub-GHz signal (315/433/868/915 MHz), then "
"replay it. Set the frequency in Config first if auto-scan misses it.\n"
"Record RAW: capture raw timings to SD for later replay/analysis.\n"
"Custom SubGhz: transmit a hand-entered protocol/code.\n"
"Spectrum / RSSI Spectrum: live signal-strength across the band to spot "
"active frequencies.\n"
"SquareWave Spec / Spectogram: waveform and waterfall views.\n"
"Listen: demodulate and play/inspect a signal.\n"
"Bruteforce: step through codes for simple fixed-code devices (authorized "
"only).\n"
"Jammer: transmit noise on a frequency (illegal in most places -- bench use "
"only).\n"
"TPMS: decode tire-pressure sensors (Toyota/Ford/Nissan/Schrader-GM) on 315 "
"or 433.92 MHz -- shows sensor ID, pressure, temperature.\n"
"Faraday Test: measure how much a bag/box/room attenuates CC1101 signal. "
"Take an open-air reading, then a shielded reading; it reports the dB drop "
"and a pass/fail verdict.\n"
"Antenna Plot: measure a real radiation pattern. Pick the CC1101 band, "
"toggle which antennas to sweep (CC1101/WiFi/NRF24), then rotate the unit; "
"it plots RSSI vs bearing as a colour-coded polar graph with peak dBm, "
"front/back ratio, beamwidth and estimated gain. With the magnetometer it "
"auto-reads bearing; otherwise capture in 30-degree steps.\n"
"Live Aim: real-time signal bars for aiming antennas. Pick the CC1101 band; "
"you get live bars for CC1101 (real dBm), WiFi (loudest AP dBm) and NRF24 "
"(carrier-detect %, shown n/a if the NRF isn't active). Each bar has a white "
"peak-hold tick; SEL resets peaks; heading shows top-right with the "
"magnetometer. Bend a whip toward your target and watch its bar climb.\n"
"Fox Hunt: RF direction finding. Pick a band; live RSSI warmer/colder plus "
"a per-bearing peak map (needs magnetometer) with an arrow to the source.\n"
"\n"
"== NRF24 (2.4 GHz) ==\n"
"Information: read the NRF24 module registers/status.\n"
"Spectrum: 2.4GHz channel activity scan.\n"
"NRF Jammer: transmit noise across 2.4GHz (bench only).\n"
"MouseJack: test vulnerable wireless mice/keyboards (authorized only).\n"
"\n"
"== INFRARED (IR) ==\n"
"IR Read: capture a remote's code via the IR receiver.\n"
"Custom IR: send a specific code you enter.\n"
"IR Library: browse the SD-backed appliance database and send codes.\n"
"TV-B-Gone: blast common TV power-off codes.\n"
"IR Jammer: flood IR to block other remotes.\n"
"Intervalometer: trigger a camera's IR shutter on a timed interval for "
"time-lapse.\n"
"\n"
"== NFC / RFID (PN532) ==\n"
"Under I2C -> RFID. Scan/Read tag: identify and dump a 13.56MHz tag. Read "
"125kHz: low-frequency tags. Read EMV: read a contactless card's public "
"data. Write/Emulate NDEF: write records or emulate a tag. Chameleon / "
"Amiibolink / SRIX / Add MIF Key: specialist tag tools. iButton (Others): "
"read/emulate 1-Wire iButton keys.\n"
"\n"
"== GPS (NEO-6M) ==\n"
"GPS Tracker: live position/speed/altitude and logging.\n"
"Wardriving: log WiFi and/or BLE devices tagged with GPS coordinates to SD "
"(Scan WiFi / Scan BLE / Scan Both).\n"
"Waypoint Nav: SEL marks your spot as a target, then shows distance + a "
"compass arrow to it (arrow needs the magnetometer).\n"
"Sun Finder: sun azimuth/elevation + sunrise/sunset for your GPS fix, with "
"a compass arrow to the sun.\n"
"Trip Computer: speed, max, heading, trip distance (SEL resets), altitude, "
"clock and cabin temp.\n"
"RF Coverage: pick a CC1101 band, walk a site; SEL logs geotagged CC1101/"
"WiFi/NRF signal to /deuce/rfmap.csv (NEXT = auto) and draws a colour-coded "
"coverage trail.\n"
"Lap Timer: GPS start/finish gate. SEL marks a gate at your position; crossing "
"it starts the timer, crossing again banks a lap. Shows last/best lap + count; "
"NEXT clears the gate. Outdoors only.\n"
"Reverse Geo: SEL resolves your current GPS position to a place name (locality/"
"city/region/country) via BigDataCloud. Needs a fix + WiFi, no API key.\n"
"\n"
"== OTHERS ==\n"
"Health Checks: eight read-only network-safety modules -- WiFi Hygiene, "
"Internet/Latency, Device Inventory, Service/Port Scan, Attack Watchdog, "
"Rogue DHCP, UPnP/mDNS Exposure, and a consolidated Health Report. Run them "
"against your own network to see what is exposed.\n"
"VIN Decoder: enter a 17-char VIN. Offline it decodes the WMI (maker/region) "
"and check digit; online it pulls the full NHTSA record.\n"
"Lock Calc: enter the sticking/known digits of a combination padlock and it "
"lists the valid remaining combinations to try.\n"
"Barcodes: generate Code128/UPC-A/EAN-13/Code93/39/11. Enter data, see it "
"live, export a PNG to SD.\n"
"QRCodes: generate a QR from text/URL; Saved Codes tab stores favourites.\n"
"Ghost Radar: sweep RF field strength (with the magnetometer, true EMF). It "
"self-calibrates to the room's baseline, then flags spikes -- a field tool, "
"not a real ghost detector.\n"
"Resistor Calc: pick the colour bands (4/5/6) and it shows resistance and "
"tolerance; it also detects if you entered the bands reversed.\n"
"Crypto: live top-coin prices in a 2x2 grid with 24h change and a sparkline. "
"Needs WiFi. Swipe for more; SEL refreshes; it updates every 60s.\n"
"Money: live USD exchange rates. Tile 1 is USD (1.0000), then GBP/EUR/CAD/"
"CNY/JPY/AUD/MXN/INR with a ~2-week trend sparkline. Needs WiFi. Swipe for "
"more; SEL refreshes.\n"
"Key Copier: pick a key blank and enter the bitting depths; it draws the key "
"profile to help duplicate a key you own.\n"
"Microphone / Record / USB tools: audio capture and (on USB-capable builds) "
"HID devices.\n"
"Scope/Gen: SEL toggles Mini Scope (plots an ADC pin, default GPIO39 = "
"battery; NEXT cycles timebase; shows min/max V) and Signal Gen (square wave "
"on GPIO32; UP/DOWN halve/double 1Hz-100kHz; NEXT on/off). Keep inputs <=3.3V.\n"
"Morse: type text, it encodes to Morse and shows dots/dashes. SEL plays it as "
"a flashing box + buzzer beep (GPIO32); UP/DOWN adjust WPM.\n"
"TOTP: 6-digit 30s authenticator codes (RFC 6238). SEL adds an account (name + "
"base32 secret); saved to /deuce/totp.csv. Lists codes with a countdown bar. "
"RTC must be set. Secrets are plaintext on SD -- keep the card safe.\n"
"SHT41: live temperature + humidity. Big T/RH plus dew point, absolute "
"humidity, heat index and a comfort label; dual scrolling graphs; SEL = "
"T-vs-RH comfort map; NEXT = C/F. Also drives the status-bar temperature.\n"
"Magnetometer: MMC5983 suite. SEL cycles Compass (NEXT = figure-8 cal), "
"Field Meter (uT + axis bars + graph), Metal Detect (deviation from a "
"zeroed baseline + alert, NEXT = re-zero), and Raw XYZ.\n"
"Site Survey: SEL logs a geotagged row of temp/humidity/heading/field/WiFi "
"to /deuce/survey.csv (NEXT = 3s auto-log). Needs a GPS fix.\n"
"Weather Log: SHT41 logging to /deuce/weather.csv every 60s (NEXT toggle, "
"SEL now) with min/max and a trend graph.\n"
"Tripwire: SEL arms a magnetometer trip alarm; a magnet/door moving the "
"field past threshold flags TRIPPED and logs to /deuce/tripwire.csv. NEXT "
"re-baselines.\n"
"Weather Fcast: IP-geolocated current conditions + 3-day forecast "
"(open-meteo, no key). Needs WiFi.\n"
"Access Log (RFID menu): tap tags to log UID + time to /deuce/nfclog.csv.\n"
"How To: this guide.\n"
"\n"
"== CLOCK ==\n"
"Stopwatch, Countdown and Timer (99:99:99), backed by the DS3231 real-time "
"clock so time survives power-off.\n"
"\n"
"== FILES / SCRIPTS ==\n"
"Browse SD and LittleFS, run scripts, USB Mass Storage, and send/receive "
"files and commands. WebUI launches the on-device web control panel.\n"
"\n"
"== CLAUDE AI ==\n"
"On-device chat with Anthropic's API over WiFi. Set your API key (device or "
"WebUI /claude), pick Haiku/Sonnet/Opus or a custom model, and a token "
"limit. Requests are stateless to save memory; the key stays on the device.\n"
"\n"
"== WEBUI ==\n"
"Browse to http://deuce.local or the device IP, log in, and configure "
"branding/theme and Claude from a browser instead of the touchscreen.\n"
"\n"
"== CONFIG / SETTINGS ==\n"
"Set pins, RF module/frequency, IR pins, brightness, theme colours, WiFi TX "
"power, time and more. RF tools need the RF module set to CC1101 here.\n"
"\n"
"== UPDATING ==\n"
"Flash over USB with the web installer (Chrome/Edge desktop) at "
"xxxtrippy.github.io/The-Deuce, or pio -t upload. Use a good USB DATA cable "
"on a direct port with the battery charged -- a weak cable or brownout "
"causes write failures mid-flash. There is no OTA on the 4MB layout.\n"
"\n"
"== SAFETY ==\n"
"Transmit/attack tools (RF Jammer, NRF Jammer, WiFi/BLE "
"attacks, MouseJack, Evil Portal) are for devices and "
"networks you own or have written permission to test. Jamming and "
"unlicensed transmit are illegal in most places. Use responsibly.\n";

// ===========================================================================
// Scrollable viewer: wrap once into line-start offsets, page with UP/DOWN.
// ===========================================================================
#define HOWTO_MAXLINES 900

static uint16_t g_start[HOWTO_MAXLINES];
static int g_nlines = 0;

static inline char gch(int i) { return (char)pgm_read_byte(GUIDE + i); }

static void howtoWrap(int maxChars) {
    g_nlines = 0;
    int n = strlen_P(GUIDE);
    int i = 0;
    while (i < n && g_nlines < HOWTO_MAXLINES) {
        g_start[g_nlines++] = (uint16_t)i;
        int lineLen = 0, lastSpace = -1;
        int j = i;
        while (j < n) {
            char c = gch(j);
            if (c == '\n') { j++; break; }          // hard newline ends the line
            if (c == ' ') lastSpace = j;
            lineLen++;
            if (lineLen >= maxChars) {               // wrap
                if (lastSpace > i) j = lastSpace + 1; // break after last space
                else j = j + 1;                       // long word: hard break
                break;
            }
            j++;
        }
        i = j;
    }
}

static void howtoDraw(int top, int visible, int maxChars) {
    int W = tft.width(), H = tft.height();
    uint16_t pri = bruceConfig.priColor, bg = bruceConfig.bgColor, sec = bruceConfig.secColor;
    tft.fillScreen(bg);
    tft.setTextSize(1);

    int y = 4;
    char buf[80];
    if (maxChars > (int)sizeof(buf) - 1) maxChars = sizeof(buf) - 1;
    for (int r = 0; r < visible; r++) {
        int ln = top + r;
        if (ln >= g_nlines) break;
        int s = g_start[ln];
        int e = (ln + 1 < g_nlines) ? g_start[ln + 1] : (int)strlen_P(GUIDE);
        int k = 0;
        for (int p = s; p < e && k < maxChars; p++) {
            char c = gch(p);
            if (c == '\n') break;
            buf[k++] = c;
        }
        // trim trailing space
        while (k > 0 && buf[k - 1] == ' ') k--;
        buf[k] = 0;
        // headers (lines starting with "==") in primary colour
        tft.setTextColor((buf[0] == '=' ) ? pri : sec, bg);
        tft.drawString(buf, 6, y);
        y += 11;
    }

    // scrollbar
    if (g_nlines > visible) {
        int trackH = H - 8;
        int barH = trackH * visible / g_nlines; if (barH < 8) barH = 8;
        int barY = 4 + (trackH - barH) * top / (g_nlines - visible);
        tft.fillRect(W - 3, 4, 2, trackH, bg);
        tft.fillRect(W - 3, barY, 2, barH, pri);
    }
}

void deuceHowTo() {
    int W = tft.width(), H = tft.height();
    int cw = tft.textWidth("0"); if (cw < 1) cw = 6;
    int maxChars = (W - 12) / cw;
    if (maxChars < 10) maxChars = 10;
    int visible = (H - 8) / 11; if (visible < 4) visible = 4;

    howtoWrap(maxChars);

    int top = 0;
    int pageStep = visible - 1; if (pageStep < 1) pageStep = 1;
    int maxTop = g_nlines - visible; if (maxTop < 0) maxTop = 0;
    howtoDraw(top, visible, maxChars);

    bool prevLive = false;
    while (true) {
        if (check(EscPress)) break;
        bool redraw = false;

        // buttons: DOWN/SEL = page down, UP = page up
        if (check(DownPress) || check(SelPress)) {
            if (top < maxTop) { top += pageStep; if (top > maxTop) top = maxTop; redraw = true; }
        }
        if (check(UpPress)) {
            if (top > 0) { top -= pageStep; if (top < 0) top = 0; redraw = true; }
        }

        // touch: tap upper half = page up, lower half = page down
        bool live; int tx, ty;
        deuceLiveTouch(live, tx, ty);
        if (live && !prevLive) {
            if (ty < H / 2) { if (top > 0) { top -= pageStep; if (top < 0) top = 0; redraw = true; } }
            else            { if (top < maxTop) { top += pageStep; if (top > maxTop) top = maxTop; redraw = true; } }
        }
        prevLive = live;

        if (redraw) howtoDraw(top, visible, maxChars);
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}
