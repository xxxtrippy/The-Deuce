#ifndef __I2C_MENU_H__
#define __I2C_MENU_H__

#include <MenuItemInterface.h>

// THE DEUCE v1.1: "I2C" main-menu item. Takes the slot the RFID menu used to hold;
// RFID now lives inside it (I2C -> RFID -> Read tag, ...). Other I2C-bus tools can be
// added here later (the bus is SDA 0 / SCL 2: PN532, DS3231, Si4713).
class I2CMenu : public MenuItemInterface {
public:
    I2CMenu() : MenuItemInterface("I2C") {}

    void optionsMenu(void);
    void drawIcon(float scale);
    bool hasTheme() { return false; }
    const String &themePath() override {
        static const String empty = "";
        return empty;
    }
};

#endif
