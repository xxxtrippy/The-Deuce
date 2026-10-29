#pragma once
// THE DEUCE v1.1 -- live market trackers (need WiFi).
//   Crypto: top coins by market cap (BTC, ETH + the next two non-stablecoins),
//           price + 24h change, via the CoinGecko public API.
//   Money:  USD base vs GBP / CAD / CNY via the Frankfurter (ECB) public API.
// Both auto-refresh; SEL refreshes now, ESC exits. In the Others menu.
void deuceCrypto();
void deuceMoney();
