#pragma once

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <string_view>

namespace gateway_cloud_logic {

constexpr uint32_t room_event_interval_ms = 10000;

inline bool secure_config(std::string_view uri, std::string_view ca) {
    return uri.rfind("mqtts://", 0) == 0 && !ca.empty();
}

inline bool format_room_event(
    char* output,
    size_t output_size,
    uint64_t recorded_at,
    float lux
) {
    int written = 0;

    if (std::isfinite(lux)) {
        written = std::snprintf(
            output,
            output_size,
            "{\"kind\":\"room\",\"recorded_at\":%llu,\"lux\":%.1f}",
            static_cast<unsigned long long>(recorded_at),
            static_cast<double>(lux)
        );
    } else {
        written = std::snprintf(
            output,
            output_size,
            "{\"kind\":\"room\",\"recorded_at\":%llu,\"lux\":null}",
            static_cast<unsigned long long>(recorded_at)
        );
    }

    return written >= 0 && static_cast<size_t>(written) < output_size;
}

}
