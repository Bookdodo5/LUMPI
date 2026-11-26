#pragma once

namespace gateway_distance_logic {

constexpr unsigned long echo_timeout_us = 30000;

inline bool has_echo(unsigned long echo_us) {
    return echo_us != 0;
}

inline unsigned long echo_us_to_cm(unsigned long echo_us) {
    return echo_us / 58;
}

}
