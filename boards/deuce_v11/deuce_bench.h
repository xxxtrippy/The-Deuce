#pragma once
// THE DEUCE v1.1 -- pocket bench instruments.
//   Voltmeter   : DC volts on an ADC pin (0-3.3 V, do NOT exceed 3.3 V)
//   Logic Probe : HIGH / LOW / PULSING with rough duty
//   Frequency   : frequency of a square-ish signal (period via pulseIn)
//   Continuity  : buzzer when the probe pin is shorted to GND
// Complements Scope/Gen. Others menu.
void deuceBench();
