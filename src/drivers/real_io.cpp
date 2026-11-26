#include <Arduino.h>
#include <Wire.h>

#include "real_io.h"
#include "settings.h"  // gateway_pins::trig, gateway_pins::echo

namespace {

constexpr uint8_t bh1750_address = 0x23;  // ADDR pin to GND (0x5C when ADDR is high)
constexpr uint8_t continuous_high_resolution = 0x10;

constexpr unsigned long echo_timeout_us = 30000;  // ~5 m away; longer = nothing there
constexpr unsigned long us_per_cm = 58;           // sound: 58 us per cm, round trip (there and back)

}

namespace hardware {

bool light_begin() {
    Wire.beginTransmission(bh1750_address);
    Wire.write(continuous_high_resolution);
    return Wire.endTransmission() == 0;
}

bool light_read(float& lux) {
    int received = Wire.requestFrom(bh1750_address, uint8_t(2));
    if (received != 2) {
        return false;
    }

    int highByte = Wire.read();
    int lowByte = Wire.read();
    if (highByte < 0 || lowByte < 0) {
        return false;
    }

    uint16_t raw = (uint16_t(highByte) << 8) | lowByte;  // two bytes -> one 16-bit count
    lux = raw / 1.2f;                                    // datasheet: 1.2 counts per lux
    return true;
}

float read_distance_cm(unsigned long* echo_us) {
    // a 10 us pulse out, then time how long the echo pin stays HIGH
    digitalWrite(gateway_pins::trig, HIGH);
    delayMicroseconds(10);
    digitalWrite(gateway_pins::trig, LOW);
    const unsigned long us = pulseIn(gateway_pins::echo, HIGH, echo_timeout_us);
    if (echo_us) {
        *echo_us = us;
    }
    if (us == 0) {  // pulseIn gives 0 on timeout
        return NAN;
    }
    return us / us_per_cm;  // ponytail: whole cm (integer divide), same as before; enough for "is someone there"
}

}
