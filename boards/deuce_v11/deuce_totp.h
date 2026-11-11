#pragma once
// THE DEUCE v1.1 -- TOTP authenticator (RFC 6238, SHA-1, 6 digits, 30s).
//   Accounts stored in /deuce/totp.csv as  label,base32secret
//   NOTE: secrets are stored in plaintext on the SD card. Treat the card as
//   a secret. This is a convenience tool, not a hardware security key.
// Others menu.
void deuceTotp();
