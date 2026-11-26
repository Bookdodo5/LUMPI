// BH1750 light sensor, on the gateway.
// VCC -> 3.3V, GND -> GND, SDA -> GPIO21, SCL -> GPIO22, ADDR -> GND.
// Upload environment: s2b_light. Monitor: 115200.
// Cover it: lux falls. Shine a light: lux rises.
// If it fails, run s2a_i2c_scan first. BH1750 must answer at 0x23.
// Output: a row every 500 ms: time | raw count | lux (= raw / 1.2) | dark or bright,
// compared with room::dark_lux in config/settings.h.
// Guide: https://lumpi-build-guide.vercel.app/gateway/light
#include <Arduino.h>
#include <Wire.h>
#include "settings.h"  // all pins live in config/settings.h
#include "monitor.h"

const int sensorAddress = 0x23;
int rows = 0;  // table rows printed; the header repeats every 20

void setup() {
    Serial.begin(115200);
    delay(500);
    char pass[80];
    snprintf(pass, sizeof(pass), "covered: below %.0f lux (DARK). A lit room: about a few hundred lux.", room::dark_lux);
    monitor::title("STAGE 2b: LIGHT SENSOR", "the BH1750 measures how bright the room is.", pass);
    monitor::section("WIRING (BH1750 -> ESP32)");
    monitor::item("VCC", "3.3V");
    monitor::item("GND", "GND");
    monitor::item("SDA", "GPIO%d", gateway_pins::sda);
    monitor::item("SCL", "GPIO%d", gateway_pins::scl);
    monitor::item("ADDR", "GND  (address 0x23)");
    monitor::item("Dark below", "%.0f lux (room::dark_lux in settings.h)", room::dark_lux);

    Wire.begin(gateway_pins::sda, gateway_pins::scl);
    Wire.beginTransmission(sensorAddress);
    Wire.write(0x10);  // Continuous high-resolution light measurement.
    bool answered = Wire.endTransmission() == 0;

    monitor::section("STARTUP CHECK");
    monitor::item("BH1750 at 0x23", "%s", answered ? "answered" : "no answer");
    if (!answered) {
        monitor::fail("the light sensor did not answer at 0x23",
                      "check VCC, GND, SDA, SCL and ADDR to GND; run s2a_i2c_scan");
    }
    monitor::section("LIVE (cover the sensor, then shine a light on it)");
    delay(180);  // Wait for the first measurement.
}

void loop() {
    if (rows % 20 == 0) {
        Serial.println("\n  time s | raw count |      lux | verdict");
        Serial.println("  -------+-----------+----------+--------------------------");
    }
    rows++;
    int received = Wire.requestFrom(uint8_t(sensorAddress), uint8_t(2));
    if (received == 2) {
        // The sensor sends its 16-bit reading as two bytes.
        int highByte = Wire.read();
        int lowByte = Wire.read();
        int raw = (highByte << 8) | lowByte;
        float lux = raw / 1.2f;
        Serial.printf("  %6.1f | %9d | %8.1f | %s (dark below %.0f)\n", millis() / 1000.0, raw, lux,
                      lux < room::dark_lux ? "DARK  " : "bright", room::dark_lux);
    } else {
        Serial.printf("  %6.1f |         - |        - | NO READING: check wires\n", millis() / 1000.0);
    }
    delay(500);
}
