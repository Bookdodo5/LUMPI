// I2C scan on the doll.
// Each module: VCC -> 3.3V, GND -> GND, SDA -> GPIO32, SCL -> GPIO33.
// BH1750 ADDR -> GND if connected. Most modules have I2C pull-up resistors.
// Upload environment: s1e_i2c_scan. Monitor: 115200.
// Expected: 0x3C for OLED. Only connected parts will appear.
// 0x5C means BH1750 ADDR is HIGH. No devices? Check power and SDA/SCL.
// Do not mix doll and gateway wiring on one board.
// Output: scans every 3 s; prints a row (time | count | addresses + names | verdict)
// when the list changes, and every 15 s anyway.
// Guide: https://lumpi-build-guide.vercel.app/gateway/i2c-scan
#include <Arduino.h>
#include <Wire.h>
#include "settings.h"  // all pins live in config/settings.h
#include "monitor.h"

int rows = 0;
String lastList = "first scan";
unsigned long lastRowAt = 0;

// Plain name for the addresses this project uses.
const char* partName(int address) {
    if (address == 0x3C || address == 0x3D) return "OLED screen";
    if (address == 0x23) return "BH1750 light";
    if (address == 0x5C) return "BH1750, ADDR HIGH";
    if (address == 0x76 || address == 0x77) return "BME280";
    return "unknown part";
}

void setup() {
    Serial.begin(115200);
    delay(500);
    monitor::title("STAGE 1e: I2C SCAN (DOLL BUS)",
                   "which parts answer on the doll's I2C wires.",
                   "0x3C (OLED screen) is listed. Only connected parts appear.");
    monitor::section("WIRING (every I2C module)");
    monitor::item("VCC", "3.3V");
    monitor::item("GND", "GND");
    monitor::item("SDA", "GPIO%d", doll_pins::oled_sda);
    monitor::item("SCL", "GPIO%d", doll_pins::oled_scl);
    Wire.begin(doll_pins::oled_sda, doll_pins::oled_scl);  // SDA, SCL.
    monitor::section("LIVE (scan every 3 s; a row when the list changes, or every 15 s)");
}

void loop() {
    int found = 0;
    bool screenFound = false;
    String list = "";

    for (int address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) {
            char entry[40];
            snprintf(entry, sizeof(entry), "%s0x%02X %s", found > 0 ? ", " : "", address, partName(address));
            list += entry;
            found++;
            if (address == 0x3C) screenFound = true;
        }
    }
    if (found == 0) list = "(none)";

    unsigned long now = millis();
    if (list != lastList || now - lastRowAt >= 15000) {
        const char* verdict = "OK: OLED screen answers";
        if (found == 0) {
            verdict = "NOTHING: check VCC, GND, SDA, SCL";
        } else if (!screenFound) {
            verdict = "no 0x3C: is the OLED connected?";
        }
        if (rows % 20 == 0) {
            Serial.println("\n  time s | count | found                          | verdict");
            Serial.println("  -------+-------+--------------------------------+-------------------------");
        }
        rows++;
        Serial.printf("  %6.1f | %5d | %-30s | %s\n", now / 1000.0, found, list.c_str(), verdict);
        lastList = list;
        lastRowAt = now;
    }
    delay(3000);
}
