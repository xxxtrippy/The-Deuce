#include "main_menu.h"
#include "display.h"
#include "utils.h"
#include <globals.h>

// THE DEUCE v1.1 draws its own paged 2x2 touch grid for the main menu instead of
// Bruce's carousel. Only the Deuce_v1_1 env defines DEUCE_GRID_MENU, so every
// other build (including the frozen Deuce v1.0) compiles this file identically to
// stock -- no stub, no hook. The board file (boards/deuce_v11/interface.cpp)
// provides the strong override; this weak no-op is a safety net.
#ifdef DEUCE_GRID_MENU
bool __attribute__((weak)) deuceMainMenuGrid(std::vector<Option> &opts, int &idx) {
    (void)opts;
    (void)idx;
    return false;
}
#endif

MainMenu::MainMenu() {
    _menuItems = {
        &wifiMenu,
        &bleMenu,
        &rfMenu,
        &nrf24Menu,
#if !defined(LITE_VERSION)
        &loraMenu,
#endif
#if defined(FM_SI4713) && !defined(LITE_VERSION)
        &fmMenu,
#endif
        &irMenu,
#if !defined(LITE_VERSION)
        &ethernetMenu,
#endif
        &gpsMenu,
        &rfidMenu,
        &fileMenu,
#if !defined(LITE_VERSION) && !defined(DISABLE_INTERPRETER)
        &scriptsMenu,
#endif
        &clockMenu,
        &othersMenu,
        &configMenu,
    };

    _totalItems = _menuItems.size();
}

MainMenu::~MainMenu() {}

void MainMenu::begin(void) {
    returnToMenu = false;
    options = {};

    std::vector<String> l = bruceConfig.disabledMenus;
    for (int i = 0; i < _totalItems; i++) {
        String itemName = _menuItems[i]->getName();
        if (find(l.begin(), l.end(), itemName) == l.end()) { // If menu item is not disabled
            options.push_back(
                {// selected lambda
                 itemName,
                 [this, i]() { _menuItems[i]->optionsMenu(); },
                 false,                                  // selected = false
                 [](void *menuItem, bool shouldRender) { // render lambda
                     if (!shouldRender) return false;
                     drawMainBorder(false);

                     MenuItemInterface *obj = static_cast<MenuItemInterface *>(menuItem);
                     float scale = float((float)tftWidth / (float)240);
                     if (bruceConfigPins.rotation & 0b01) scale = float((float)tftHeight / (float)135);
                     obj->draw(scale);
#if defined(HAS_TOUCH)
                     TouchFooter();
#endif
                     return true;
                 },
                 _menuItems[i]
                }
            );
        }
    }
    // THE DEUCE v1.1: hand the main menu to the custom touch grid. Every other
    // build (DEUCE_GRID_MENU undefined) runs the stock carousel, unchanged.
#ifdef DEUCE_GRID_MENU
    if (!deuceMainMenuGrid(options, _currentIndex))
        _currentIndex = loopOptions(options, MENU_TYPE_MAIN, "Main Menu", _currentIndex);
#else
    _currentIndex = loopOptions(options, MENU_TYPE_MAIN, "Main Menu", _currentIndex);
#endif
};

/*********************************************************************
**  Function: hideAppsMenu
**  Menu to Hide or show menus
**********************************************************************/

void MainMenu::hideAppsMenu() {
    auto items = this->getItems();
    int index = 0;
RESTART: // using gotos to avoid stackoverflow after many choices
    options.clear();
    for (auto item : items) {
        String label = item->getName();
        std::vector<String> l = bruceConfig.disabledMenus;
        bool enabled = find(l.begin(), l.end(), label) == l.end();
        options.push_back(
            {label,
             [this, label, enabled]() {
                 if (enabled) bruceConfig.addDisabledMenu(label);
                 else bruceConfig.removeDisabledMenu(label);
             },
             enabled}
        );
    }
    options.push_back({"Show All", [=]() { bruceConfig.disabledMenus.clear(); }, true});
    addOptionToMainMenu();
    index = loopOptions(options, index);
    bruceConfig.saveFile();
    if (!returnToMenu) goto RESTART;
}
