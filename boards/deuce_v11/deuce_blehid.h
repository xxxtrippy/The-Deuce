#pragma once
// THE DEUCE v1.1 -- BLE HID host (keyboard + Xbox-style controller input).
//
// Acts as a BLE central that bonds to a BLE keyboard and/or an Xbox Series/One S
// (BLE) controller and feeds their input into Bruce's existing input system:
//   - keyboard keystrokes  -> the global KeyStroke (text entry everywhere)
//   - controller D-pad      -> Up/Down/Prev/Next nav flags
//   - A -> Select, B -> Back/Esc
//   - Guide/Xbox button     -> jump to Settings (and remappable shortcuts)
// Because it drives the same global flags as the joystick, every existing menu,
// tool and the selection highlight respond with no per-screen changes.
//
// NOTE: controller report layouts vary by model/firmware; this is tuned for the
// Xbox Series/One S BLE profile and may need field-tuning. Bluetooth-Classic
// controllers (PS4/PS5/Switch Pro) are NOT supported (NimBLE is BLE-only).

#include <Arduino.h>

// cross-menu jump request, set from the BLE callback, consumed by the main loop.
// -1 = none; >=0 = index into the controller shortcut table (see deuce_ctrlmap).
extern volatile int g_ctrlJump;

void deuceBleInputPair();   // Bluetooth menu: scan + bond a keyboard/controller
bool bleHidBonded();        // true if a device is bonded/remembered
void bleHidStart();         // bring up the host + (re)connect bonded device
void bleHidStop();          // tear down the host
bool bleHidConnected();     // true while a device is connected

// Consume a controller jump request (idx from g_ctrlJump). idx 0 = Settings;
// higher indices map to menu tabs (see deuce_ctrlmap). Call from the main loop.
void deuceHandleCtrlJump(int idx);
