// HC-SR04 distance sensor, on the gateway.
// VCC -> USB 5V, GND -> GND, TRIG -> GPIO26.
// ECHO -> 1k resistor -> GPIO27 -> 2k resistor -> GND.
// GPIO27 is the junction between the two resistors.
// NEVER connect 5V ECHO straight to an ESP32 GPIO.
// Upload environment: s2c_distance. Monitor: 115200.
// Hold a flat target at 20, 50, 100 cm. Reading should follow the target.
// "No echo" means timeout, not a distance of zero.
// Output: a row every 500 ms: time | echo time in us | distance in cm | verdict.
// distance = echo time x 0.0343 cm/us (speed of sound) / 2 (there and back).
// Guide: https://lumpi-build-guide.vercel.app/gateway/distance
#include <Arduino.h>
#include "settings.h"  // all pins live in config/settings.h
#include "monitor.h"

const int triggerPin = gateway_pins::trig;
const int echoPin = gateway_pins::echo;
int rows = 0;  // table rows printed; the header repeats every 20

// Sends one sound pulse; returns the echo time in microseconds, 0 = no echo.
unsigned long measureEcho() {
    // Tell the sensor to send a sound pulse.
    digitalWrite(triggerPin, LOW);
    delayMicroseconds(2);
    digitalWrite(triggerPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(triggerPin, LOW);

    // Measure the round-trip time. Give up after 30000 microseconds.
    return pulseIn(echoPin, HIGH, 30000);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    pinMode(triggerPin, OUTPUT);
    pinMode(echoPin, INPUT);
    digitalWrite(triggerPin, LOW);

    monitor::title("STAGE 2c: DISTANCE SENSOR",
                   "the HC-SR04 measures how far away a flat object is.",
                   "a target held at 20, 50, 100 cm reads about 20, 50, 100 cm.");
    monitor::section("WIRING (HC-SR04 -> ESP32)");
    monitor::item("VCC", "USB 5V");
    monitor::item("GND", "GND");
    monitor::item("TRIG", "GPIO%d", triggerPin);
    monitor::item("ECHO", "1k resistor -> GPIO%d -> 2k resistor -> GND", echoPin);
    monitor::item("Never", "ECHO straight to a GPIO (it is 5V)");

    monitor::section("STARTUP CHECK (point it at a wall within 1 m)");
    unsigned long first = measureEcho();
    monitor::item("First echo", "%lu us", first);
    if (first == 0) {
        monitor::fail("no echo came back",
                      "check 5V, GND, TRIG, ECHO and both divider resistors; or nothing is in front");
    }
    monitor::section("LIVE (move a flat target nearer and further)");
}

void loop() {
    unsigned long echoTime = measureEcho();
    if (rows % 20 == 0) {
        Serial.println("\n  time s |  echo us | distance | verdict");
        Serial.println("  -------+----------+----------+--------------------------------------");
    }
    rows++;
    if (echoTime == 0) {
        Serial.printf("  %6.1f |        0 |        - | NO ECHO: nothing in range, or check wires\n",
                      millis() / 1000.0);
    } else {
        float distanceCm = echoTime * 0.0343f / 2;
        Serial.printf("  %6.1f | %8lu | %5.1f cm | target seen\n", millis() / 1000.0, echoTime, distanceCm);
    }
    delay(500);
}
