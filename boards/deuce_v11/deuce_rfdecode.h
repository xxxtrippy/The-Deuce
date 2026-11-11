#pragma once
// THE DEUCE v1.1 -- live Sub-GHz protocol decoder.
//   Rides the stock RMT OOK capture engine + Flipper-style protocol registry.
//   Shows the decoded protocol / preset / bit length / key / TE of the last
//   captured frame, plus a running capture count. NEXT cycles frequency.
// RF menu.
void deuceSubghzDecoder();
