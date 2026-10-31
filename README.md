 <p align="center">
  <img src="docs/the-deuce.jpg" alt="The Deuce" width="500">
</p>

<p align="center">
  <em>One rig. Two brains. Multiple attack surfaces. Total situational awareness.</em>
</p>

<p align="center">
  <img src="https://github.com/xxxtrippy/the-deuce/actions/workflows/deuce_ci.yml/badge.svg" alt="Build">
  &nbsp;·&nbsp; ESP32-WROOM-32U &nbsp;·&nbsp; ILI9341 Touch &nbsp;·&nbsp; Private Build
</p>

---

## `// SYST3M OVERV1EW`

The Deuce is a custom built ESP32xRP2040 handheld, engineered for wireless diagnostics, signal exploration, hardware interaction, and on-device intelligence.

Fabricated around the ESP32-WROOM-32U and a 2.4-inch touchscreen, it integrates a collection of radio interfaces, peripheral controllers, and purpose built firmware into a compact field platform.

No bullshit. No unnecessary overhead. Just a custom environment, made for direct access to the tools that matter most.

### Core capabilities

- Wireless network reconnaissance and diagnostics
- SubGHz RF and 2.4 GHz radio experimentation
- Infrared transmission and reception
- NFC / RFID interaction
- GPS positioning and RTC integration
- SD based storage and utilities
- Local device WebUI
- On-device interface for Anthropic's Claude AI

> **Build status:** Personal, private, continuously evolving firmware. Not a commercial product and not intended for distribution.

---

## `// OPERAT0R INT3RFACE`

### Custom segmented LCD UI

The interface is built from the ground up, around a segmented LCD "Matrix" aesthetic.

- **Three-page touch interface** — a 2×2 swipe based menu grid, with direct tile access and no navigation bars.
- **14-segment starburst typography** — custom rendered glyphs with deliberate segment breaks, consistent character height, and precise spacing.
- **Matrix rain engine** — animated background rendered behind the interface, with reserved label regions to prevent visual interference.
- **Live system header** — clock, battery percentage, WiFi, SD, BLE, GPS, and WebUI status indicators.
- **Hardware flash indicator** — lightning bolt status indicator triggered by the GPIO2 flash switch.
- **Dedicated Back input** — GPIO36 hardware button with debounce handling. No getting stuck.

The goal: a high information interface that stays readable, responsive, and consistent across the device.

---

## `// INT3LLIGENCE 5UBSYST3M`

### Claude AI · Onboard interface

A direct interface to Anthropic's Messages API, accessible from the handheld over WiFi.

- Selectable Haiku, Sonnet, Opus, or custom model ID.
- Adjustable output-token limit.
- Stateless request/response execution to avoid accumulating conversation history on memory-constrained hardware.
- API key configuration directly on the device or through the WebUI at `/claude`.
- Credentials remain on the device and are not committed to the repository.

The ESP32 handles the interface and network transport; inference is performed by Anthropic's remote API.

---

## `// NETW0RK H3ALTH & EXP0SURE`

### Health Checks

Eight defensive network testing modules are available under `Others → Health Checks`.

| Module | Purpose |
|---|---|
| WiFi Hygiene | Inspect basic wireless security configuration |
| Internet / Latency | Check connectivity and response times |
| Device Inventory | Identify devices visible on the local network |
| Service / Port Scan | Discover exposed services on authorized targets |
| Attack Watchdog | Monitor for supported suspicious activity |
| Rogue DHCP | Check for unexpected DHCP activity |
| UPnP / mDNS Exposure | Inspect local service-discovery exposure |
| Health Report | Consolidate available diagnostic results |

Designed for authorized environments, homelab testing, and understanding the devices/services exposed on your own network.

---

## `// WEB C0NTROL PL4NE`

### De-branded WebUI

The device hosts its own local web interface at:

- `http://deuce.local`
- `http://<device-ip>`

Features include:

- Dedicated login
- Animated Matrix rain visuals
- Theme colors synchronized with the device UI
- Device branding and Claude AI configuration

The WebUI provides a second control surface, without requiring interaction through the touchscreen for every configuration task.

---

## `// FE4TURE M4P`

Every on-device tool, grouped by its menu section. **★ = Deuce custom field tool** (added on top of the Bruce base); everything else is inherited and retained.

### Radio Frequency `RF`
- Scan / copy, Record RAW, Custom SubGHz send
- Spectrum, RSSI Spectrum, SquareWave Spectrum, Spectrogram (waterfall)
- Listen, Bruteforce, Jammer
- **TPMS reader ★** — Toyota / Ford / Nissan / Schrader-GM (315 / 433.92 MHz)
- **Faraday Test ★** — RF-shielding attenuation + verdict
- **Antenna Plot ★** — unified multi-antenna polar RSSI pattern (CC1101 / WiFi / NRF24, per-antenna toggle, colour-coded overlay, HPBW / F-B / est. gain)
- **Live Aim ★** — real-time per-band signal bars with peak-hold + heading, for aiming whips by measured signal

### WiFi
- Scan hosts, Sniffer, Channel Analyzer
- Wifi attacks — Evil Portal, Evil WiFi, deauth / jam detect, beacon spam
- Brucegotchi, NetCut, Responder, Reverse Shell, SSH, TelNET, SOCKS4 proxy, TCP client / listen, Wireguard
- Start AP, Connect, Change MAC, WiFi password recovery
- **WiFi Radio ★** — SD-backed synced web radio station (browser player, ~8 listeners)

### Bluetooth `BLE`
- BLE Scan, BLE Sniffer, BLE Spam, Bad BLE, BLE Suite
- BLE Keyboard, Media Cmds, Presenter mode, iBeacon, Ninebot

### 2.4 GHz `NRF24`
- Information, Spectrum, NRF Jammer, MouseJack
- Shared-SPI and legacy SPI module modes

### Infrared `IR`
- IR Read, Custom IR, TV-B-Gone, IR Jammer
- **IR Library ★** — SD-backed appliance code database + browser
- **Intervalometer ★**

### NFC / RFID `I²C → RFID` (PN532)
- Scan / Read tag, Read 125 kHz, Read EMV
- Write / Emulate NDEF, Chameleon, Amiibolink, SRIX tool, Add MIF key
- iButton

### GPS (NEO-6M)
- GPS Tracker, Wardriving (WiFi / BLE / both), Scan WiFi / BLE

### LoRa
- Chat, change frequency / username

### FM
- Broadcast (standard / reserved / stop), FM Spectrum, Hijack TA

### Ethernet (LAN8720 bridge builds)
- Scan hosts, MAC flooding, DHCP starvation

### Others (utilities)
- **Barcodes ★** — Code 128 / UPC-A / EAN-13 / Code 93 / 39 / 11, live display + PNG export to SD
- **QR Codes ★** — generator + saved-codes tab
- **Crypto tracker ★** — top coins, 2×2 grid, 24h sparklines
- **Money tracker ★** — USD vs GBP / EUR / CAD / CNY / JPY / AUD / MXN / INR, trend sparklines
- **Ghost Radar ★** — RF field-strength sweep + optional MMC5983MA magnetometer (true EMF), self-calibrating
- **Resistor Calc ★** — 4 / 5 / 6 band with reversed-entry detection
- **Key Copier ★** — mechanical key bitting visualizer
- **Lock Calc ★** — lock combination calculator
- **VIN Decoder ★** — offline WMI + online NHTSA lookup
- Health Checks — eight network modules (see below)
- BadUSB / HID, USB Keyboard, USB Clicker, USB U2F, Microphone, Record, Spectrum

### Clock
- **Stopwatch ★**, **Countdown ★**, **Timer ★** `99:99:99`, DS3231 RTC

### Files / Scripts
- SD card, LittleFS, USB Mass Storage, send / receive files & commands, script loader, WebUI launch

---

## `// RAD1O & PERIPH3RAL STACK`

| Subsystem | Hardware / Function |
|---|---|
| MCU | ESP32-WROOM-32U |
| Sub-GHz | CC1101 |
| 2.4 GHz RF | NRF24 |
| Wireless networking | WiFi |
| Bluetooth | BLE |
| Infrared | Dedicated TX / RX |
| Positioning | NEO-6M GPS |
| NFC / RFID | PN532 |
| Timekeeping | DS3231 RTC |
| Magnetometer | MMC5983MA — true magnetic EMF channel |
| Storage | SD card |
| Display | 2.4-inch ILI9341, 320×240 |
| Touch | XPT2046 resistive touchscreen |
| Power monitoring | Battery ADC |
| Hardware input | GPIO36 Back button |

A shared SPI architecture keeps the radio and storage peripherals on a tightly packed bus layout, while the display and touch controller use their own assigned interface.

---

## `// H4RDWAR3 MAP`

### Controller

- **MCU:** ESP32-WROOM-32U
- **Memory:** Classic ESP32, no PSRAM
- **Flash:** 4 MB
- **Display:** ILI9341, 320×240, rotation 3
- **Touch controller:** XPT2046

### Pin assignments

| Interface | Signal | GPIO |
|---|---|---:|
| Touch / VSPI | SCLK | 18 |
| Touch / VSPI | MOSI | 23 |
| Touch / VSPI | MISO | 19 |
| Touch / VSPI | CS | 21 |
| Radio / SD SPI | SCK | 14 |
| Radio / SD SPI | MOSI | 13 |
| Radio / SD SPI | MISO | 26 |
| CC1101 | SS | 22 |
| NRF24 | CSN | 27 |
| SD | CS | 15 |
| I²C | SDA | 0 |
| I²C | SCL | 2 |
| Infrared | TX | 25 |
| Infrared | RX | 35 |
| GPS NEO-6M | TX | 34 |
| Battery monitor | ADC | 39 |
| Back button | Input | 36 |

### Flash layout

| Partition | Allocation |
|---|---:|
| Application | 3.69 MB |
| LittleFS | 256 KB |
| Total flash | 4 MB |

Partition configuration: `custom_4Mb_deuce.csv`

---

## `// D3VELOPMENT R0ADMAP`

Current development targets:

- [x] Stopwatch and countdown timer `99:99:99`
- [x] Barcode generation via Code 128, UPC-A, EAN-13, Code 93, Code 39, Code 11
- [x] Live barcode display & PNG export to SD
- [x] IR appliance library with SD-backed code database and browser
- [x] Intervalometer
- [x] VIN decoder — offline WMI lookup & online NHTSA lookup
- [x] Lock combination calculator
- [x] I²C main menu with RFID nested inside
- [x] Max WiFi TX power (20 dBm) & BLE TX power (+9 dBm)
- [x] QR Saved Codes tab
- [x] TPMS reader — Toyota, Ford, Nissan, Schrader/GM (315 / 433.92 MHz)
- [x] Ghost Radar — RF field-strength sweep + optional MMC5983MA magnetometer (true EMF), self-calibrating
- [x] Faraday cage / RF-shielding tester (CC1101 RSSI attenuation + verdict)
- [x] Antenna Plot — unified multi-antenna polar RSSI pattern (CC1101 / WiFi / NRF24, per-antenna toggle, colour-coded overlay, HPBW / F-B / est. gain)
- [x] WiFi Radio — SD-backed synced web radio station (browser player, up to ~8 listeners)
- [x] Resistor color-code calculator — 4/5/6 band with reversed-entry detection
- [x] Crypto tracker — top coins, 2×2 swipe grid with 24h sparklines
- [x] Money tracker — USD vs GBP/EUR/CAD/CNY/JPY/AUD/MXN/INR with trend sparklines
- [x] Key Copier — mechanical key bitting visualizer
- [x] Web USB flasher — browser WebSerial installer (esp-web-tools), auto-published by CI
- [x] Live Aim — real-time per-band signal bars (CC1101 dBm / WiFi dBm / NRF24 carrier %) with peak-hold + heading, for aiming antennas by measured signal

Features are developed incrementally and remain subject to hardware, memory, and firmware constraints.

---

## `// BU1LD & DEPL0YMENT`

### Requirements

- [PlatformIO](https://platformio.org/)
- Compatible ESP32 development environment
- USB connection to the target device

### Compile

```bash
pio run -e Deuce_v1_1
```

### Flash

```bash
pio run -e Deuce_v1_1 -t upload
```

Build artifacts:

```text
.pio/build/Deuce_v1_1/
```

Every push is compiled through the `Deuce CI` GitHub Actions workflow. CI reports flash and RAM usage so partition constraints can be monitored BEFORE firmware is deployed.

### Web flasher

On every `main` build, CI publishes a browser-based **WebSerial installer** (esp-web-tools) to GitHub Pages at `https://xxxtrippy.github.io/The-Deuce/`. Open it in Chrome/Edge on desktop, connect the Deuce over USB, and flash the latest build with one click — no toolchain required. It writes bootloader + partitions + app by region, so LittleFS (API key, saved data) is preserved.

> OTA (WiFi/Bluetooth) updating is not possible on the 4 MB single-slot layout — dual-OTA needs two ~3.8 MB app slots, which do not fit. USB (the web flasher or `pio -t upload`) is the update path.

The legacy `Deuce` environment remains available for reference to the original stock style menu.

---

## `// S3CURITY & PR0JECT ST4TUS`

The Deuce is a personal hardware and firmware project.

- Not a commercial product.
- Not intended for public distribution.
- Network testing is intended for authorized systems and environments.
- API credentials are configured on-device, and must never be committed to source control.

Use the device responsibly, and only against systems you **own** or have explicit permission to test.

---

<p align="center">
  <code>NO BULLSHIT // NO UNNECESSARY OVERHEAD // BUILD YOUR OWN TOOLCHAIN</code>
</p>

<p align="center">
  <sub>Built on the open-source <a href="https://github.com/pr3y/Bruce">Bruce</a> firmware. Independently customized for The Deuce.</sub>
</p>
