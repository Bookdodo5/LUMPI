// Laptop test for src/common/dashboard_event.h. No ESP32 needed.
// Run from the project folder:
//   g++ -std=c++17 -I src/common tests/test_dashboard_event.cpp -o build/test_dashboard_event && ./build/test_dashboard_event
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

#include "dashboard_event.h"

int main() {
    using namespace dashboard_event;

    char event[256];

    // Normal room event.
    assert(format_room_event(event, sizeof(event), "lumpi_1a2b3c4d", 1794794400, 7, 1794794410, 230.5f, 160.0f));
    assert(std::strcmp(
        event,
        "{\"event_id\":\"lumpi_1a2b3c4d/1794794400-7\",\"node_id\":0,\"event_type\":\"room\","
        "\"recorded_at\":1794794410,\"activity\":null,"
        "\"data\":{\"lux\":230.5,\"distance_cm\":160.0}}"
    ) == 0);

    // Failed sensors become null, never a fake number.
    assert(format_room_event(event, sizeof(event), "lumpi_1a2b3c4d", 1794794400, 8, 1794794420, NAN, NAN));
    assert(std::strstr(event, "\"data\":{\"lux\":null,\"distance_cm\":null}") != nullptr);

    // Too small a buffer fails instead of sending cut-off JSON.
    char tiny[40];
    assert(!format_room_event(tiny, sizeof(tiny), "lumpi_1a2b3c4d", 1794794400, 9, 1794794430, 230.5f, 160.0f));

    // A real start packet from a doll, through the existing parser.
    gateway_start_packet::StartPacket packet;
    gateway_start_packet::ParseResult result = gateway_start_packet::parse(
        "node=2&attempt=1&passage=3&uid=04D5E6F7&fsr=2850",
        packet
    );
    assert(result == gateway_start_packet::ParseResult::ok);
    assert(format_start_event(event, sizeof(event), "lumpi_1a2b3c4d", 1794794400, 10, 1794794440, packet));
    assert(std::strcmp(
        event,
        "{\"event_id\":\"lumpi_1a2b3c4d/1794794400-10\",\"node_id\":2,\"event_type\":\"start\","
        "\"recorded_at\":1794794440,\"activity\":\"book_on\","
        "\"data\":{\"uid\":\"04D5E6F7\",\"passage\":3,\"attempt\":1,\"fsr\":2850}}"
    ) == 0);

    // A UID that could break the JSON is refused.
    result = gateway_start_packet::parse(
        "node=1&attempt=1&passage=1&uid=04\",\"x\":\"1&fsr=3100",
        packet
    );
    assert(result == gateway_start_packet::ParseResult::ok);
    assert(!format_start_event(event, sizeof(event), "lumpi_1a2b3c4d", 1794794400, 11, 1794794450, packet));

    assert(is_hex_uid("04a1b2c3"));
    assert(!is_hex_uid(""));
    assert(!is_hex_uid("04 A1"));
    assert(!is_hex_uid("0123456789ABCDEF01234"));

    // Print the events so they can be checked against the Python server.
    format_room_event(event, sizeof(event), "lumpi_1a2b3c4d", 1794794400, 7, 1794794410, 230.5f, 160.0f);
    std::cout << event << "\n";
    format_room_event(event, sizeof(event), "lumpi_1a2b3c4d", 1794794400, 8, 1794794420, NAN, NAN);
    std::cout << event << "\n";
    gateway_start_packet::parse("node=2&attempt=1&passage=3&uid=04D5E6F7&fsr=2850", packet);
    format_start_event(event, sizeof(event), "lumpi_1a2b3c4d", 1794794400, 10, 1794794440, packet);
    std::cout << event << "\n";

    std::cerr << "dashboard event tests passed\n";
    return 0;
}
