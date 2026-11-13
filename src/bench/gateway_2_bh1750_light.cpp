// BH1750 light sensor, on the gateway.
// VCC -> 3.3V, GND -> GND, SDA -> GPIO21, SCL -> GPIO22, ADDR -> GND.
// Upload environment: light. Monitor: 115200.
// Cover it: lux falls. Shine a light: lux rises.
// If it fails, run gateway_i2c first. BH1750 must answer at 0x23.
// Guide: https://lumpi-build-guide.vercel.app/gateway/light
#include <Arduino.h>
#include <Wire.h>

const int sensorAddress = 0x23;

void setup() {
    Serial.begin(115200);
    Wire.begin(21, 22);

    Wire.beginTransmission(sensorAddress);
    Wire.write(0x10);  // Continuous high-resolution light measurement.
    if (Wire.endTransmission() != 0) {
        Serial.println("BH1750 not found. Check wiring.");
    }
    delay(180);  // Wait for the first measurement.
}

void loop() {
    int received = Wire.requestFrom(uint8_t(sensorAddress), uint8_t(2));
    if (received == 2) {
        // The sensor sends its 16-bit reading as two bytes.
        int highByte = Wire.read();
        int lowByte = Wire.read();
        int raw = (highByte << 8) | lowByte;
        float lux = raw / 1.2f;
        Serial.printf("Light: %.1f lux\n", lux);
    } else {
        Serial.println("Light reading failed");
    }
    delay(500);
}
