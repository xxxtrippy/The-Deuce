// THE DEUCE v1.1 -- WiFi probe-request / client logger. See .h.

#include "deuce_probelog.h"

#include <Arduino.h>
#include <SD.h>
#include <esp_wifi.h>
#include <globals.h>
#include <nvs_flash.h>

#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/sd_functions.h"
#include "core/wifi/wifi_common.h"

// ── lock-free ring the callback writes, the loop drains ───────────────────────
struct ProbeRaw { uint8_t mac[6]; char ssid[33]; int8_t rssi; uint8_t ch; };
#define RING 48
static volatile uint32_t pbHead = 0;
static ProbeRaw pbRing[RING];
static volatile uint8_t pbChannel = 1;

static void IRAM_ATTR probe_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    const wifi_promiscuous_pkt_t *pkt = (const wifi_promiscuous_pkt_t *)buf;
    if (!pkt) return;
    const uint8_t *p = pkt->payload;
    int len = pkt->rx_ctrl.sig_len;
    if (len < 26) return;
    uint8_t fc = p[0];
    if (((fc >> 2) & 0x3) != 0 || ((fc >> 4) & 0xF) != 4) return; // mgmt / probe-req
    ProbeRaw &r = pbRing[pbHead % RING];
    memcpy(r.mac, p + 10, 6);                 // addr2 = source
    r.rssi = pkt->rx_ctrl.rssi;
    r.ch = pbChannel;
    r.ssid[0] = 0;
    // tagged params at offset 24: tag 0 = SSID
    if (len >= 26 && p[24] == 0) {
        int sl = p[25]; if (sl > 32) sl = 32;
        if (24 + 2 + sl <= len) { memcpy(r.ssid, p + 26, sl); r.ssid[sl] = 0; }
    }
    pbHead++;
}

// ── aggregate table ───────────────────────────────────────────────────────────
struct Seen { uint8_t mac[6]; String ssid; int8_t rssi; uint16_t hits; uint32_t last; };

static void wifiStart() {
    ensureWifiPlatform();
    nvs_flash_init();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t e = esp_wifi_init(&cfg);
    if (e != ESP_OK && e != ESP_ERR_WIFI_INIT_STATE) {}
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
    esp_wifi_set_promiscuous(true);
    wifi_promiscuous_filter_t filt = {};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&filt);
    esp_wifi_set_promiscuous_rx_cb(probe_cb);
}
static void wifiStop() {
    esp_wifi_set_promiscuous(false);
    esp_wifi_set_promiscuous_rx_cb(NULL);
    esp_wifi_stop();
    wifiDisconnect();
    vTaskDelay(1 / portTICK_PERIOD_MS);
}

static String macStr(const uint8_t *m) {
    char b[18]; snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
    return String(b);
}

void deuceProbeLog() {
    int W = tft.width(), H = tft.height();
    uint16_t bg = bruceConfig.bgColor, pri = bruceConfig.priColor, sec = bruceConfig.secColor;

    std::vector<Seen> seen;
    uint32_t tail = 0, totalProbes = 0;
    bool logging = false;
    const uint8_t CHANS[] = {1, 6, 11, 2, 7, 3, 8, 4, 9, 5, 10, 13};
    const int NCH = sizeof(CHANS) / sizeof(CHANS[0]);
    int chIdx = 0;
    uint32_t lastHop = 0, lastDraw = 0;
    File logf;

    wifiStart();
    pbChannel = CHANS[0];
    esp_wifi_set_channel(CHANS[0], WIFI_SECOND_CHAN_NONE);

    while (true) {
        if (check(EscPress)) break;
        if (check(SelPress)) {
            logging = !logging;
            if (logging && sdcardMounted) {
                SD.mkdir("/deuce");
                logf = SD.open("/deuce/probes.csv", FILE_APPEND);
                if (logf && logf.size() == 0) logf.println("ms,mac,ssid,rssi,ch");
            } else if (!logging && logf) { logf.close(); }
        }
        if (check(NextPress)) { seen.clear(); totalProbes = 0; }

        // channel hop
        uint32_t now = millis();
        if (now - lastHop > 280) {
            lastHop = now;
            chIdx = (chIdx + 1) % NCH;
            pbChannel = CHANS[chIdx];
            esp_wifi_set_channel(CHANS[chIdx], WIFI_SECOND_CHAN_NONE);
        }

        // drain ring
        while (tail < pbHead) {
            if (pbHead - tail > RING) tail = pbHead - RING; // overrun: skip ahead
            ProbeRaw r = pbRing[tail % RING];
            tail++;
            totalProbes++;
            String ssid = strlen(r.ssid) ? String(r.ssid) : String("(broadcast)");
            // aggregate by mac+ssid
            bool found = false;
            for (auto &s : seen) {
                if (memcmp(s.mac, r.mac, 6) == 0 && s.ssid == ssid) {
                    s.hits++; s.rssi = r.rssi; s.last = now; found = true; break;
                }
            }
            if (!found) {
                Seen s; memcpy(s.mac, r.mac, 6); s.ssid = ssid; s.rssi = r.rssi; s.hits = 1; s.last = now;
                if (seen.size() < 200) seen.push_back(s);
            }
            if (logging && logf) {
                logf.printf("%lu,%s,%s,%d,%d\n", (unsigned long)now, macStr(r.mac).c_str(),
                            ssid.c_str(), r.rssi, r.ch);
                logf.flush();
            }
        }

        // draw ~3 fps
        if (now - lastDraw > 300) {
            lastDraw = now;
            tft.fillScreen(bg);
            tft.setTextColor(pri, bg); tft.setTextSize(1);
            tft.drawCentreString("PROBE LOGGER", W / 2, 3, 1);
            tft.drawFastHLine(0, 15, W, sec);
            char hdr[48];
            snprintf(hdr, sizeof(hdr), "ch%d  probes:%lu  uniq:%d", CHANS[chIdx], (unsigned long)totalProbes, (int)seen.size());
            tft.setTextColor(sec, bg); tft.drawString(hdr, 6, 18);

            // show most-recent entries
            int y = 32;
            int start = seen.size() > 13 ? seen.size() - 13 : 0;
            for (int i = (int)seen.size() - 1; i >= start && y < H - 22; i--) {
                Seen &s = seen[i];
                tft.setTextColor(pri, bg);
                tft.drawString(macStr(s.mac).substring(9), 6, y); // last 3 octets
                tft.setTextColor(sec, bg);
                String ss = s.ssid.length() > 16 ? s.ssid.substring(0, 16) : s.ssid;
                tft.drawString(ss, 60, y);
                char rh[16]; snprintf(rh, sizeof(rh), "%d x%d", s.rssi, s.hits);
                tft.drawRightString(rh, W - 6, y, 1);
                y += 11;
            }
            tft.setTextColor(logging ? TFT_GREEN : sec, bg);
            tft.drawString(logging ? "LOG on (/deuce/probes.csv)" : "[SEL] log  [NEXT] clear  [ESC]", 4, H - 11);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    if (logf) logf.close();
    wifiStop();
}
