// HC-SR04 distance sensor on the gateway.
// VCC -> USB 5V, GND -> GND, TRIG -> GPIO18.
// ECHO -> 1k resistor -> GPIO19 -> 2k resistor -> GND.
// Never connect the 5V ECHO output directly to an ESP32 GPIO.
// Upload environment: p2_distance. Monitor: 115200.
// Expected: distance follows a target from 20 cm to over 1 m.
// Guide: https://lumpi-build-guide.vercel.app/gateway/distance
#include <Arduino.h>

#include "gateway_distance_logic.h"
#include "settings.h"

void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(gateway_pins::trig, OUTPUT);
    digitalWrite(gateway_pins::trig, LOW);
    pinMode(gateway_pins::echo, INPUT);

    Serial.println("=== PHASE 2: distance ===");
}

void loop() {
    digitalWrite(gateway_pins::trig, LOW);
    delayMicroseconds(2);
    digitalWrite(gateway_pins::trig, HIGH);
    delayMicroseconds(10);
    digitalWrite(gateway_pins::trig, LOW);

    unsigned long echo_us = pulseIn(
        gateway_pins::echo,
        HIGH,
        gateway_distance_logic::echo_timeout_us
    );

    if (!gateway_distance_logic::has_echo(echo_us)) {
        Serial.println("no echo");
    } else {
        unsigned long distance_cm = gateway_distance_logic::echo_us_to_cm(echo_us);
        Serial.printf("distance = %lu cm\n", distance_cm);
    }

    delay(200);
}
