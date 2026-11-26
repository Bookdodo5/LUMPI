#pragma once

namespace hardware {

bool light_begin();
bool light_read(float& lux);

// HC-SR04 distance in cm, or NAN when no echo came back (no sensor, or nothing in front).
// The sketch sets pinMode for gateway_pins::trig (OUTPUT) and ::echo (INPUT) in setup().
// echo_us, when given, gets the raw echo time (0 = no echo), for the monitor.
float read_distance_cm(unsigned long* echo_us = nullptr);

}
