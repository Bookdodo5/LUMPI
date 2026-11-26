#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

#include "gateway_cloud_logic.h"

int main() {
    using namespace gateway_cloud_logic;

    assert(room_event_interval_ms == 10000);

    assert(secure_config("mqtts://broker.example:8883", "CA certificate"));
    assert(!secure_config("", "CA certificate"));
    assert(!secure_config("mqtts://broker.example:8883", ""));
    assert(!secure_config("mqtt://broker.example:1883", "CA certificate"));
    assert(!secure_config("https://broker.example", "CA certificate"));

    assert(publish_succeeded(0));
    assert(publish_succeeded(123));
    assert(!publish_succeeded(-1));

    assert(!ready_to_connect(false, 1700000001, false, true));
    assert(!ready_to_connect(true, 1700000000, false, true));
    assert(!ready_to_connect(true, 1700000001, true, true));
    assert(!ready_to_connect(true, 1700000001, false, false));
    assert(ready_to_connect(true, 1700000001, false, true));

    assert(!room_event_due(false, 20000, 0));
    assert(!room_event_due(true, 9999, 0));
    assert(room_event_due(true, 10000, 0));
    assert(room_event_due(true, 5000, 0xFFFFE890));

    assert(safe_identifier("lumpi_1a2b3c4d", 32));
    assert(safe_identifier("ABC-xyz_123", 32));
    assert(!safe_identifier("", 32));
    assert(!safe_identifier("bad/device", 32));
    assert(!safe_identifier("bad\"device", 32));
    assert(!safe_identifier("123456789012345678901234567890123", 32));

    for (int value = 1; value <= 255; value += 1) {
        char character = static_cast<char>(value);
        std::string one_character(1, character);
        bool expected = (character >= 'a' && character <= 'z')
            || (character >= 'A' && character <= 'Z')
            || (character >= '0' && character <= '9')
            || character == '_'
            || character == '-';
        assert(safe_identifier(one_character, 1) == expected);
    }

    char topic[64];
    assert(format_event_topic(topic, sizeof(topic), "lumpi_1a2b3c4d"));
    assert(std::strcmp(topic, "lumpi/lumpi_1a2b3c4d/events") == 0);
    assert(!format_event_topic(nullptr, 0, "lumpi_1a2b3c4d"));
    assert(!format_event_topic(topic, 0, "lumpi_1a2b3c4d"));
    assert(!format_event_topic(topic, sizeof(topic), "bad/device"));

    char small_topic[8];
    assert(!format_event_topic(
        small_topic,
        sizeof(small_topic),
        "lumpi_1a2b3c4d"
    ));

    char event[256];

    assert(format_room_event(
        event,
        sizeof(event),
        "lumpi_1a2b3c4d",
        482913,
        7,
        1700000001,
        230.5f
    ));
    assert(std::strcmp(
        event,
        "{\"id\":\"lumpi_1a2b3c4d/482913/7\",\"body\":{\"kind\":\"room\","
        "\"recorded_at\":1700000001,\"lux\":230.5}}"
    ) == 0);

    assert(format_room_event(
        event,
        sizeof(event),
        "lumpi_1a2b3c4d",
        482913,
        8,
        1700000002,
        NAN
    ));
    assert(std::strcmp(
        event,
        "{\"id\":\"lumpi_1a2b3c4d/482913/8\",\"body\":{\"kind\":\"room\","
        "\"recorded_at\":1700000002,\"lux\":null}}"
    ) == 0);

    char too_small[8];
    assert(!format_room_event(
        too_small,
        sizeof(too_small),
        "lumpi_1a2b3c4d",
        1,
        1,
        1700000003,
        100.0f
    ));

    assert(!format_room_event(
        event,
        0,
        "lumpi_1a2b3c4d",
        1,
        1,
        1700000003,
        100.0f
    ));

    assert(!format_room_event(
        nullptr,
        0,
        "lumpi_1a2b3c4d",
        1,
        1,
        1700000003,
        100.0f
    ));

    assert(format_room_event(
        event,
        sizeof(event),
        "lumpi_1a2b3c4d",
        1,
        2,
        1700000004,
        INFINITY
    ));
    assert(std::strstr(event, "\"lux\":null") != nullptr);

    assert(format_room_event(
        event,
        sizeof(event),
        "lumpi_1a2b3c4d",
        1,
        3,
        1700000005,
        -INFINITY
    ));
    assert(std::strstr(event, "\"lux\":null") != nullptr);

    assert(!format_room_event(
        event,
        sizeof(event),
        "bad\"device",
        1,
        1,
        1700000003,
        100.0f
    ));

    assert(format_recording_start_event(
        event,
        sizeof(event),
        "lumpi_1a2b3c4d",
        482913,
        9,
        1700000010,
        2,
        4,
        "04A1B2C3",
        3,
        3100
    ));
    assert(std::strcmp(
        event,
        "{\"id\":\"lumpi_1a2b3c4d/482913/9\",\"body\":{\"kind\":\"recording_start\","
        "\"recorded_at\":1700000010,\"node\":2,\"attempt\":4,\"book\":\"04A1B2C3\","
        "\"passage\":3,\"fsr\":3100}}"
    ) == 0);

    assert(!format_recording_start_event(
        event,
        sizeof(event),
        "lumpi_1a2b3c4d",
        1,
        1,
        1700000010,
        4,
        1,
        "04A1B2C3",
        1,
        3100
    ));

    assert(!format_recording_start_event(
        nullptr,
        0,
        "lumpi_1a2b3c4d",
        1,
        1,
        1700000010,
        1,
        1,
        "04A1B2C3",
        1,
        3100
    ));

    assert(!format_recording_start_event(
        event,
        0,
        "lumpi_1a2b3c4d",
        1,
        1,
        1700000010,
        1,
        1,
        "04A1B2C3",
        1,
        3100
    ));

    assert(!format_recording_start_event(
        event,
        sizeof(event),
        "bad/device",
        1,
        1,
        1700000010,
        1,
        1,
        "04A1B2C3",
        1,
        3100
    ));

    char small_recording[16];
    assert(!format_recording_start_event(
        small_recording,
        sizeof(small_recording),
        "lumpi_1a2b3c4d",
        1,
        1,
        1700000010,
        1,
        1,
        "04A1B2C3",
        1,
        3100
    ));

    assert(!format_recording_start_event(
        event,
        sizeof(event),
        "lumpi_1a2b3c4d",
        1,
        1,
        1700000010,
        1,
        1,
        "04A1B2C3",
        4,
        3100
    ));

    assert(!format_recording_start_event(
        event,
        sizeof(event),
        "lumpi_1a2b3c4d",
        1,
        1,
        1700000010,
        0,
        1,
        "04A1B2C3",
        1,
        3100
    ));

    assert(!format_recording_start_event(
        event,
        sizeof(event),
        "lumpi_1a2b3c4d",
        1,
        1,
        1700000010,
        1,
        1,
        "04A1B2C3",
        0,
        3100
    ));

    assert(!format_recording_start_event(
        event,
        sizeof(event),
        "lumpi_1a2b3c4d",
        1,
        1,
        1700000010,
        1,
        1,
        "bad/book",
        1,
        3100
    ));

    std::cout << "PASS: gateway cloud event logic\n";
    return 0;
}
