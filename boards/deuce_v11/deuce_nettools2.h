#pragma once
// THE DEUCE v1.1 -- online network recon tools (WiFi menu).
//   TLS Inspector : connect to host:443, show cert subject/issuer/validity + SHA-256 fp
//   HTTP Banner   : grab a host's HTTP status line, server banner + security headers
//   DNS Tools     : DNS-over-HTTPS lookups (A / AAAA / MX / TXT / NS / PTR)
void deuceTlsInspect();
void deuceHttpBanner();
void deuceDnsTools();
