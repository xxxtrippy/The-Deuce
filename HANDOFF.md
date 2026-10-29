# THE DEUCE — build handoff

Context for picking this build up in a fresh chat.

- **Repo:** `github.com/xxxtrippy/the-deuce` (private)
- **Build env:** `Deuce_v1_1` · **Flash:** `pio run -e Deuce_v1_1 -t upload`
- **CI:** `.github/workflows/deuce_ci.yml` compiles only `Deuce_v1_1` on every push and prints
  flash/RAM as run annotations (notices). Repo is debloated to only the Deuce boards.
- **Hardware:** ESP32-WROOM-32U (classic, **no PSRAM**) · 2.4" ILI9341 320×240 · XPT2046
  resistive touch · `ROTATION 3`.
- **Memory budget:** app partition **3,866,624 B**; firmware ~**3,648,727 B** → **~213 KB
  headroom**; RAM ~29%. Partition `custom_4Mb_deuce.csv` (app `0x3B0000` / LittleFS `0x40000`
  = 256 KB).
- ⚠️ **Next flash reformats LittleFS** (from the repartition) → API key + saved settings wiped;
  re-enter the Claude API key after flashing (on-device Settings or `http://<device-ip>/claude`).

## Last 5 firmware edits/additions (newest first — all CI-green)

1. **VIN Decoder** (Others → VIN Decoder) — `boards/deuce_v11/deuce_vin.{cpp,h}`, wired in
   `OthersMenu.cpp`. Offline: 17-char + check-digit validation, WMI→make (~100 makes), region,
   model year; Online: NHTSA vPIC full decode over WiFi.
2. **Intervalometer** (IR → Intervalometer) — `boards/deuce_v11/deuce_ir_tools.{cpp,h}`, wired in
   `IRMenu.cpp`. Nikon/Canon/Pentax shutter codes + Custom raw; interval + shot count via
   `sendRawCommand()`.
3. **Megalodon removed + QR restructured** — deleted `src/modules/others/tururururu.{cpp,h}` and
   its `OthersMenu` entry; `qrcode_menu.{cpp,h}` is now **Create** + **Saved Codes**; `config.h`
   `qrCodes = {}` (presets cleared).
4. **Stopwatch + Countdown** (Clock submenu) — `boards/deuce_v11/deuce_timers.{cpp,h}` +
   `deuce_seg14.h`; exposed `deuceSeg14At()` / `deuceSeg14Width()` from `interface.cpp`; wired in
   `ClockMenu.cpp`. Big 14-seg `HH:MM:SS`, tap = start/stop, tap-bottom = reset, Back = exit.
5. **Repartition** — `custom_4Mb_deuce.csv`: LittleFS 448 KB→256 KB, app +192 KB (21 KB→213 KB
   headroom), to make room for the feature batch.

## Pinout

| Bus / function | Pins |
|---|---|
| Display (VSPI) | SCLK 18 · MOSI 23 · MISO 19 · CS 17 · DC 16 · RST 5 · BL 32 |
| Touch (XPT2046, on VSPI) | CS 21 |
| Radios + SD (2nd SPI, `sdcardSPI`) | SCK 14 · MOSI 13 · MISO 26 · CC1101 SS 22 / GDO0 4 · NRF24 CSN 27 / CE 33 · SD CS 15 |
| I²C | SDA 0 · SCL 2 (PN532 NFC, DS3231 RTC, Si4713 FM @0x63) |
| IR | TX 25 · RX 35 |
| Single-pin RF | TX 25 · RX 35 |
| Battery ADC | 39 |
| Back button | GPIO36 (active-low, 10k pull-up) |
| Flash switch | GPIO2/SCL grounded → lightning-bolt indicator |

## Modules in `boards/deuce_v11/`

`interface.cpp` (3-page 2×2 swipe grid UI, 14-seg font w/ corner breaks + even gap, matrix rain,
header, boot anim, touch handler, `deuceSeg14At/Width`) · `claude_chat.{cpp,h}` (stateless Claude
API chat) · `health_checks.cpp` (8 tests) · `deuce_timers.{cpp,h}` · `deuce_seg14.h` ·
`deuce_ir_tools.{cpp,h}` · `deuce_vin.{cpp,h}` · `deuce_boot.h` · `pins_arduino.h`

**Gated by `-DDEUCE_GRID_MENU`** in shared files: `main_menu.cpp` (grid hook), `ConfigMenu.cpp`
(Claude AI + rain menu), `FileMenu.cpp` (Scripts), `OthersMenu.cpp` (Health Checks, VIN),
`ClockMenu.cpp`, `IRMenu.cpp`, `config.h`, `webInterface.cpp` (deuce.local + `/claude` key page),
`ble_api.cpp`, `main.cpp`.

## Still pending (not built)

- Lock combo calculator
- Barcodes (Code128 / UPC-A / EAN-13 / Code93 / Code39 / Code11 — screen + SD PNG)
- IR appliance library (SD code DB + browser)
- Sentry-Safe (I²C)
- **LiDAR-emulator — parked** (not viable as described with an IR LED)
