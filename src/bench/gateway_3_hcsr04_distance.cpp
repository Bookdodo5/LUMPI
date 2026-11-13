// HC-SR04 distance sensor, on the gateway.
// VCC -> USB 5V, GND -> GND, TRIG -> GPIO18.
// ECHO -> 1k resistor -> GPIO19 -> 2k resistor -> GND.
// GPIO19 is the junction between the two resistors.
// NEVER connect 5V ECHO straight to an ESP32 GPIO.
// Upload environment: distance. Monitor: 115200.
// Hold a flat target at 20, 50, 100 cm. Reading should follow the target.
// "No echo" means timeout, not a distance of zero.
// Guide: https://lumpi-build-guide.vercel.app/gateway/distance
#include <Arduino.h>

const int triggerPin = 18;
const int echoPin = 19;

void setup() {
    Serial.begin(115200);
    pinMode(triggerPin, OUTPUT);
    pinMode(echoPin, INPUT);
    digitalWrite(triggerPin, LOW);
}

void loop() {
    // Tell the sensor to send a sound pulse.
    digitalWrite(triggerPin, LOW);
    delayMicroseconds(2);
    digitalWrite(triggerPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(triggerPin, LOW);

    // Measure the round-trip time. Give up after 30000 microseconds.
    unsigned long echoTime = pulseIn(echoPin, HIGH, 30000);

    if (echoTime == 0) {
        Serial.println("No echo");
    } else {
        float distanceCm = echoTime * 0.0343f / 2;
        Serial.printf("Distance: %.1f cm\n", distanceCm);
    }
    delay(200);
}
