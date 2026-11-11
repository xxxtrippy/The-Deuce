#pragma once
// THE DEUCE v1.1 -- POCSAG pager decoder  *** EXPERIMENTAL ***
//
// POCSAG is 2-FSK, not the OOK the rest of the Sub-GHz tools use, so this does
// something different: it puts the CC1101 into asynchronous-serial FSK mode
// (raw sliced data out on GDO0), bit-bangs that data line, recovers the bit
// stream by run length, hunts the POCSAG frame-sync word and decodes the
// 32-bit codewords into pager address (RIC) + numeric / alphanumeric message.
//
// This path cannot be validated without a live pager signal, and the async
// bit-recovery timing is sensitive to the board. Treat it as a starting point
// to field-test and tune on real hardware, not a finished decoder.
// RF menu.
void deucePocsag();
