#include <Arduino.h>
#include <Wire.h>

#include "gateway_sensor_logic.h"
#include "real_io.h"

namespace {

constexpr uint8_t continuous_high_resolution = 0x10;

}

namespace hardware {

bool light_begin() {
    Wire.beginTransmission(gateway_sensor_logic::bh1750_address);
    Wire.write(continuous_high_resolution);
    return Wire.endTransmission() == 0;
}

bool light_read(float& lux) {
    int received = Wire.requestFrom(gateway_sensor_logic::bh1750_address, uint8_t(2));
    if (received != 2) {
        return false;
    }

    int highByte = Wire.read();
    int lowByte = Wire.read();
    if (highByte < 0 || lowByte < 0) {
        return false;
    }

    uint16_t raw = gateway_sensor_logic::join_bytes(highByte, lowByte);
    lux = gateway_sensor_logic::bh1750_raw_to_lux(raw);
    return true;
}

}
