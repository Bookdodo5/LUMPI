#pragma once

// Build the JSON events the laptop dashboard accepts (docs/dashboard/PRD-dashboard.md, section 3).
// The nest POSTs them to LUMPI_ADAPTER_URL + "/api/telemetry".

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "gateway_start_packet.h"

namespace dashboard_event {

constexpr uint32_t room_event_interval_ms = 10000;

// Writes a number with one decimal, or null when the sensor failed (NAN).
inline bool format_number(char* output, size_t output_size, float value) {
    int written = 0;

    if (std::isfinite(value)) {
        written = std::snprintf(output, output_size, "%.1f", static_cast<double>(value));
    } else {
        written = std::snprintf(output, output_size, "null");
    }

    return written >= 0 && static_cast<size_t>(written) < output_size;
}

// The RFID UID goes into JSON text, so allow hex digits only. Anything else
// could break the JSON or inject extra fields.
inline bool is_hex_uid(std::string_view uid) {
    if (uid.empty() || uid.size() > 20) {
        return false;
    }

    for (char c : uid) {
        bool digit = c >= '0' && c <= '9';
        bool upper = c >= 'A' && c <= 'F';
        bool lower = c >= 'a' && c <= 'f';
        if (!digit && !upper && !lower) {
            return false;
        }
    }

    return true;
}

// Event ID = <gateway id>/<boot time>-<number>.
// The boot time makes IDs unique after a restart; without it the counter
// would start at 1 again and the server would drop the events as duplicates.
inline bool format_room_event(
    char* output,
    size_t output_size,
    const char* gateway_id,
    uint64_t boot_time,
    uint32_t number,
    uint64_t recorded_at,
    float lux,
    float distance_cm
) {
    char lux_text[16];
    char distance_text[16];

    if (!format_number(lux_text, sizeof(lux_text), lux)) {
        return false;
    }
    if (!format_number(distance_text, sizeof(distance_text), distance_cm)) {
        return false;
    }

    int written = std::snprintf(
        output,
        output_size,
        "{\"event_id\":\"%s/%llu-%lu\",\"node_id\":0,\"event_type\":\"room\","
        "\"recorded_at\":%llu,\"activity\":null,"
        "\"data\":{\"lux\":%s,\"distance_cm\":%s}}",
        gateway_id,
        static_cast<unsigned long long>(boot_time),
        static_cast<unsigned long>(number),
        static_cast<unsigned long long>(recorded_at),
        lux_text,
        distance_text
    );

    return written >= 0 && static_cast<size_t>(written) < output_size;
}

// A doll start packet (already checked by gateway_start_packet::parse),
// forwarded to the dashboard. recorded_at is the nest's receive time,
// because the dolls have no clock.
inline bool format_start_event(
    char* output,
    size_t output_size,
    const char* gateway_id,
    uint64_t boot_time,
    uint32_t number,
    uint64_t recorded_at,
    const gateway_start_packet::StartPacket& packet
) {
    if (!is_hex_uid(packet.uid)) {
        return false;
    }

    int written = std::snprintf(
        output,
        output_size,
        "{\"event_id\":\"%s/%llu-%lu\",\"node_id\":%lu,\"event_type\":\"start\","
        "\"recorded_at\":%llu,\"activity\":\"book_on\","
        "\"data\":{\"uid\":\"%.*s\",\"passage\":%lu,\"attempt\":%lu,\"fsr\":%lu}}",
        gateway_id,
        static_cast<unsigned long long>(boot_time),
        static_cast<unsigned long>(number),
        static_cast<unsigned long>(packet.node),
        static_cast<unsigned long long>(recorded_at),
        static_cast<int>(packet.uid.size()),
        packet.uid.data(),
        static_cast<unsigned long>(packet.passage),
        static_cast<unsigned long>(packet.attempt),
        static_cast<unsigned long>(packet.fsr)
    );

    return written >= 0 && static_cast<size_t>(written) < output_size;
}

}
