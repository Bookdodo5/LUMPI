#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

#include "gateway_sensor_logic.h"

int main() {
    using namespace gateway_sensor_logic;

    assert(std::strcmp(i2c_description(0x23), "BH1750 light sensor   (OK)") == 0);
    assert(std::strcmp(i2c_description(0x3C), "OLED screen           (OK)") == 0);
    assert(std::strcmp(i2c_description(0x5C),
                       "BH1750 ADDR HIGH      (WRONG: tie ADDR to GND)") == 0);
    assert(std::strcmp(i2c_description(0x40), "unknown device") == 0);

    assert(join_bytes(0x12, 0x34) == 0x1234);
    assert(join_bytes(0x00, 0x00) == 0x0000);
    assert(join_bytes(0xFF, 0xFF) == 0xFFFF);

    float lux = bh1750_raw_to_lux(join_bytes(0x01, 0x2C));
    assert(std::fabs(lux - 250.0f) < 0.01f);
    assert(std::fabs(bh1750_raw_to_lux(0) - 0.0f) < 0.01f);

    std::cout << "PASS: gateway sensor logic\n";
    return 0;
}
