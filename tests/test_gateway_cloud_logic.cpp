#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

#include "gateway_cloud_logic.h"

int main() {
    using namespace gateway_cloud_logic;

    assert(room_event_interval_ms == 10000);

    assert(secure_config("mqtts://broker.example:8883", "CA certificate"));
    assert(!secure_config("mqtt://broker.example:1883", "CA certificate"));
    assert(!secure_config("mqtts://broker.example:8883", ""));

    char event[128];

    assert(format_room_event(event, sizeof(event), 1700000001, 230.5f));
    assert(std::strcmp(
        event,
        "{\"kind\":\"room\",\"recorded_at\":1700000001,\"lux\":230.5}"
    ) == 0);

    assert(format_room_event(event, sizeof(event), 1700000002, NAN));
    assert(std::strcmp(
        event,
        "{\"kind\":\"room\",\"recorded_at\":1700000002,\"lux\":null}"
    ) == 0);

    char too_small[8];
    assert(!format_room_event(too_small, sizeof(too_small), 1700000003, 100.0f));

    std::cout << "PASS: gateway cloud event logic\n";
    return 0;
}
