#pragma once
// THE DEUCE v1.1 -- controller button -> menu-tab shortcut map.
//   Assign X / Y / LB / RB / View / Menu to jump straight to a top-level menu
//   from the controller. A/B stay Select/Back and Guide stays Settings.
// Button indices match the HID button order (0=A,1=B,2=X,3=Y,4=LB,5=RB,6=View,7=Menu).
// Others menu.

// returns the jump target for a button (0=Settings,1..10 = tabs, -1 = unmapped);
// see deuceHandleCtrlJump() in deuce_blehid for the index meaning.
int ctrlShortcutFor(int button);

void deuceControllerMenu();  // config UI
