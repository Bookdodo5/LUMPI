// Optional BME280 spare sensor, on the gateway. Not needed for the main build.
// For a 3.3V-compatible breakout: VCC -> 3.3V, GND -> GND.
// SDA -> GPIO21, SCL -> GPIO22.
// Upload environment: s2f_bme280_optional. Monitor: 115200.
// Expect temperature, humidity and pressure. Warm it gently with your hand.
// Tries 0x76 then 0x77. A BMP280 is a different part with no humidity sensor.
// Output: a row every second: time | temperature | humidity | pressure | verdict.
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include "settings.h"  // all pins live in config/settings.h
#include "monitor.h"

Adafruit_BME280 sensor;
bool sensorReady = false;
int rows = 0;  // table rows printed; the header repeats every 20

void setup() {
    Serial.begin(115200);
    delay(500);
    monitor::title("STAGE 2f: BME280 (OPTIONAL)",
                   "the spare BME280 reads temperature, humidity and air pressure.",
                   "indoors about 18-28 C, 30-70 %, 950-1050 hPa; your hand warms it.");
    monitor::section("WIRING (BME280 -> ESP32)");
    monitor::item("VCC", "3.3V");
    monitor::item("GND", "GND");
    monitor::item("SDA", "GPIO%d", gateway_pins::sda);
    monitor::item("SCL", "GPIO%d", gateway_pins::scl);

    Wire.begin(gateway_pins::sda, gateway_pins::scl);
    int address = 0x76;
    sensorReady = sensor.begin(0x76, &Wire);
    if (!sensorReady) {
        address = 0x77;
        sensorReady = sensor.begin(0x77, &Wire);
    }
    monitor::section("STARTUP CHECK");
    if (sensorReady) {
        monitor::item("BME280", "answered at 0x%02X", address);
    } else {
        monitor::item("BME280", "no answer at 0x76 or 0x77");
        monitor::fail("no BME280 found",
                      "check VCC, GND, SDA, SCL; a BMP280 (no humidity) will not work here");
    }
    monitor::section("LIVE (a row every second)");
}

void loop() {
    if (sensorReady) {
        if (rows % 20 == 0) {
            Serial.println("\n  time s | temperature | humidity | pressure   | verdict");
            Serial.println("  -------+-------------+----------+------------+-----------------");
        }
        rows++;
        float temperature = sensor.readTemperature();
        float humidity = sensor.readHumidity();
        float pressure = sensor.readPressure() / 100;  // pascal -> hPa
        bool plausible = temperature > -20 && temperature < 60 && pressure > 800 && pressure < 1100;
        Serial.printf("  %6.1f |   %6.1f C  |  %5.1f %% | %6.1f hPa | %s\n", millis() / 1000.0, temperature,
                      humidity, pressure, plausible ? "reading OK" : "ODD: check wires");
    }
    delay(1000);
}
