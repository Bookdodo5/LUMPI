// FSR402 pressure sensor, on the doll.
// 3.3V -> FSR -> GPIO35 -> 10k resistor -> GND.
// GPIO35 connects at the junction between the FSR and the resistor.
// Upload environment: pressure. Monitor: 115200.
// Press the FSR or put the book on it. The number should rise.
// Remove the book. The number should fall. Write down both ranges.
// This is an ADC reading (0..4095), not a weight measurement.
// Guide: https://lumpi-build-guide.vercel.app/station/fsr
#include <Arduino.h>

const int pressurePin = 35;

void setup() {
    Serial.begin(115200);
    analogReadResolution(12);
    analogSetPinAttenuation(pressurePin, ADC_11db);
}

void loop() {
    int pressure = analogRead(pressurePin);
    Serial.print("Pressure: ");
    Serial.println(pressure);
    delay(200);
}
