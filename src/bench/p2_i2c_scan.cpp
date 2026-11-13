// Gateway I2C scan.
// BH1750: VCC -> 3.3V, GND -> GND, SDA -> GPIO21, SCL -> GPIO22, ADDR -> GND.
// OLED: VCC -> 3.3V, GND -> GND, SDA -> GPIO21, SCL -> GPIO22.
// Upload environment: p2_i2c_scan. Monitor: 115200.
// Expected: BH1750 at 0x23 and OLED at 0x3C. Exactly 2 devices.
// Guide: https://lumpi-build-guide.vercel.app/gateway/i2c-scan
#include <Arduino.h>
#include <Wire.h>

#include "settings.h"

const char* describe(int address) {
    if (address == 0x23) {
        return "BH1750 light sensor   (OK)";
    }
    if (address == 0x5C) {
        return "BH1750 ADDR HIGH      (WRONG: tie ADDR to GND)";
    }
    if (address == 0x3C) {
        return "OLED screen           (OK)";
    }
    return "unknown device";
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Wire.begin(gateway_pins::sda, gateway_pins::scl);
    Serial.println("=== PHASE 2: I2C scan ===");
}

void loop() {
    int found = 0;

    for (int address = 1; address <= 126; address++) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) {
            Serial.printf("found 0x%02X  %s\n", address, describe(address));
            found++;
        }
    }

    Serial.printf("--- %d device(s). Expected 2.\n\n", found);
    delay(3000);
}
