THE DEUCE — custom Bruce board target
======================================
Baked-in pin map for your hardware: classic ESP32-WROOM-32U + 2.4" ILI9341
(XPT2046 resistive touch), SD + CC1101 + NRF24 on a shared second SPI bus,
GPS, IR, and a battery monitor. Flash it and everything comes up on defaults —
no on-device pin menu needed.

WHAT THIS FIXES
  - You flashed the CYD target; its display pins don't match your wiring, so the
    screen stayed dark. This target uses the Marauder/ILI9341 pins you actually
    wired, and bakes in the corrected pins for every other module.

INSTALL (one time)
  1. Copy the whole "boards\deuce" folder from this zip into:
         C:\Bruce\boards\
     You should end up with:
         C:\Bruce\boards\deuce\pins_arduino.h
         C:\Bruce\boards\deuce\interface.cpp
         C:\Bruce\boards\deuce\deuce.ini
     (Bruce auto-includes boards\*\*.ini, so the "Deuce" build target appears.)

BUILD + FLASH  (PowerShell — full steps)
  1. Start menu -> type: PowerShell -> Enter.
  2. Go to the Bruce folder (type, then Enter):
         cd C:\Bruce
  3. Erase old config + build + flash in one go (type, then Enter):
         C:\Users\drave\.platformio\penv\Scripts\pio.exe run -e Deuce -t erase -t upload
     - The erase wipes the stale CYD settings so the new defaults take hold.
     - First build pulls the toolchain (a few minutes). Success looks like:
         ==================== [SUCCESS] ====================
     - If it can't find the port, add:  --upload-port COM#
       (COM number is in Device Manager -> Ports)

  Or in VS Code / PlatformIO: pick "env:Deuce" in the bottom bar, then Upload.
  Do a one-time erase first (PlatformIO: Project Tasks -> Deuce -> Platform ->
  Erase Flash) so the old CYD config is cleared.

BAKED-IN PIN MAP
  Display  : CS17 DC16 RST5 MOSI23 SCLK18 MISO19 BL32            (ILI9341)
  Touch    : XPT2046 on the display bus, CS21
  SD card  : SCK14 MISO26 MOSI13 CS15
  CC1101   : SCK14 MISO26 MOSI13 CS22  GDO0 4
  NRF24    : SCK14 MISO26 MOSI13 CS27  CE 33     (IRQ not used — leave unwired)
  GPS      : module TX -> ESP32 GPIO34  (ESP TX unused)
  IR       : TX 25, RX (TSOP) 35
  Battery  : GPIO39 via 100k/100k 2:1 divider (3.3V=0%, ~4.15V=100%)

NOTES
  - NRF24 IRQ: Bruce's driver is RF24(CE, CSN) only; it never reads IRQ. Don't
    wire it. (Nothing to configure.)
  - Battery %: handled by Bruce's built-in reader via ANALOG_BAT_PIN=39.
  - GROVE/I2C pins are placeholders (0/2); set them in the I2C menu only if you
    ever add an I2C device.
  - To change IR/RF pin choices later, use the IR/RF menus (or edit deuce.ini).
  - This target was assembled from Bruce's own marauder-touch board + your
    wiring. It should compile clean; if the first build throws an error, send me
    the exact message and I'll fix it.
