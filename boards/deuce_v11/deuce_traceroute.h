#pragma once
// THE DEUCE v1.1 -- traceroute (ICMP, best-effort).
//   Sends ICMP echoes with increasing TTL and lists the routers that reply with
//   "time exceeded", hop by hop, with per-hop RTT. Uses a raw ICMP socket, which
//   the network stack may restrict -- treat missing hops as filtered/limited.
// WiFi menu.
void deuceTraceroute();
