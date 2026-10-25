<p align="center">
  <img src="docs/the-deuce.jpg" alt="The Deuce" width="500">
</p>

<p align="center">
  <img src="docs/the-deuce-title-lcd14.svg"
       alt="THE DEUCE"
       width="500">
</p>
<p align="center">
  <em>A custom ESP32 handheld multitool — wireless testing, RF, IR, NFC, GPS, and an on-device Claude AI chat, wrapped in a matrix-green segmented-LCD UI.</em>
</p>

<p align="center">
  <img src="https://github.com/xxxtrippy/the-deuce/actions/workflows/deuce_ci.yml/badge.svg" alt="Build">
  &nbsp;·&nbsp; ESP32-WROOM-32U &nbsp;·&nbsp; 2.4" ILI9341 Touch &nbsp;·&nbsp; private build
</p>

---

## What it is

The Deuce is a hand-built ESP32-WROOM-32U handheld on a 2.4" resistive touchscreen, running a heavily customized firmware with its own UI, its own WebUI, and a growing set of original modules. It carries Sub-GHz RF, NRF24, WiFi, BLE, IR, GPS, RFID/NFC and SD on a tightly packed two-SPI-bus layout — and it talks to Claude.

This is a personal, private build. It is not a product and not for distribution.

---

## Signature features

### Custom paged touch UI
- **3-page, 2×2 swipe grid** main menu — swipe between pages, tap a tile to open it. No carousel, no prev/sel/next bar.
- **14-segment "starburst" LCD font** on every menu label, with real inter-segment and corner breaks, uniform height, and even spacing — rendered live, not a bitmap.
- **Matrix-rain background** that flows *behind* the labels (reserved cells, no bleed), with brightness / speed / color configurable in `Config → UI`.
- **Live header** — clock, battery %, and status icons (WiFi / SD / BLE / GPS / WebUI), plus a **lightning-bolt indicator** when the flash switch (GPIO2) is grounded.
- **GPIO36 hardware Back button**, debounced.

### Claude AI — on-device
- Text-only chat with Anthropic's **Messages API**, straight from the handheld over WiFi.
- **Model-selectable** in `Config → Claude AI` (Haiku / Sonnet / Opus / custom id) with an adjustable token cap.
- Stateless **one-in / one-out** design — reliable on the no-PSRAM WROOM, no history buildup.
- API key set on-device **or** from the WebUI at `http://<device-ip>/claude`. Key stored on the device, never in the repo.

### Health Checks — home-network testing suite
Eight defensive tests under `Others → Health Checks`:
`WiFi Hygiene` · `Internet / Latency` · `Device Inventory` · `Service / Port Scan` · `Attack Watchdog` · `Rogue DHCP` · `UPnP / mDNS Exposure` · `Health Report`

### De-branded WebUI
- Served at **`deuce.local`** (or the device IP) with its own login.
- Matrix-green theme that tracks the device's primary/secondary/background colors, animated rain, and The Deuce logo.

### The radios
`CC1101` Sub-GHz RF · `NRF24` · WiFi · BLE · `IR` (TX/RX) · `GPS` (NEO-6M) · `PN532` NFC + `DS3231` RTC on I²C · `SD` card · battery monitor.

---

## Roadmap

In progress / planned:

- [ ] Clock: **Stopwatch** + **Countdown timer** (99:99:99)
- [ ] **Barcode generator** — Code 128, UPC-A, EAN-13, Code 93, Code 39, Code 11 (on-screen + PNG to SD)
- [ ] **IR appliance library** (SD code database + browser) · **Intervalometer**
- [ ] **VIN decoder** (offline WMI + NHTSA online)
- [ ] **Lock combo calculator**
- [ ] **Sentry-Safe** interface (I²C line)
- [ ] QR **Saved Codes** tab

---

## Hardware

| | |
|---|---|
| **MCU** | ESP32-WROOM-32U (classic, no PSRAM) |
| **Display** | 2.4" ILI9341 320×240, rotation 3 |
| **Touch** | XPT2046 resistive (VSPI: SCLK 18 / MOSI 23 / MISO 19, CS 21) |
| **Radio/SD bus** | shared SPI — SCK 14 / MOSI 13 / MISO 26 · CC1101 SS 22 / NRF CSN 27 / SD CS 15 |
| **I²C** | SDA 0 / SCL 2 (PN532 NFC, DS3231 RTC) |
| **IR** | TX 25 / RX 35 · **GPS** NEO-6M · **battery** ADC 39 · **Back** btn 36 |
| **Flash** | 4 MB — app 3.69 MB / LittleFS 256 KB (`custom_4Mb_deuce.csv`) |

---

## Build & flash

Requires [PlatformIO](https://platformio.org/).

```bash
pio run -e Deuce_v1_1 -t upload
```

Build output lands in `.pio/build/Deuce_v1_1/`. Every push is compiled on CI (`Deuce CI`) before anything is flashed, and the workflow reports the exact flash/RAM usage so the app partition never overflows.

> There's also a `Deuce` env (v1.0, the original stock-style menu) kept alongside for reference.

---

<sub>Built on the open-source <a href="https://github.com/pr3y/Bruce">Bruce</a> firmware. Private, non-distributed build.</sub>
