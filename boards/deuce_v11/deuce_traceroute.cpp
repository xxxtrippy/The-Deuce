// THE DEUCE v1.1 -- traceroute (ICMP, best-effort). See .h.

#include "deuce_traceroute.h"

#include <Arduino.h>
#include <WiFi.h>
#include <globals.h>
#include <lwip/netdb.h>
#include <lwip/sockets.h>

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/wifi/wifi_common.h"

#define MAX_HOPS 20
#define PER_HOP_TIMEOUT_MS 1200

struct IcmpEcho { uint8_t type, code; uint16_t cksum, id, seq; uint32_t stamp; };

static uint16_t icmpChecksum(const void *data, int len) {
    const uint16_t *p = (const uint16_t *)data;
    uint32_t sum = 0;
    while (len > 1) { sum += *p++; len -= 2; }
    if (len) sum += *(const uint8_t *)p;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (uint16_t)~sum;
}

void deuceTraceroute() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;

    String host = keyboard("8.8.8.8", 64, "Host / IP to trace");
    if (host.length() == 0) return;
    if (WiFi.status() != WL_CONNECTED && !wifiConnectMenu()) return;

    tft.fillScreen(bg);
    tft.setTextColor(pri, bg); tft.drawCentreString("Resolving...", W / 2, H / 2, 1);

    // resolve
    struct addrinfo hints = {}, *res = nullptr;
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_RAW;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) != 0 || !res) {
        displayError("DNS resolve failed", true); return;
    }
    struct sockaddr_in dest = *(struct sockaddr_in *)res->ai_addr;
    freeaddrinfo(res);
    char destip[16]; inet_ntoa_r(dest.sin_addr, destip, sizeof(destip));

    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) {
        std::vector<String> none;
        displayError("raw socket blocked", true);
        return;
    }
    struct timeval tv; tv.tv_sec = 0; tv.tv_usec = PER_HOP_TIMEOUT_MS * 1000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    std::vector<String> hops;
    hops.push_back("trace to " + String(destip));

    uint16_t id = (uint16_t)(millis() & 0xFFFF);
    bool reachedDest = false;

    tft.fillScreen(bg);
    tft.setTextColor(pri, bg); tft.drawCentreString("TRACEROUTE", W / 2, 3, 1);
    tft.drawFastHLine(0, 15, W, sec);

    for (int ttl = 1; ttl <= MAX_HOPS && !reachedDest; ttl++) {
        if (check(EscPress)) break;
        setsockopt(sock, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl));

        IcmpEcho echo = {};
        echo.type = 8; echo.code = 0; echo.id = htons(id); echo.seq = htons(ttl);
        echo.stamp = millis();
        echo.cksum = 0; echo.cksum = icmpChecksum(&echo, sizeof(echo));

        uint32_t t0 = millis();
        sendto(sock, &echo, sizeof(echo), 0, (struct sockaddr *)&dest, sizeof(dest));

        // receive
        uint8_t buf[128];
        struct sockaddr_in from; socklen_t fl = sizeof(from);
        String hopline;
        int n = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fl);
        int rtt = millis() - t0;
        if (n > 0) {
            char fip[16]; inet_ntoa_r(from.sin_addr, fip, sizeof(fip));
            // IP header length (buf[0] low nibble *4), then ICMP type at [ihl]
            int ihl = (buf[0] & 0x0F) * 4;
            uint8_t itype = (n > ihl) ? buf[ihl] : 0xFF;
            hopline = String(ttl) + ". " + String(fip) + "  " + String(rtt) + "ms";
            if (from.sin_addr.s_addr == dest.sin_addr.s_addr || itype == 0) { hopline += " *dest*"; reachedDest = true; }
            (void)itype;
        } else {
            hopline = String(ttl) + ". * (no reply)";
        }
        hops.push_back(hopline);

        // redraw list (tail)
        tft.fillRect(0, 18, W, H - 30, bg);
        int y = 20, start = hops.size() > 16 ? hops.size() - 16 : 0;
        for (int i = start; i < (int)hops.size(); i++) {
            tft.setTextColor(hops[i].indexOf("*dest*") >= 0 ? TFT_GREEN : pri, bg);
            tft.drawString(hops[i], 6, y); y += 11;
        }
        tft.setTextColor(sec, bg); tft.drawString("[ESC] stop", 4, H - 11);
    }
    close(sock);

    if (!reachedDest) hops.push_back("(stopped)");
    // final: let the user scroll
    int scroll = 0, rows = (H - 30) / 11;
    while (true) {
        if (check(EscPress) || check(SelPress)) break;
        if (check(DownPress) && scroll < (int)hops.size() - rows) scroll++;
        if (check(UpPress) && scroll > 0) scroll--;
        tft.fillScreen(bg);
        tft.setTextColor(pri, bg); tft.drawCentreString("TRACEROUTE", W / 2, 3, 1);
        tft.drawFastHLine(0, 15, W, sec);
        int y = 20;
        for (int i = scroll; i < (int)hops.size() && y < H - 12; i++) {
            tft.setTextColor(hops[i].indexOf("*dest*") >= 0 ? TFT_GREEN : pri, bg);
            tft.drawString(hops[i], 6, y); y += 11;
        }
        tft.setTextColor(sec, bg); tft.drawString("[UP/DN] scroll [SEL/ESC] exit", 4, H - 11);
        vTaskDelay(40 / portTICK_PERIOD_MS);
    }
}
