#pragma once
// THE DEUCE v1.1 -- BLE recon tools (Bluetooth menu).
//   BLE Tracker Detector : scan for AirTag / FindMy, Tile, Samsung SmartTag and
//                          other item trackers; flags ones that persist across
//                          scans (possible following/stalking).
//   BLE GATT Explorer    : connect to a BLE device and enumerate its services,
//                          characteristics, properties and readable values.
void deuceBleTracker();
void deuceBleGatt();
