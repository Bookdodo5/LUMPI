// I2C scan on the doll.
// Each module: VCC -> 3.3V, GND -> GND, SDA -> GPIO32, SCL -> GPIO33.
// BH1750 ADDR -> GND if connected. Most modules have I2C pull-up resistors.
// Upload environment: doll_i2c. Monitor: 115200.
// Expected: 0x3C for OLED. Only connected parts will appear.
// 0x5C means BH1750 ADDR is HIGH. No devices? Check power and SDA/SCL.
// Do not mix doll and gateway wiring on one board.
// Guide: https://lumpi-build-guide.vercel.app/gateway/i2c-scan
#include <Arduino.h>
#include <Wire.h>

void setup() {
    Serial.begin(115200);
    Wire.begin(32, 33);  // SDA, SCL.
}

void loop() {
    int found = 0;

    for (int address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0) {
            Serial.printf("Found: 0x%02X\n", address);
            found++;
        }
    }

    Serial.printf("Devices found: %d\n\n", found);
    delay(3000);
}
