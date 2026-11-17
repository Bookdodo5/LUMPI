#pragma once

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <string_view>

namespace gateway_cloud_logic {

constexpr uint32_t room_event_interval_ms = 10000;
constexpr uint64_t valid_clock_after = 1700000000;

inline bool secure_config(std::string_view uri, std::string_view ca) {
    return uri.rfind("mqtts://", 0) == 0 && !ca.empty();
}

inline bool ready_to_connect(
    bool wifi_connected,
    uint64_t unix_time,
    bool client_started,
    bool config_is_secure
) {
    return wifi_connected
        && unix_time > valid_clock_after
        && !client_started
        && config_is_secure;
}

inline bool room_event_due(
    bool mqtt_connected,
    uint32_t now_ms,
    uint32_t last_event_ms
) {
    return mqtt_connected
        && static_cast<uint32_t>(now_ms - last_event_ms) >= room_event_interval_ms;
}

inline bool safe_identifier(std::string_view text, size_t maximum_size) {
    if (text.empty() || text.size() > maximum_size) {
        return false;
    }

    for (char character : text) {
        bool safe_character = (character >= 'a' && character <= 'z')
            || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9')
            || character == '_'
            || character == '-';

        if (!safe_character) {
            return false;
        }
    }

    return true;
}

inline bool format_room_event(
    char* output,
    size_t output_size,
    std::string_view device_id,
    uint32_t boot_number,
    uint32_t event_number,
    uint64_t recorded_at,
    float lux
) {
    if (!safe_identifier(device_id, 32)) {
        return false;
    }

    int written = 0;

    if (std::isfinite(lux)) {
        written = std::snprintf(
            output,
            output_size,
            "{\"id\":\"%.*s/%lu/%lu\",\"body\":{\"kind\":\"room\","
            "\"recorded_at\":%llu,\"lux\":%.1f}}",
            static_cast<int>(device_id.size()),
            device_id.data(),
            static_cast<unsigned long>(boot_number),
            static_cast<unsigned long>(event_number),
            static_cast<unsigned long long>(recorded_at),
            static_cast<double>(lux)
        );
    } else {
        written = std::snprintf(
            output,
            output_size,
            "{\"id\":\"%.*s/%lu/%lu\",\"body\":{\"kind\":\"room\","
            "\"recorded_at\":%llu,\"lux\":null}}",
            static_cast<int>(device_id.size()),
            device_id.data(),
            static_cast<unsigned long>(boot_number),
            static_cast<unsigned long>(event_number),
            static_cast<unsigned long long>(recorded_at)
        );
    }

    return written >= 0 && static_cast<size_t>(written) < output_size;
}

inline bool format_recording_start_event(
    char* output,
    size_t output_size,
    std::string_view device_id,
    uint32_t boot_number,
    uint32_t event_number,
    uint64_t recorded_at,
    uint32_t node,
    uint32_t attempt,
    std::string_view book,
    uint32_t passage,
    uint32_t fsr
) {
    if (!safe_identifier(device_id, 32) || !safe_identifier(book, 32)) {
        return false;
    }
    if (node < 1 || node > 3 || passage < 1 || passage > 3) {
        return false;
    }

    int written = std::snprintf(
        output,
        output_size,
        "{\"id\":\"%.*s/%lu/%lu\",\"body\":{\"kind\":\"recording_start\","
        "\"recorded_at\":%llu,\"node\":%lu,\"attempt\":%lu,\"book\":\"%.*s\","
        "\"passage\":%lu,\"fsr\":%lu}}",
        static_cast<int>(device_id.size()),
        device_id.data(),
        static_cast<unsigned long>(boot_number),
        static_cast<unsigned long>(event_number),
        static_cast<unsigned long long>(recorded_at),
        static_cast<unsigned long>(node),
        static_cast<unsigned long>(attempt),
        static_cast<int>(book.size()),
        book.data(),
        static_cast<unsigned long>(passage),
        static_cast<unsigned long>(fsr)
    );

    return written >= 0 && static_cast<size_t>(written) < output_size;
}

}
