// THE DEUCE v1.1 -- VIN decoder.
//
// Offline (always): 17-char validation + ISO-3779 check digit, WMI -> manufacturer,
// country/region from the first char, and model year from position 10 (disambiguated by
// position 7). Online (NHTSA vPIC, when WiFi is connected): full make / model / year /
// body / engine / fuel / plant. User chose "both": offline first, NHTSA if reachable.

#include "deuce_vin.h"

#include <Arduino.h>
#include "core/display.h"
#include "core/mykeyboard.h"
#include "core/scrollableTextArea.h"
#include "core/wifi/wifi_common.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <globals.h>

// ---- check-digit transliteration (I,O,Q are not valid VIN chars) ----
static int vinVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    switch (c) {
        case 'A': case 'J': return 1;
        case 'B': case 'K': case 'S': return 2;
        case 'C': case 'L': case 'T': return 3;
        case 'D': case 'M': case 'U': return 4;
        case 'E': case 'N': case 'V': return 5;
        case 'F': case 'W': return 6;
        case 'G': case 'P': case 'X': return 7;
        case 'H': case 'Y': return 8;
        case 'R': case 'Z': return 9;
        default: return -1; // I, O, Q or junk
    }
}

static bool vinCheckDigitOK(const String &v) {
    static const int wt[17] = {8, 7, 6, 5, 4, 3, 2, 10, 0, 9, 8, 7, 6, 5, 4, 3, 2};
    int sum = 0;
    for (int i = 0; i < 17; i++) {
        int val = vinVal(v[i]);
        if (val < 0) return false;
        sum += val * wt[i];
    }
    int chk = sum % 11;
    char expect = (chk == 10) ? 'X' : (char)('0' + chk);
    return v[8] == expect;
}

// ---- model year (position 10, index 9), disambiguated by position 7 being alpha ----
static int vinYear(char code, char pos7) {
    static const char *YC = "ABCDEFGHJKLMNPRSTVWXY123456789"; // 30 codes: 1980..2009
    const char *p = strchr(YC, code);
    if (!p) return 0;
    int year = 1980 + (int)(p - YC);
    bool pos7alpha = (pos7 >= 'A' && pos7 <= 'Z');
    if (pos7alpha && year < 2010) year += 30; // 2010..2039 cycle
    return year;
}

// ---- country / region from the first character ----
static const char *vinCountry(char c) {
    if (c >= '1' && c <= '5') return "North America (USA/CA/MX)";
    if (c == '6' || c == '7') return "Oceania";
    if (c == '8' || c == '9') return "South America";
    if (c >= 'A' && c <= 'C') return "Africa";
    if (c >= 'J' && c <= 'R') return "Asia";
    if (c >= 'S' && c <= 'Z') return "Europe";
    return "Unknown";
}

// ---- a focused WMI -> manufacturer table (common makes). Exact 3-char first. ----
struct Wmi {
    const char *wmi;
    const char *make;
};
static const Wmi WMIS[] = {
    {"1G1", "Chevrolet"},  {"1GC", "Chevrolet"},  {"1GT", "GMC"},        {"1G6", "Cadillac"},
    {"1GM", "Pontiac"},    {"1FA", "Ford"},       {"1FT", "Ford Truck"}, {"1FM", "Ford SUV"},
    {"1FD", "Ford"},       {"1HG", "Honda"},      {"2HG", "Honda"},      {"1HF", "Honda"},
    {"JHM", "Honda"},      {"JH4", "Acura"},      {"19U", "Acura"},      {"1N4", "Nissan"},
    {"1N6", "Nissan"},     {"JN1", "Nissan"},     {"JN8", "Nissan"},     {"5N1", "Nissan"},
    {"4T1", "Toyota"},     {"4T3", "Toyota"},     {"5TD", "Toyota"},     {"5TF", "Toyota"},
    {"JTD", "Toyota"},     {"JTE", "Toyota"},     {"JT2", "Toyota"},     {"2T1", "Toyota"},
    {"JTH", "Lexus"},      {"4T4", "Toyota"},     {"1C3", "Chrysler"},   {"1C4", "Chrysler"},
    {"2C3", "Chrysler"},   {"1J4", "Jeep"},       {"1J8", "Jeep"},       {"1C6", "Ram"},
    {"3C4", "Chrysler"},   {"4S3", "Subaru"},     {"4S4", "Subaru"},     {"JF1", "Subaru"},
    {"JF2", "Subaru"},     {"KMH", "Hyundai"},    {"5NP", "Hyundai"},    {"KM8", "Hyundai"},
    {"KNA", "Kia"},        {"KND", "Kia"},        {"5XY", "Kia"},        {"JM1", "Mazda"},
    {"JM3", "Mazda"},      {"4F2", "Mazda"},      {"WBA", "BMW"},        {"WBS", "BMW M"},
    {"4US", "BMW"},        {"5UX", "BMW"},        {"WBY", "BMW i"},      {"WDB", "Mercedes-Benz"},
    {"WDD", "Mercedes-Benz"}, {"WDC", "Mercedes-Benz"}, {"4JG", "Mercedes-Benz"},
    {"WMW", "MINI"},       {"WVW", "Volkswagen"}, {"WV1", "VW Comm."},   {"WV2", "VW Comm."},
    {"3VW", "Volkswagen"}, {"1VW", "Volkswagen"}, {"WVG", "VW SUV"},     {"WAU", "Audi"},
    {"WA1", "Audi SUV"},   {"TRU", "Audi"},       {"WP0", "Porsche"},    {"WP1", "Porsche SUV"},
    {"YV1", "Volvo"},      {"YV4", "Volvo"},      {"SAJ", "Jaguar"},     {"SAL", "Land Rover"},
    {"SCA", "Rolls-Royce"},{"SCB", "Bentley"},    {"ZFF", "Ferrari"},    {"ZHW", "Lamborghini"},
    {"ZAR", "Alfa Romeo"}, {"ZAM", "Maserati"},   {"VF1", "Renault"},    {"VF3", "Peugeot"},
    {"VF7", "Citroen"},    {"ML3", "Mitsubishi"}, {"JA3", "Mitsubishi"}, {"JA4", "Mitsubishi"},
    {"5YJ", "Tesla"},      {"7SA", "Tesla"},      {"LRW", "Tesla (CN)"}, {"KL", "GM Korea"},
};
static const int WMI_COUNT = sizeof(WMIS) / sizeof(WMIS[0]);

static const char *vinMake(const String &v) {
    char w[4] = {v[0], v[1], v[2], 0};
    for (int i = 0; i < WMI_COUNT; i++)
        if (strncmp(WMIS[i].wmi, w, 3) == 0) return WMIS[i].make;
    // 2-char fallback (e.g. "KL")
    for (int i = 0; i < WMI_COUNT; i++)
        if (strlen(WMIS[i].wmi) == 2 && strncmp(WMIS[i].wmi, w, 2) == 0) return WMIS[i].make;
    return "(unknown WMI)";
}

// ---- NHTSA vPIC online decode (best-effort; appended when WiFi is up) ----
static void vinOnline(const String &vin, String &out) {
    if (WiFi.status() != WL_CONNECTED) {
        if (!wifiConnectMenu()) {
            out += "\n[online] WiFi not connected.\n";
            return;
        }
    }
    String url = "https://vpic.nhtsa.dot.gov/api/vehicles/DecodeVinValues/" + vin + "?format=json";
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient https;
    https.setConnectTimeout(15000);
    https.setTimeout(30000);
    if (!https.begin(client, url)) {
        out += "\n[online] connection failed.\n";
        return;
    }
    https.useHTTP10(true); // reliable body read over TLS (chunked otherwise comes back empty)
    int code = https.GET();
    String resp = https.getString();
    https.end();
    if (code != 200 || resp.length() == 0) {
        out += "\n[online] NHTSA HTTP " + String(code) + ".\n";
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, resp)) {
        out += "\n[online] bad JSON.\n";
        return;
    }
    JsonObject r = doc["Results"][0];
    if (r.isNull()) {
        out += "\n[online] no results.\n";
        return;
    }
    auto add = [&](const char *label, const char *key) {
        const char *val = r[key] | "";
        if (val && strlen(val) > 0) out += String(label) + ": " + val + "\n";
    };
    out += "\n--- NHTSA ---\n";
    add("Year", "ModelYear");
    add("Make", "Make");
    add("Model", "Model");
    add("Trim", "Trim");
    add("Body", "BodyClass");
    add("Cyl", "EngineCylinders");
    add("Disp(L)", "DisplacementL");
    add("Fuel", "FuelTypePrimary");
    add("Drive", "DriveType");
    add("Plant", "PlantCountry");
    add("Mfr", "Manufacturer");
}

void deuceVinDecoder() {
    String vin = keyboard("", 17, "Enter VIN (17):");
    vin.trim();
    vin.toUpperCase();
    if (vin.length() == 0) return;

    String out = "VIN: " + vin + "\n\n--- offline ---\n";
    if (vin.length() != 17) {
        out += "Length " + String(vin.length()) + " (VIN must be 17).\n";
        ScrollableTextArea area("VIN");
        area.fromString(out);
        area.show();
        return;
    }

    bool chk = vinCheckDigitOK(vin);
    out += String("Check digit: ") + (chk ? "VALID" : "INVALID") + "\n";
    out += "WMI: " + vin.substring(0, 3) + " -> " + vinMake(vin) + "\n";
    out += "Region: " + String(vinCountry(vin[0])) + "\n";
    int yr = vinYear(vin[9], vin[6]);
    out += "Model year: " + (yr ? String(yr) : String("?")) + "\n";
    out += "Serial: " + vin.substring(11) + "\n";

    // online (NHTSA) -- offline results are already in `out` if this fails
    vinOnline(vin, out);

    ScrollableTextArea area("VIN");
    area.fromString(out);
    area.show();
}
