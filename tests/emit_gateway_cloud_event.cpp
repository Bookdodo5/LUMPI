#include <iostream>

#include "gateway_cloud_logic.h"
#include "gateway_start_packet.h"

int main() {
    char event[384];

    bool ready = gateway_cloud_logic::format_room_event(
        event,
        sizeof(event),
        "lumpi_1a2b3c4d",
        482913,
        7,
        1700000001,
        230.5f
    );

    if (!ready) {
        return 1;
    }

    std::cout << event << '\n';

    gateway_start_packet::StartPacket packet;
    gateway_start_packet::ParseResult packet_result = gateway_start_packet::parse(
        "node=2&attempt=4&passage=3&uid=04A1B2C3&fsr=3100",
        packet
    );

    if (packet_result != gateway_start_packet::ParseResult::ok) {
        return 1;
    }

    ready = gateway_cloud_logic::format_recording_start_event(
        event,
        sizeof(event),
        "lumpi_1a2b3c4d",
        482913,
        8,
        1700000010,
        packet.node,
        packet.attempt,
        packet.uid,
        packet.passage,
        packet.fsr
    );

    if (!ready) {
        return 1;
    }

    std::cout << event << '\n';
    return 0;
}
