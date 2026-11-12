#pragma once
// THE DEUCE v1.1 -- hardware UART tools on the two free GPIOs (32=RX, 33=TX).
//   UART Console : tap a target device's serial/debug header -- auto or manual
//                  baud, live terminal, send typed lines. Great for router/IoT
//                  consoles and bootloaders.
//   Serial Bridge: bridge that same UART to a TCP port over WiFi, so you can
//                  reach a device's console remotely (nc <deuce-ip> <port>).
//
//   *** 3.3 V LOGIC ONLY -- the ESP32 is NOT 5 V tolerant. Use a level shifter
//   or divider on 5 V+ lines and share a common ground, or you will damage the
//   pin/chip. ***
// Others menu.
void deuceUartConsole();
void deuceSerialBridge();
