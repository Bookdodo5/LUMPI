// Direct I2C driver for the BH1750FVI light sensor (E8, no library) and HC-SR04 pulse timing.
// Opcodes, MTreg bit packing, and lux formula: ROHM BH1750FVI datasheet (Rev. C), pp. 2, 5, 10-11.
#include <Arduino.h>
#include <Wire.h>

#include "real_io.h"
#include "settings.h"  // gateway_pins::trig, gateway_pins::echo

namespace {

constexpr uint8_t bh1750_address = 0x23;  // ADDR pin to GND (datasheet p. 10)
constexpr uint8_t cmd_power_on = 0x01;    // wake sensor (p. 5)
constexpr uint8_t cmd_reset = 0x07;       // clear data register (p. 5)
constexpr uint8_t cmd_cont_hres = 0x10;   // continuous H-Resolution mode, 1 lx (p. 5)
constexpr uint8_t cmd_mtreg_high = 0x40;  // 01000_MT[7:5] measurement time high bits (p. 11)
constexpr uint8_t cmd_mtreg_low = 0x60;   // 011_MT[4:0]   measurement time low bits (p. 11)
constexpr uint8_t default_mtreg = 69;     // default sensitivity register, range 31..254 (p. 11)

constexpr unsigned long echo_timeout_us = 30000;  // ~5 m away; longer = nothing there
constexpr unsigned long us_per_cm = 58;           // sound round trip: 58 us per cm

bool bh1750_send(uint8_t opcode) {
    Wire.beginTransmission(bh1750_address);
    Wire.write(opcode);
    return Wire.endTransmission() == 0;
}

// Writes the 8-bit MTreg sensitivity register split across two I2C opcodes (datasheet p. 11).
bool bh1750_set_mtreg(uint8_t mtreg) {
    return bh1750_send(cmd_mtreg_high | (mtreg >> 5)) && bh1750_send(cmd_mtreg_low | (mtreg & 0x1F));
}

}  // namespace

namespace hardware {

bool light_begin() {
    return bh1750_send(cmd_power_on) && bh1750_send(cmd_reset) &&
           bh1750_set_mtreg(default_mtreg) && bh1750_send(cmd_cont_hres);
}

bool light_read(float& lux) {
    if (Wire.requestFrom(bh1750_address, uint8_t(2)) != 2) {
        return false;
    }
    const int high_byte = Wire.read();
    const int low_byte = Wire.read();
    if (high_byte < 0 || low_byte < 0) {
        return false;
    }
    const uint16_t raw = (static_cast<uint16_t>(high_byte) << 8) | static_cast<uint16_t>(low_byte);
    lux = (raw / 1.2f) * (69.0f / default_mtreg);  // datasheet pp. 2, 11
    return true;
}

float read_distance_cm(unsigned long* echo_us) {
    // A 10 us pulse out, then time how long the echo pin stays HIGH.
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
    return us / us_per_cm;  // whole cm, enough for "is someone there"
}

}  // namespace hardware
