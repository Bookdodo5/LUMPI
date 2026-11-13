#include <Arduino.h>
#include <Wire.h>

#include "real_io.h"

namespace {

constexpr uint8_t light_address = 0x23;
constexpr uint8_t continuous_high_resolution = 0x10;

}

namespace hardware {

bool light_begin() {
    Wire.beginTransmission(light_address);
    Wire.write(continuous_high_resolution);
    return Wire.endTransmission() == 0;
}

bool light_read(float& lux) {
    int received = Wire.requestFrom(light_address, uint8_t(2));
    if (received != 2) {
        return false;
    }

    int highByte = Wire.read();
    int lowByte = Wire.read();
    if (highByte < 0 || lowByte < 0) {
        return false;
    }

    int raw = (highByte << 8) | lowByte;
    lux = raw / 1.2f;
    return true;
}

}
