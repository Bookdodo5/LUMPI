#pragma once
// Quiet Study noise decision: does the doll need a "too noisy" warning?
// Pure C++ (no Arduino), testable on the laptop like session.h.
//
// The gateway keeps one NoiseState per doll. When /v1/noise arrives,
// call on_noise(). If it returns true, reply "command,5" (too_noisy).

#include <cstdint>

namespace lumpi {

struct NoiseState {
    bool child_present = false;    // updated by /v1/presence
    bool session_active = false;   // true when Session::state != idle
    uint32_t last_warning_ms = 0;
    bool warned_once = false;      // false until the first warning

    static constexpr uint32_t cooldown_ms = 30000;
    static constexpr uint32_t default_threshold = 8000;

    // Returns true when the nest should reply "command,5".
    bool on_noise(uint32_t peak, uint32_t threshold, uint32_t now_ms) {
        if (session_active) return false;      // don't interrupt a reading
        if (!child_present) return false;       // nobody there
        if (peak < threshold) return false;     // quiet enough
        // Cooldown: skip if the last warning was less than cooldown_ms ago.
        if (warned_once && (now_ms - last_warning_ms) < cooldown_ms) return false;
        last_warning_ms = now_ms;
        warned_once = true;
        return true;
    }
};

} // namespace lumpi
