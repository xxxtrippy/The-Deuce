# THE DEUCE — FE4TURE 1NV3NT0RY

Every feature/mode/tool on **The Deuce** (ESP32-WROOM-32U, customized Bruce firmware),
grouped by its on-device menu in menu order.

**★ = custom-built for the Deuce.** Unmarked entries are stock Bruce features.

---

## 📶 WiFi
Connect to WiFi · Start WiFi AP · Turn Off WiFi · WiFi Radio · Scan Hosts ·
Sniffer (`.pcap`) · Channel Analyzer · WiFi Atks · Evil Portal ·
Evil WiFi (add / remove / settings) · Brucegotchi · Jam Detect · NetCut ·
Responder · ReverseShell · SSH · TelNET · SOCKS4 Proxy · Client TCP ·
Listen TCP · Wireguard · Change MAC · WiFi Pass Recovery

- **Device Finder ★** — pick an AP/station and home in on it by live RSSI (warmer/colder)
- **Wake-on-LAN ★** — send a magic packet to a MAC (broadcast + directed subnet)
- **Speed Test ★** — internet download throughput + latency (Cloudflare)
- **NTP Time Sync ★** — set system clock + DS3231 from an internet time server (UTC)
- **TLS Inspector ★** — cert subject/issuer/validity/expiry + SHA-256 fingerprint for host:443
- **HTTP Banner ★** — HTTP status, Server banner, redirects, HSTS/CSP header check
- **DNS Tools ★** — DNS-over-HTTPS lookups (A / AAAA / MX / TXT / NS / PTR)
- **Traceroute ★** *(best-effort)* — ICMP TTL traceroute with per-hop RTT
- **Probe Logger ★** — promiscuous probe-request logger: client MACs + probed SSIDs → `/deuce/probes.csv`

## 🔵 Bluetooth (BLE)
BLE Scan · BLE Sniffer · BLE Spam · Bad BLE · BLE Keyboard · Media Cmds ·
Presenter Mode · iBeacon · BLE Suite · Ninebot

- **Tracker Scan ★** — detect AirTag/FindMy, Tile, Samsung SmartTag; flags tags that persist across scans (anti-stalk)
- **GATT Explorer ★** — connect to a BLE device and walk its services / characteristics / properties / values

## 📻 RF / Sub-GHz (CC1101)
Scan/Copy · Record RAW · Custom SubGhz · Spectrum · RSSI Spectrum ·
SquareWave Spectrum · Spectogram · Listen · Bruteforce · Jammer · TPMS

- **Faraday Test ★** — RF shielding/attenuation tester
- **Antenna Plot ★** — antenna pattern/interference plot
- **Live Aim ★** — on-device live aiming screen: watch bands/interference update as you move the antenna
- **Fox Hunt ★** — RF direction finding: RSSI warmer/colder + per-bearing peak map with arrow to source (CC1101 + magnetometer)
- **Decoder ★** — live Sub-GHz OOK protocol decoder (protocol / preset / bits / key / TE), NEXT/PREV cycles frequency
- **433 Weather ★** — receives Nexus-family 433.92 MHz weather sensors (temp C+F, humidity, channel, id, battery)
- **POCSAG ★** *(experimental)* — pager decoder: CC1101 async 2-FSK → RIC + numeric/alphanumeric messages (512/1200/2400 baud)

## 📡 NRF24 (2.4 GHz)
Information · Spectrum · NRF Jammer · MouseJack

## 🔦 IR (Infrared)
IR Read · Custom IR · TV-B-Gone · IR Jammer

- **IR Library ★** — SD-backed appliance-code database + browser
- **Intervalometer ★** — timed IR shutter trigger for time-lapse
- **AC Remote ★** — universal A/C remote (IRac engine): pick brand, set power/mode/temp/fan, beam full-state frames

## 🛰️ GPS (NEO-6M)
GPS Tracker · Wardriving · Scan WiFi / BLE / Both

- **Waypoint Nav ★** — mark a target, get distance + bearing arrow (haversine)
- **Sun Finder ★** — solar azimuth/elevation, sunrise/sunset (NOAA approximation)
- **Trip Computer ★** — speed / max / trip distance / clock / cabin temp
- **RF Coverage ★** — geotagged CC1101/WiFi/NRF RSSI → `/deuce/rfmap.csv` + live trail
- **Lap Timer ★** — GPS start/finish gate with auto lap splits + best lap
- **Reverse Geo ★** — resolve current position to a place name (BigDataCloud)
- **GPS Time Sync ★** — set system clock + DS3231 from satellite UTC time

## 🔌 I²C → RFID / NFC (PN532)
Scan Tags · Read Tag · Read 125kHz · Read EMV · Write NDEF · Emulate NDEF ·
Erase Data · Load File · Chameleon · Amiibolink · SRIX Tool · Add MIF Key · iButton

- **Access Log ★** — log scanned NFC UIDs to SD with timestamps

## 📁 Files · 📜 Scripts · 🔗 Connect
File manager (SD / LittleFS) · Script interpreter · Send/Recv Cmds · Send/Recv File

## ⏰ Clock
Stopwatch · Countdown · Timer · RTC clock

## 🧰 Others
BadUSB & HID (USB Keyboard · USB Clicker · USB U2F · BadUSB) · Microphone ·
Record · Barcodes · QRCodes · Money · Crypto (prices) · Ghost Radar ·
VIN Decoder · Resistor Calc · Lock Calc · Key Copier

- **Health Checks ★** — read-only network safety suite: WiFi Hygiene · Device Inventory · Service/Port Scan · Rogue DHCP · UPnP Exposure · Duplicate SSIDs · Internet/Latency · Attack Watchdog · Health Report
- **SHT41 ★** — live temp/humidity dashboard (dew point, abs humidity, heat index, graphs)
- **Magnetometer ★** — compass / gaussmeter / metal detector / raw XYZ (MMC5983)
- **Site Survey ★** — geotagged environmental logging → `/deuce/survey.csv`
- **Weather Log ★** — SHT41 logging → `/deuce/weather.csv`
- **Tripwire ★** — magnetometer trip alarm → `/deuce/tripwire.csv`
- **Weather Fcast ★** — IP-geolocated current conditions + 3-day forecast (open-meteo)
- **Scope/Gen ★** — mini ADC oscilloscope + square-wave signal generator
- **Morse ★** — text → Morse, visual + buzzer playback, adjustable WPM
- **TOTP ★** — RFC 6238 authenticator (6-digit, 30s), `/deuce/totp.csv`, live countdown
- **Crypto Tools ★** — MD5/SHA-1/SHA-256, Base64/Hex enc+dec, ROT13, URL enc+dec
- **Random ★** — HW-RNG coin / dice (d4–d100) / number / hex bytes
- **Flashlight ★** — torch / adjustable strobe / SOS beacon
- **Password Gen ★** — strong password generator + entropy/strength meter
- **WiFi QR ★** — build a `WIFI:` join QR from SSID/security/password
- **Battery Log ★** — discharge-curve plot + runtime estimate → `/deuce/battlog.csv`
- **Pocket Bench ★** — voltmeter / logic probe / frequency counter / continuity beeper
- **How To ★** — on-device walkthrough for every Deuce tool

## ⚙️ Config
Device / display / pin settings

---

## Notes for field-testing
Two tools compile clean but need tuning on real hardware (both labelled in-menu):

- **POCSAG** (`*exp*`) — 2-FSK async bit-recovery off GDO0 is board-sensitive; set the CC1101 to your local pager channel and try the 512/1200/2400 baud options.
- **Traceroute** (`*best-effort*`) — uses a raw ICMP socket the network stack may restrict; some hops may show no reply.

For a full `.pcap` of all 802.11 frames (not just probe requests), use **WiFi → Sniffer**.
