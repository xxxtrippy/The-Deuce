#include "GpsMenu.h"
#include "core/display.h"
#include "core/settings.h"
#include "core/utils.h"
#include "modules/gps/gps_tracker.h"
#include "modules/gps/wardriving.h"
#include <math.h>

#if __has_include("deuce_navtools.h")
#include "deuce_navtools.h"
#define HAVE_DEUCE_NAV 1
#endif
#if __has_include("deuce_logtools.h")
#include "deuce_logtools.h"
#define HAVE_DEUCE_LOG 1
#endif

void GpsMenu::optionsMenu() {
    options = {
        {"Wardriving",  [this]() { wardrivingMenu(); }},
#if !defined(LITE_VERSION)
        {"GPS Tracker", [=]() { GPSTracker(); }       },
#endif
#ifdef HAVE_DEUCE_NAV
        {"Waypoint Nav",  []() { deuceWaypointNav(); }  },
        {"Sun Finder",    []() { deuceSunFinder(); }    },
        {"Trip Computer", []() { deuceTripComputer(); } },
#endif
#ifdef HAVE_DEUCE_LOG
        {"RF Coverage",   []() { deuceRFMap(); }         },
#endif
        {"Config",      [this]() { configMenu(); }    },
    };
    addOptionToMainMenu();

    String txt = "GPS (" + String(bruceConfigPins.gpsBaudrate) + " bps)";
    loopOptions(options, MENU_TYPE_SUBMENU, txt.c_str());
}

void GpsMenu::wardrivingMenu() {
    options = {
        {"Scan WiFi Networks", []() { Wardriving(true, false); }},
        {"Scan BLE Devices",   []() { Wardriving(false, true); }},
        {"Scan Both",          []() { Wardriving(true, true); } },
        {"Back",               [this]() { optionsMenu(); }      },
    };

    loopOptions(options, MENU_TYPE_SUBMENU, "Wardriving");
}
void GpsMenu::configMenu() {
    options = {
        {"Baudrate", setGpsBaudrateMenu                                 },
        {"GPS Pins", [=]() { setUARTPinsMenu(bruceConfigPins.gps_bus); }},
        {"Back",     [this]() { optionsMenu(); }                        },
    };

    loopOptions(options, MENU_TYPE_SUBMENU, "GPS Config");
}

void GpsMenu::drawIcon(float scale) {
    clearIconArea();
    int radius = scale * 18;
    if (radius % 2 != 0) radius++;

    int tangentX = sqrt(radius * radius - (radius / 2 * radius / 2));
    int32_t tangentY = radius / 2;

    tft.fillCircle(iconCenterX, iconCenterY - radius / 2, radius, bruceConfig.priColor);
    tft.fillTriangle(
        iconCenterX - tangentX,
        iconCenterY - radius / 2 + tangentY,
        iconCenterX + tangentX,
        iconCenterY - radius / 2 + tangentY,
        iconCenterX,
        iconCenterY + 1.5 * radius,
        bruceConfig.priColor
    );
    tft.fillCircle(iconCenterX, iconCenterY - radius / 2, radius / 2, bruceConfig.bgColor);

    tft.drawEllipse(iconCenterX, iconCenterY + 1.5 * radius, 1.5 * radius, radius / 2, bruceConfig.priColor);
}
