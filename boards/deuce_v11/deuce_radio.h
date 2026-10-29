#pragma once
// THE DEUCE v1.1 -- WiFi Radio: an SD-backed, synced web "radio station".
//
// The Deuce serves the MP3s in /music off the SD card over HTTP; the listener's
// BROWSER decodes and plays them (the Deuce has no audio hardware and does no
// decoding). It runs as a broadcast: the server owns a single playback clock, so
// every listener (up to ~8, network/SD permitting) hears the same track at the
// same position -- tune in late and you drop into whatever is already playing.
// ID3 title/artist are parsed at scan time. Matrix-themed player page at /radio.
// Reached from the WiFi menu. Requires SD + the WebUI (WiFi).
#include <ESPAsyncWebServer.h>

void deuceRadioConfigure(AsyncWebServer *s); // register /radio* routes (called by configureWebServer)
void deuceWifiRadio();                       // WiFi-menu entry: scan + bring up the WebUI
