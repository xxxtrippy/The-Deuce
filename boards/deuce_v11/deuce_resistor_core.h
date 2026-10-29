#pragma once
// THE DEUCE v1.1 -- resistor color-code decode core (pure, host-testable).
//
// Colors (index): 0 Black 1 Brown 2 Red 3 Orange 4 Yellow 5 Green 6 Blue
// 7 Violet 8 Grey 9 White 10 Gold 11 Silver 12 None(tolerance only).
// Supports 4/5/6 bands and detects a reversed (backwards) reading: if the
// sequence doesn't parse forward but parses when reversed, it was read from the
// wrong end. Values follow the standard EIA color code.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct ResResult {
    bool valid;
    bool backwards;
    double ohms;
    double tolPct;
    bool haveTol;
    int tempco; // ppm/K
    bool haveTempco;
};

static inline int res_digit(int c) { return (c >= 0 && c <= 9) ? c : -1; }

static inline double res_mult(int c) {
    if (c >= 0 && c <= 9) return pow(10.0, c);
    if (c == 10) return 0.1;  // gold
    if (c == 11) return 0.01; // silver
    return NAN;
}

static inline double res_tol(int c) {
    switch (c) {
        case 1: return 1.0;    // brown
        case 2: return 2.0;    // red
        case 5: return 0.5;    // green
        case 6: return 0.25;   // blue
        case 7: return 0.1;    // violet
        case 8: return 0.05;   // grey
        case 10: return 5.0;   // gold
        case 11: return 10.0;  // silver
        case 12: return 20.0;  // none
        default: return NAN;
    }
}

static inline int res_tempco(int c) {
    switch (c) {
        case 0: return 250; // black
        case 1: return 100; // brown
        case 2: return 50;  // red
        case 3: return 15;  // orange
        case 4: return 25;  // yellow
        case 5: return 20;  // green
        case 6: return 10;  // blue
        case 7: return 5;   // violet
        case 8: return 1;   // grey
        default: return -1;
    }
}

// strict forward parse of an n-band sequence
static inline bool res_parse_fwd(const int *s, int n, ResResult *o) {
    o->valid = false;
    o->backwards = false;
    o->haveTol = false;
    o->haveTempco = false;
    o->tempco = 0;
    int nd = (n == 4) ? 2 : 3;
    long digits = 0;
    for (int i = 0; i < nd; i++) {
        int d = res_digit(s[i]);
        if (d < 0) return false;
        digits = digits * 10 + d;
    }
    double m = res_mult(s[nd]);
    if (isnan(m)) return false;
    double tp = res_tol(s[nd + 1]);
    if (isnan(tp)) return false;
    o->ohms = (double)digits * m;
    o->tolPct = tp;
    o->haveTol = true;
    if (n == 6) {
        int tc = res_tempco(s[nd + 2]);
        if (tc < 0) return false;
        o->tempco = tc;
        o->haveTempco = true;
    }
    o->valid = true;
    return true;
}

// Evaluate: forward if it parses, else reversed (flagged backwards), else invalid.
static inline ResResult res_eval(const int *seq, int n) {
    ResResult o;
    if (res_parse_fwd(seq, n, &o)) {
        o.backwards = false;
        return o;
    }
    int r[6];
    for (int i = 0; i < n; i++) r[i] = seq[n - 1 - i];
    ResResult o2;
    if (res_parse_fwd(r, n, &o2)) {
        o2.backwards = true;
        return o2;
    }
    ResResult bad;
    bad.valid = false;
    bad.backwards = false;
    bad.haveTol = false;
    bad.haveTempco = false;
    return bad;
}

// Format resistance with an engineering unit into buf (e.g. "4.7 kOhm").
static inline void res_format(double ohms, char *buf, int buflen) {
    const char *unit;
    double v;
    if (ohms >= 1e9) {
        v = ohms / 1e9;
        unit = "GOhm";
    } else if (ohms >= 1e6) {
        v = ohms / 1e6;
        unit = "MOhm";
    } else if (ohms >= 1e3) {
        v = ohms / 1e3;
        unit = "kOhm";
    } else if (ohms >= 1.0 || ohms == 0.0) {
        v = ohms;
        unit = "Ohm";
    } else {
        v = ohms * 1000.0;
        unit = "mOhm";
    }
    // up to 3 decimals, trimmed
    char num[32];
    snprintf(num, sizeof(num), "%.3f", v);
    int len = (int)strlen(num);
    while (len > 0 && num[len - 1] == '0') num[--len] = 0;
    if (len > 0 && num[len - 1] == '.') num[--len] = 0;
    snprintf(buf, buflen, "%s %s", num, unit);
}
