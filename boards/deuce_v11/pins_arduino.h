#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include "soc/soc_caps.h"
#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

static const uint8_t TX = 43;
static const uint8_t RX = 44;

static const uint8_t TXD2 = 1;
static const uint8_t RXD2 = 2;

static const uint8_t SDA = 0;
static const uint8_t SCL = 2;

// Modified elsewhere
static const uint8_t SS = -1;
static const uint8_t MOSI = -1;
static const uint8_t MISO = -1;
static const uint8_t SCK = -1;

static const uint8_t G0 = 0;
static const uint8_t G1 = 1;
static const uint8_t G2 = 2;
static const uint8_t G3 = 3;
static const uint8_t G4 = 4;
static const uint8_t G5 = 5;
static const uint8_t G6 = 6;
static const uint8_t G7 = 7;
static const uint8_t G8 = 8;
static const uint8_t G9 = 9;
static const uint8_t G10 = 10;
static const uint8_t G11 = 11;
static const uint8_t G12 = 12;
static const uint8_t G13 = 13;
static const uint8_t G14 = 14;
static const uint8_t G15 = 15;
static const uint8_t G39 = 39;
static const uint8_t G40 = 40;
static const uint8_t G41 = 41;
static const uint8_t G42 = 42;
static const uint8_t G43 = 43;
static const uint8_t G44 = 44;
static const uint8_t G46 = 46;

static const uint8_t ADC1 = 7;
static const uint8_t ADC2 = 8;

#define RGB_LED 21

#define BAD_TX -1
#define BAD_RX -1

// SERIAL (GPS) dedicated pins
#define SERIAL_TX -1
#define SERIAL_RX 34
#define GPS_SERIAL_TX SERIAL_TX
#define GPS_SERIAL_RX SERIAL_RX
#define HAS_BTN 0
#define BTN_ALIAS "\"Ok\""
#define BTN_PIN 0
#define BTN_ACT LOW

#define TXLED 25
#define RXLED 35
#define LED_ON HIGH
#define LED_OFF LOW

#define CC1101_GDO0_PIN 4
#define CC1101_SS_PIN 22
#define CC1101_MOSI_PIN SPI_MOSI_PIN
#define CC1101_SCK_PIN SPI_SCK_PIN
#define CC1101_MISO_PIN SPI_MISO_PIN

#define NRF24_CE_PIN 33
#define NRF24_SS_PIN 27
#define NRF24_MOSI_PIN SPI_MOSI_PIN
#define NRF24_SCK_PIN SPI_SCK_PIN
#define NRF24_MISO_PIN SPI_MISO_PIN

#define FP 1
#define FM 2
#define FG 3

#define HAS_SCREEN 1
#define ROTATION 3
#define MINBRIGHT 160

#define USER_SETUP_LOADED 1
#define ILI9341_DRIVER 1
#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS 17
#define TFT_DC 16
#define TFT_RST 5
#define TFT_BL 32
#define TFT_BACKLIGHT_ON HIGH
#define SMOOTH_FONT 1
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define SPI_TOUCH_FREQUENCY 2500000
#define TOUCH_CS 21
#define HAS_TOUCH 1

#define SDCARD_CS 15
#define SDCARD_SCK 14
#define SDCARD_MISO 26
#define SDCARD_MOSI 13

#define GROVE_SDA 0
#define GROVE_SCL 2

#define SPI_SCK_PIN 14
#define SPI_MISO_PIN 26
#define SPI_MOSI_PIN 13
#define SPI_SS_PIN 22

// --- Battery monitor: 2:1 divider (100k/100k) on GPIO39 ---
#define ANALOG_BAT_PIN 39
#define ANALOG_BAT_MULTIPLIER 2.0f

#endif /* Pins_Arduino_h */
