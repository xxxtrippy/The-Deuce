#pragma once
// THE DEUCE v1.1 -- WiFi probe-request / client logger.
//   Listens in promiscuous mode across the 2.4 GHz channels and logs the probe
//   requests nearby devices broadcast -- their MAC and the SSIDs they are
//   hunting for. Live list of seen clients + the networks they've probed, with
//   RSSI and hit count, optionally logged to /deuce/probes.csv.
//   (Full .pcap capture is already covered by the WiFi > Sniffer tool.)
// WiFi menu.
void deuceProbeLog();
