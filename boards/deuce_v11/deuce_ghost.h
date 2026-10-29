#pragma once
// THE DEUCE v1.1 -- "Ghost Radar": an RF field-strength / activity detector themed
// as a ghost-hunting EMF tool.
//
// HONEST SCOPE: the Deuce has no magnetometer or EMF coil, so this is NOT a true
// low-frequency (K-II style) EMF meter. It uses the CC1101 to sweep RSSI across
// the 300-928 MHz sub-GHz bands, tracks a per-frequency noise floor, and flags
// energy spikes above it -- a genuine RF-activity detector. Spikes are painted as
// blips on a live sweeping radar, with a K-II-style 0-5 level meter. It reacts to
// real RF in the environment (electronics, transmitters, interference), presented
// in the ghost-hunting idiom. CC1101 required. Lives in the Others menu.
void deuceGhostRadar();
