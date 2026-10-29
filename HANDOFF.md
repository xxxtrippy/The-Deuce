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

1. **I2C menu + max radio power** — new `I2C` main-menu item (`src/core/menu_items/I2CMenu.{cpp,h}`)
   takes the RFID slot; **RFID now lives inside it** (I2C → RFID → Read tag, …). Wired in
   `main_menu.{h,cpp}` (`i2cMenu` replaces `&rfidMenu` in `_menuItems`) and the grid tile list in
   `interface.cpp` (`add("I2C","I2C")`). Also forced radios to max TX: WiFi `esp_wifi_set_max_tx_power(84)`
   + country cap 84 (clamps to the WROOM's ~20 dBm PHY ceiling), applied after `set_country` so it
   isn't re-limited; BLE `NimBLEDevice::setPower(ESP_PWR_LVL_P9)` (+9 dBm, classic-ESP32 max) added to
   the ducky/ninebot/sniffer init paths that defaulted lower (ble_api already set P9).
2. **IR appliance library** (IR → IR Library) — `boards/deuce_v11/deuce_ir_library.{cpp,h}`, wired
   in `IRMenu.cpp`. Universal-remote browser over an SD code DB at `/ir_library.txt` (pipe-delimited
   `Category|Brand|Function|Protocol|Address|Command`, or `...|RAW|<freqHz>|<raw durations>`; `#`
   comments). Browses **Category → Brand → Function** and transmits via Bruce's `sendIRCommand()`
   (NEC/NECext/RC5/RC6/Samsung32/SIRC/Kaseikyo/raw). Writes a small starter DB (common TV power/vol
   codes for Samsung/LG/Sony/Panasonic + a few others) on first run if the file is missing.
3. **Barcode SD PNG export** — `deuce_barcode.cpp`: added a minimal 8-bit grayscale PNG writer
   (zlib *stored* blocks, **streamed one scanline per deflate block** so RAM stays at one row
   buffer — safe on the no-PSRAM WROOM). Writes to `/barcodes/<name>.png` (auto `-N` suffix to
   avoid clobber). New menu entry **Export PNG** (pick type → data → optional filename) and an
   **Export PNG** action on each **Saved Code**. PNG writer host-verified bit-exact against PIL
   (valid file, pixels match the pattern, quiet zone white). scale=3px/module, 90px tall.
4. **Barcodes** (Others → Barcodes) — `boards/deuce_v11/deuce_barcode.{cpp,h}`, wired in
   `OthersMenu.cpp`. Offline 1D generator. Six symbologies: Code128 (auto B/C), UPC-A, EAN-13,
   Code93, Code39, Code11; check digits auto-computed where applicable. Menu = Create /
   Save&Display / Export PNG / Saved Codes. Saved list lives on **SD** at `/barcodes.txt`
   (`name|type|data` per line), add/remove, mirroring the QR menu. Encoders host-verified
   bit-for-bit (EAN-13 vs the 5901234123457 reference, Code39 vs an independent width table,
   Code128 table+mod-103 checksum vs known codewords, Code11 canonical USS widths, Code93 full
   47-pattern table so C/K checks ≥43 index valid patterns).
5. **Lock combo calculator** (Others → Lock Calc) — `boards/deuce_v11/deuce_lock.{cpp,h}`, wired in
   `OthersMenu.cpp`. Offline Master-style 40-position dial recovery: user enters the 3rd number
   (from the shackle-tension "sticking point" test); fixed modular relationship fixes 1st/2nd to
   40 candidates (plus ±1 third-number neighbor blocks for drift). Pure integer math, no network;
   `num_keyboard()` input, `ScrollableTextArea` output.

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
`deuce_ir_tools.{cpp,h}` · `deuce_vin.{cpp,h}` · `deuce_lock.{cpp,h}` ·
`deuce_barcode.{cpp,h}` · `deuce_ir_library.{cpp,h}` · `deuce_boot.h` · `pins_arduino.h`

**Gated by `-DDEUCE_GRID_MENU`** in shared files: `main_menu.cpp` (grid hook), `ConfigMenu.cpp`
(Claude AI + rain menu), `FileMenu.cpp` (Scripts), `OthersMenu.cpp` (Health Checks, VIN),
`ClockMenu.cpp`, `IRMenu.cpp`, `config.h`, `webInterface.cpp` (deuce.local + `/claude` key page),
`ble_api.cpp`, `main.cpp`.

## Still pending (not built)

- Sentry-Safe
- **LiDAR-emulator — parked** (not viable as described with an IR LED)
