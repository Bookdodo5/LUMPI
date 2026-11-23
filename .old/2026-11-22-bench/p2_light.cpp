// BH1750 light sensor on the gateway.
// VCC -> 3.3V, GND -> GND, SDA -> GPIO21, SCL -> GPIO22, ADDR -> GND.
// Upload environment: p2_light. Monitor: 115200.
// Expected: lux falls when covered and rises under a phone flashlight.
// Run p2_i2c_scan first. The BH1750 must answer at 0x23.
// Guide: https://lumpi-build-guide.vercel.app/gateway/light
#include <Arduino.h>
#include <Wire.h>

#include "real_io.h"
#include "settings.h"

void setup() {
    Serial.begin(115200);
    delay(500);

    Wire.begin(gateway_pins::sda, gateway_pins::scl);
    Serial.println("=== PHASE 2: light sensor ===");

    bool lightReady = hardware::light_begin();
    Serial.println(lightReady ? "light init OK" : "light init FAILED");
}

void loop() {
    float lux = 0.0f;

    if (hardware::light_read(lux)) {
        Serial.printf("lux = %.1f\n", lux);
    } else {
        Serial.println("read failed");
    }

    delay(500);
}
