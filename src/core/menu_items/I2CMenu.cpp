#include "I2CMenu.h"

#include "core/display.h"
#include "core/main_menu.h"
#include "core/utils.h"

// The I2C menu groups the I2C-bus tools. For now its one entry is RFID (the PN532 is an
// I2C device), reached here instead of from the top level. The RFID submenu itself is
// unchanged -- we just call into the existing RFIDMenu instance owned by mainMenu.
void I2CMenu::optionsMenu(void) {
    options = {
        {"RFID", [=]() { mainMenu.rfidMenu.optionsMenu(); }},
    };
    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "I2C");
}

// Simple I2C-bus glyph: two horizontal bus lines (SDA/SCL) with device "taps".
void I2CMenu::drawIcon(float scale) {
    clearIconArea();
    uint16_t c = bruceConfig.priColor;
    int half = scale * 30;            // half-width of the bus
    int x0 = iconCenterX - half;
    int x1 = iconCenterX + half;
    int gap = scale * 9;              // vertical gap between the two lines
    int th = scale * 3;              // line thickness
    if (th < 2) th = 2;
    int yTop = iconCenterY - gap;
    int yBot = iconCenterY + gap;

    tft.fillRect(x0, yTop, x1 - x0, th, c); // SCL
    tft.fillRect(x0, yBot, x1 - x0, th, c); // SDA

    // three device taps hanging off the bus
    int r = scale * 4;
    for (int i = 0; i < 3; i++) {
        int dx = x0 + (x1 - x0) * (i + 1) / 4;
        tft.fillRect(dx - th / 2, yTop, th, (yBot - yTop) + th, c); // connect the lines
        tft.fillCircle(dx, yBot + gap, r, c);                       // device node below
    }
}
