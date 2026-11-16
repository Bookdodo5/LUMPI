#pragma once

#include <cstdint>

namespace gateway_sensor_logic {

constexpr uint8_t bh1750_address = 0x23;
constexpr uint8_t bh1750_wrong_address = 0x5C;
constexpr uint8_t oled_address = 0x3C;

inline const char* i2c_description(uint8_t address) {
    if (address == bh1750_address) {
        return "BH1750 light sensor   (OK)";
    }
    if (address == bh1750_wrong_address) {
        return "BH1750 ADDR HIGH      (WRONG: tie ADDR to GND)";
    }
    if (address == oled_address) {
        return "OLED screen           (OK)";
    }
    return "unknown device";
}

inline uint16_t join_bytes(uint8_t highByte, uint8_t lowByte) {
    return (uint16_t(highByte) << 8) | lowByte;
}

inline float bh1750_raw_to_lux(uint16_t raw) {
    return raw / 1.2f;
}

}
