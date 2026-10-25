// THE DEUCE v1.1 -- Home Network "Health Checks" suite (Others -> Health Checks).
// Defensive / audit tooling for your OWN network. Modules: WiFi Hygiene, Internet/
// Latency, Device Inventory (ping sweep + new-device alert), Service/Port Scan,
// Attack Watchdog (evil-twin scan), Rogue DHCP, UPnP Exposure (SSDP), Health Report.
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <globals.h>
#include "core/display.h"
// host discovery (ICMP ping) -- same primitives ARPScanner.cpp uses, so they're in the build
#include "esp_ping.h"
#include "lwip/ip_addr.h"
#include "ping/ping_sock.h"

// ---- wait for a tap (or the GPIO36 Back) to leave a results screen ----
// check() on this board calls InputHandler() internally, which refreshes touchPoint,
// so we can poll the tap without touching the input chip directly.
static void hcWaitTap() {
    delay(250); // swallow the press that opened this screen
    touchPoint.Clear();
    while (true) {
        check(SelPress); // drives InputHandler() -> updates touchPoint / EscPress
        if (touchPoint.pressed) {
            touchPoint.Clear();
            return;
        }
        if (EscPress) {
            EscPress = false;
            return;
        }
        delay(15);
    }
}

static void hcHeader(const char *title) {
    tft.fillScreen(bruceConfig.bgColor);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setTextSize(2);
    tft.drawCentreString(title, tftWidth / 2, 6, 1);
    tft.drawFastHLine(0, 26, tftWidth, bruceConfig.priColor);
    tft.setTextSize(1);
    tft.setCursor(6, 34);
}

// ============================= WiFi Hygiene =============================
static const char *hcAuthName(wifi_auth_mode_t a) {
    switch (a) {
        case WIFI_AUTH_OPEN: return "OPEN";
        case WIFI_AUTH_WEP: return "WEP";
        case WIFI_AUTH_WPA_PSK: return "WPA";
        case WIFI_AUTH_WPA2_PSK: return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
        case WIFI_AUTH_WPA3_PSK: return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
        default: return "?";
    }
}
static void hcWifiHygiene() {
    hcHeader("WiFi Hygiene");
    tft.println("Scanning APs...");
    int n = WiFi.scanNetworks();

    hcHeader("WiFi Hygiene");
    if (n <= 0) {
        tft.println("No networks found.");
        tft.println("");
        tft.println("Tap to exit.");
        hcWaitTap();
        return;
    }
    int shown = n > 12 ? 12 : n;
    for (int i = 0; i < shown; i++) {
        wifi_auth_mode_t a = WiFi.encryptionType(i);
        uint16_t col = bruceConfig.priColor;
        if (a == WIFI_AUTH_OPEN || a == WIFI_AUTH_WEP) col = TFT_RED; // weak
        else if (a == WIFI_AUTH_WPA3_PSK || a == WIFI_AUTH_WPA2_WPA3_PSK) col = TFT_GREEN; // strong
        tft.setTextColor(col, bruceConfig.bgColor);
        String ssid = WiFi.SSID(i);
        if (ssid.length() > 13) ssid = ssid.substring(0, 13);
        tft.println(
            String(hcAuthName(a)) + " c" + String(WiFi.channel(i)) + " " + String((int)WiFi.RSSI(i)) +
            "d " + ssid
        );
    }
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.println("");
    tft.println("RED=open/WEP GRN=WPA3");
    tft.println("Tap to exit.");
    WiFi.scanDelete();
    hcWaitTap();
}

// ============================ Internet / Latency ============================
static void hcInternet() {
    hcHeader("Internet Check");
    if (!wifiConnected) {
        tft.println("Not joined to WiFi.");
        tft.println("");
        tft.println("Connect to your network");
        tft.println("in the WiFi menu first,");
        tft.println("then run this again.");
        tft.println("");
        tft.println("Tap to exit.");
        hcWaitTap();
        return;
    }
    tft.println("Probing 1.1.1.1:53 ...");
    const int TRIES = 5;
    int ok = 0;
    long tmin = 999999, tmax = 0, tsum = 0;
    for (int i = 0; i < TRIES; i++) {
        WiFiClient c;
        unsigned long t0 = millis();
        bool r = c.connect("1.1.1.1", 53, 1500);
        long dt = (long)(millis() - t0);
        if (r) {
            ok++;
            tsum += dt;
            if (dt < tmin) tmin = dt;
            if (dt > tmax) tmax = dt;
            c.stop();
        }
        delay(120);
    }

    hcHeader("Internet Check");
    tft.println("Your IP : " + WiFi.localIP().toString());
    tft.println("Gateway : " + WiFi.gatewayIP().toString());
    tft.println("DNS srv : " + WiFi.dnsIP().toString());
    tft.println("");
    if (ok == 0) {
        tft.setTextColor(TFT_RED, bruceConfig.bgColor);
        tft.println("INTERNET: DOWN (0/5)");
    } else {
        tft.setTextColor(ok == TRIES ? TFT_GREEN : bruceConfig.priColor, bruceConfig.bgColor);
        tft.printf("Reachable: %d/%d\n", ok, TRIES);
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        tft.printf("Lat min/avg/max ms:\n  %ld / %ld / %ld\n", tmin, tsum / ok, tmax);
        tft.printf("Packet loss: %d%%\n", (TRIES - ok) * 100 / TRIES);
    }
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.println("");
    tft.println("Tap to exit.");
    hcWaitTap();
}

// ============================ Attack Watchdog ============================
// Scans the airspace and flags any SSID advertised by 2+ different BSSIDs -- the
// signature of an evil-twin / Wi-Fi impersonation attack (also legitimately caused
// by a mesh or range-extender, which is why it's flagged for review, not alarmed).
static void hcAttackWatch() {
    hcHeader("Attack Watchdog");
    tft.println("Scanning airspace...");
    int n = WiFi.scanNetworks();

    hcHeader("Attack Watchdog");
    if (n <= 0) {
        tft.println("No APs found.");
        tft.println("");
        tft.println("Tap to exit.");
        hcWaitTap();
        return;
    }
    int dup = 0;
    for (int i = 0; i < n; i++) {
        String si = WiFi.SSID(i);
        if (si.length() == 0) continue;
        bool firstSeen = true;
        for (int j = 0; j < i; j++)
            if (WiFi.SSID(j) == si) {
                firstSeen = false;
                break;
            }
        if (!firstSeen) continue;
        int cnt = 0;
        for (int j = 0; j < n; j++)
            if (WiFi.SSID(j) == si) cnt++;
        if (cnt >= 2) {
            dup++;
            tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
            String s = si.length() > 15 ? si.substring(0, 15) : si;
            tft.println(s + "  x" + String(cnt) + " APs");
        }
    }
    WiFi.scanDelete();
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.println("");
    if (dup == 0) {
        tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
        tft.println("No duplicate SSIDs.");
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    } else {
        tft.println("Dupes = evil-twin OR your");
        tft.println("own mesh/extender. Verify");
        tft.println("the extra BSSIDs are yours.");
    }
    tft.println("");
    tft.println("Tap to exit.");
    hcWaitTap();
}

// ============================ UPnP Exposure ============================
// Sends an SSDP M-SEARCH and lists devices that answer. UPnP-exposed gear (routers
// with UPnP-IGD, media servers, cameras) is a common home attack surface.
static void hcUPnP() {
    hcHeader("UPnP Exposure");
    if (!wifiConnected) {
        tft.println("Join your WiFi first");
        tft.println("(WiFi menu), then retry.");
        tft.println("");
        tft.println("Tap to exit.");
        hcWaitTap();
        return;
    }
    tft.println("Probing SSDP (3s)...");

    WiFiUDP udp;
    udp.begin(8888); // local port; SSDP replies come back here
    const char *msearch = "M-SEARCH * HTTP/1.1\r\n"
                          "HOST: 239.255.255.250:1900\r\n"
                          "MAN: \"ssdp:discover\"\r\n"
                          "MX: 2\r\n"
                          "ST: ssdp:all\r\n\r\n";
    IPAddress mcast(239, 255, 255, 250);
    udp.beginPacket(mcast, 1900);
    udp.write((const uint8_t *)msearch, strlen(msearch));
    udp.endPacket();

    IPAddress found[16];
    int nf = 0;
    char buf[400];
    unsigned long t0 = millis();
    while (millis() - t0 < 3000) {
        int sz = udp.parsePacket();
        if (sz > 0) {
            IPAddress from = udp.remoteIP();
            bool have = false;
            for (int i = 0; i < nf; i++)
                if (found[i] == from) have = true;
            if (!have && nf < 16) found[nf++] = from;
            int r = udp.read(buf, sizeof(buf) - 1); // drain (counted by source IP)
            (void)r;
        }
        delay(5);
    }
    udp.stop();

    hcHeader("UPnP Exposure");
    if (nf == 0) {
        tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
        tft.println("No UPnP devices replied.");
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        tft.println("(Good -- smaller attack");
        tft.println(" surface.)");
    } else {
        tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
        tft.printf("UPnP devices: %d\n", nf);
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        for (int i = 0; i < nf && i < 7; i++) tft.println(" " + found[i].toString());
        tft.println("UPnP can open ports to");
        tft.println("the internet -- review it.");
    }
    tft.println("");
    tft.println("Tap to exit.");
    hcWaitTap();
}

// ============================ host discovery (ICMP ping) ============================
static volatile bool hc_pg_ok = false, hc_pg_done = false;
static void hc_pg_success(esp_ping_handle_t h, void *a) { hc_pg_ok = true; }
static void hc_pg_timeout(esp_ping_handle_t h, void *a) {}
static void hc_pg_end(esp_ping_handle_t h, void *a) {
    esp_ping_stop(h);
    esp_ping_delete_session(h);
    hc_pg_done = true;
}
// Ping one host once; true if it replied. ~timeout_ms for a dead host.
static bool hcPing(IPAddress ip) {
    hc_pg_ok = false;
    hc_pg_done = false;
    esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
    ip_addr_t target;
    memset(&target, 0, sizeof(target));
    target.type = IPADDR_TYPE_V4;
    target.u_addr.ip4.addr = (uint32_t)ip; // Arduino IPAddress matches lwip's byte order
    cfg.target_addr = target;
    cfg.count = 1;
    cfg.timeout_ms = 100;
    cfg.interval_ms = 20;
    esp_ping_callbacks_t cbs = {
        .cb_args = NULL,
        .on_ping_success = hc_pg_success,
        .on_ping_timeout = hc_pg_timeout,
        .on_ping_end = hc_pg_end,
    };
    esp_ping_handle_t ph;
    if (esp_ping_new_session(&cfg, &cbs, &ph) != ESP_OK) return false;
    esp_ping_start(ph);
    unsigned long t0 = millis();
    while (!hc_pg_done && millis() - t0 < 500) delay(4);
    return hc_pg_ok;
}
// small status line at y34 during a scan
static void hcStatus(const String &s) {
    tft.fillRect(6, 34, tftWidth - 12, 10, bruceConfig.bgColor);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.setCursor(6, 34);
    tft.print(s);
}

// ============================ Device Inventory ============================
// Ping-sweeps your /24, lists everything that answers, and flags anything NEW since
// the last run (baseline saved to LittleFS /deuce_inv.txt). ~25-30s; Back cancels.
static void hcInventory() {
    hcHeader("Device Inventory");
    if (!wifiConnected) {
        tft.println("Join your WiFi first.");
        tft.println("");
        tft.println("Tap to exit.");
        hcWaitTap();
        return;
    }
    IPAddress me = WiFi.localIP();
    String base = "";
    {
        File f = LittleFS.open("/deuce_inv.txt", "r");
        if (f) {
            base = f.readString();
            f.close();
        }
    }
    tft.setCursor(6, 48);
    tft.println("Sweeping /24 (Back=stop)");
    String cur = "";
    int live = 0, neu = 0;
    unsigned long disp = 0;
    for (int h = 1; h <= 254; h++) {
        if (millis() - disp > 300) {
            hcStatus("Probing ." + String(h) + "   found " + String(live));
            disp = millis();
        }
        IPAddress ip(me[0], me[1], me[2], h);
        if (hcPing(ip)) {
            live++;
            String s = ip.toString();
            cur += s + "\n";
            if (base.indexOf(s + "\n") < 0) neu++;
        }
        if (EscPress) {
            EscPress = false;
            break;
        }
        check(SelPress); // drives InputHandler so Back (EscPress) is seen
    }
    {
        File f = LittleFS.open("/deuce_inv.txt", "w");
        if (f) {
            f.print(cur);
            f.close();
        }
    }
    hcHeader("Device Inventory");
    tft.printf("Live devices: %d\n", live);
    if (neu > 0) {
        tft.setTextColor(TFT_YELLOW, bruceConfig.bgColor);
        tft.printf("NEW since last scan: %d\n", neu);
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    }
    tft.println("");
    int shown = 0, idx = 0;
    while (idx < (int)cur.length() && shown < 11) {
        int nl = cur.indexOf('\n', idx);
        if (nl < 0) break;
        String s = cur.substring(idx, nl);
        idx = nl + 1;
        bool isNew = base.indexOf(s + "\n") < 0;
        tft.setTextColor(isNew ? TFT_YELLOW : bruceConfig.priColor, bruceConfig.bgColor);
        tft.println((isNew ? "* " : "  ") + s);
        shown++;
    }
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.println("");
    tft.println("* = new.  Tap to exit.");
    hcWaitTap();
}

// ============================ Service / Port Scan ============================
// Port-scans the hosts found by the last Device Inventory (run that first), plus the
// gateway, for risky/common ports. Flags Telnet/SMB. Uses short TCP connects.
static void hcPortScan() {
    hcHeader("Service / Port Scan");
    if (!wifiConnected) {
        tft.println("Join your WiFi first.");
        tft.println("");
        tft.println("Tap to exit.");
        hcWaitTap();
        return;
    }
    // host list = last inventory + gateway
    String hosts = "";
    {
        File f = LittleFS.open("/deuce_inv.txt", "r");
        if (f) {
            hosts = f.readString();
            f.close();
        }
    }
    String gw = WiFi.gatewayIP().toString();
    if (hosts.indexOf(gw + "\n") < 0) hosts = gw + "\n" + hosts;
    if (hosts.length() < 7) {
        tft.println("No host list yet.");
        tft.println("Run Device Inventory");
        tft.println("first, then retry.");
        tft.println("");
        tft.println("Tap to exit.");
        hcWaitTap();
        return;
    }
    const uint16_t ports[] = {21, 22, 23, 80, 139, 443, 445, 554, 8080, 9100};
    const int NP = sizeof(ports) / sizeof(ports[0]);
    hcHeader("Service / Port Scan");
    int idx = 0, hostsDone = 0, risky = 0;
    while (idx < (int)hosts.length() && hostsDone < 10) {
        int nl = hosts.indexOf('\n', idx);
        if (nl < 0) break;
        String hs = hosts.substring(idx, nl);
        idx = nl + 1;
        if (hs.length() < 7) continue;
        IPAddress ip;
        if (!ip.fromString(hs)) continue;
        hostsDone++;
        hcStatus("Scanning " + hs + " ...");
        String open = "";
        for (int p = 0; p < NP; p++) {
            WiFiClient c;
            if (c.connect(ip, ports[p], 120)) {
                open += String(ports[p]) + " ";
                if (ports[p] == 23 || ports[p] == 445 || ports[p] == 139) risky++;
                c.stop();
            }
            if (EscPress) {
                EscPress = false;
                break;
            }
            check(SelPress);
        }
        if (open.length()) {
            bool r = open.indexOf("23 ") >= 0 || open.indexOf("445 ") >= 0 || open.indexOf("139 ") >= 0;
            tft.setTextColor(r ? TFT_RED : bruceConfig.priColor, bruceConfig.bgColor);
            tft.println(hs);
            tft.setTextColor(r ? TFT_RED : TFT_GREEN, bruceConfig.bgColor);
            tft.println("  " + open);
        }
    }
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    hcStatus("");
    tft.println("");
    if (risky) {
        tft.setTextColor(TFT_RED, bruceConfig.bgColor);
        tft.println("RED = Telnet/SMB exposed!");
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    } else {
        tft.println("No Telnet/SMB exposed.");
    }
    tft.println("Tap to exit.");
    hcWaitTap();
}

// ============================ Rogue DHCP ============================
// Broadcasts a DHCP DISCOVER and counts how many servers OFFER. More than one = a
// rogue DHCP server may be on your LAN (a classic traffic-hijack setup).
static void hcRogueDHCP() {
    hcHeader("Rogue DHCP");
    if (!wifiConnected) {
        tft.println("Join your WiFi first.");
        tft.println("");
        tft.println("Tap to exit.");
        hcWaitTap();
        return;
    }
    WiFiUDP udp;
    if (!udp.begin(68)) {
        tft.println("DHCP port 68 busy --");
        tft.println("can't test right now.");
        tft.println("");
        tft.println("Tap to exit.");
        hcWaitTap();
        return;
    }
    uint8_t mac[6];
    WiFi.macAddress(mac);
    uint8_t pkt[300];
    memset(pkt, 0, sizeof(pkt));
    pkt[0] = 1;
    pkt[1] = 1;
    pkt[2] = 6; // op=REQUEST, htype=ETH, hlen=6
    uint32_t xid = (uint32_t)millis() ^ ((uint32_t)random(0, 65535) << 16);
    memcpy(pkt + 4, &xid, 4);
    pkt[10] = 0x80; // flags: broadcast
    memcpy(pkt + 28, mac, 6);
    pkt[236] = 0x63;
    pkt[237] = 0x82;
    pkt[238] = 0x53;
    pkt[239] = 0x63; // magic cookie
    int o = 240;
    pkt[o++] = 53;
    pkt[o++] = 1;
    pkt[o++] = 1; // DHCP message type = DISCOVER
    pkt[o++] = 55;
    pkt[o++] = 3;
    pkt[o++] = 1;
    pkt[o++] = 3;
    pkt[o++] = 6; // param request list
    pkt[o++] = 255;
    udp.beginPacket(IPAddress(255, 255, 255, 255), 67);
    udp.write(pkt, o);
    udp.endPacket();

    tft.println("Listening (4s)...");
    IPAddress servers[8];
    int ns = 0;
    uint8_t rb[600];
    unsigned long t0 = millis();
    while (millis() - t0 < 4000) {
        int sz = udp.parsePacket();
        if (sz > 0) {
            int r = udp.read(rb, sizeof(rb));
            if (r >= 240 && rb[0] == 2) { // BOOTREPLY
                IPAddress from = udp.remoteIP();
                bool have = false;
                for (int i = 0; i < ns; i++)
                    if (servers[i] == from) have = true;
                if (!have && ns < 8) servers[ns++] = from;
            }
        }
        delay(5);
    }
    udp.stop();

    hcHeader("Rogue DHCP");
    if (ns <= 1) {
        tft.setTextColor(TFT_GREEN, bruceConfig.bgColor);
        tft.printf("DHCP servers: %d  (OK)\n", ns);
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        if (ns == 1) tft.println(" " + servers[0].toString());
    } else {
        tft.setTextColor(TFT_RED, bruceConfig.bgColor);
        tft.printf("WARNING: %d DHCP servers\n", ns);
        tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
        for (int i = 0; i < ns; i++) tft.println(" " + servers[i].toString());
        tft.println("A rogue DHCP server can");
        tft.println("reroute all your traffic.");
    }
    tft.println("");
    tft.println("Tap to exit.");
    hcWaitTap();
}

// ============================ Health Report ============================
// One-tap scorecard that runs quick versions of the key checks.
static void hcHealthReport() {
    hcHeader("Health Report");
    int score = 0, max = 0;

    // WiFi encryption of the airspace (weak APs nearby is informational)
    tft.println("Scanning WiFi...");
    int n = WiFi.scanNetworks();
    int openWep = 0, dupe = 0;
    for (int i = 0; i < n; i++) {
        wifi_auth_mode_t a = WiFi.encryptionType(i);
        if (a == WIFI_AUTH_OPEN || a == WIFI_AUTH_WEP) openWep++;
        String si = WiFi.SSID(i);
        bool first = true;
        for (int j = 0; j < i; j++)
            if (WiFi.SSID(j) == si) {
                first = false;
                break;
            }
        if (first && si.length()) {
            int c = 0;
            for (int j = 0; j < n; j++)
                if (WiFi.SSID(j) == si) c++;
            if (c >= 2) dupe++;
        }
    }
    WiFi.scanDelete();

    // internet reachability (quick)
    bool net = false;
    if (wifiConnected) {
        WiFiClient c;
        net = c.connect("1.1.1.1", 53, 1200);
        if (net) c.stop();
    }

    // UPnP quick probe
    int upnp = 0;
    if (wifiConnected) {
        WiFiUDP u;
        u.begin(8890);
        const char *m = "M-SEARCH * HTTP/1.1\r\nHOST: 239.255.255.250:1900\r\nMAN: \"ssdp:discover\"\r\nMX: 1\r\nST: ssdp:all\r\n\r\n";
        u.beginPacket(IPAddress(239, 255, 255, 250), 1900);
        u.write((const uint8_t *)m, strlen(m));
        u.endPacket();
        IPAddress f[16];
        int nf = 0;
        char b[200];
        unsigned long t0 = millis();
        while (millis() - t0 < 2000) {
            if (u.parsePacket() > 0) {
                IPAddress fr = u.remoteIP();
                bool h = false;
                for (int i = 0; i < nf; i++)
                    if (f[i] == fr) h = true;
                if (!h && nf < 16) f[nf++] = fr;
                u.read(b, sizeof(b));
            }
            delay(5);
        }
        u.stop();
        upnp = nf;
    }

    // last inventory count
    int devs = 0;
    {
        File fl = LittleFS.open("/deuce_inv.txt", "r");
        if (fl) {
            String s = fl.readString();
            fl.close();
            for (int i = 0; i < (int)s.length(); i++)
                if (s[i] == '\n') devs++;
        }
    }

    hcHeader("Health Report");
    auto line = [&](const String &label, bool good, const String &val) {
        tft.setTextColor(good ? TFT_GREEN : TFT_YELLOW, bruceConfig.bgColor);
        tft.println((good ? "[OK]  " : "[!]   ") + label + ": " + val);
        if (good) score++;
        max++;
    };
    tft.setTextSize(1);
    line("Internet", net, net ? "up" : "down/na");
    line("Weak APs near", openWep == 0, String(openWep));
    line("Duplicate SSIDs", dupe == 0, String(dupe));
    line("UPnP exposed", upnp == 0, String(upnp));
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.println("Devices (last scan): " + String(devs));
    tft.println("");
    tft.setTextColor(score == max ? TFT_GREEN : TFT_YELLOW, bruceConfig.bgColor);
    tft.printf("SCORE: %d / %d\n", score, max);
    tft.setTextColor(bruceConfig.priColor, bruceConfig.bgColor);
    tft.println("Tap to exit.");
    hcWaitTap();
}

// ============================ the submenu ============================
void healthChecksMenu() {
    while (true) {
        options = {
            {"WiFi Hygiene", hcWifiHygiene},
            {"Internet/Latency", hcInternet},
            {"Device Inventory", hcInventory},
            {"Service/Port Scan", hcPortScan},
            {"Attack Watchdog", hcAttackWatch},
            {"Rogue DHCP", hcRogueDHCP},
            {"UPnP Exposure", hcUPnP},
            {"Health Report", hcHealthReport},
            {"Back", []() {}},
        };
        int sel = loopOptions(options, MENU_TYPE_SUBMENU, "Health Checks");
        if (sel == -1 || sel == (int)options.size() - 1) return;
    }
}
