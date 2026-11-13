// Optional BME280 spare sensor, on the gateway. Not needed for the main build.
// For a 3.3V-compatible breakout: VCC -> 3.3V, GND -> GND.
// SDA -> GPIO21, SCL -> GPIO22.
// Upload environment: bme280. Monitor: 115200.
// Expect temperature, humidity and pressure. Warm it gently with your hand.
// Tries 0x76 then 0x77. A BMP280 is a different part with no humidity sensor.
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_BME280.h>

Adafruit_BME280 sensor;
bool sensorReady = false;

void setup() {
    Serial.begin(115200);
    Wire.begin(21, 22);

    sensorReady = sensor.begin(0x76, &Wire);
    if (!sensorReady) {
        sensorReady = sensor.begin(0x77, &Wire);
    }
    Serial.println(sensorReady ? "BME280 ready" : "BME280 not found");
}

void loop() {
    if (sensorReady) {
        Serial.printf("Temperature: %.1f C\n", sensor.readTemperature());
        Serial.printf("Humidity: %.1f %%\n", sensor.readHumidity());
        Serial.printf("Pressure: %.1f hPa\n\n", sensor.readPressure() / 100);
    }
    delay(1000);
}
