#include <iostream>

#include "gateway_cloud_logic.h"

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

    ready = gateway_cloud_logic::format_recording_start_event(
        event,
        sizeof(event),
        "lumpi_1a2b3c4d",
        482913,
        8,
        1700000010,
        2,
        4,
        "04A1B2C3",
        3,
        3100
    );

    if (!ready) {
        return 1;
    }

    std::cout << event << '\n';
    return 0;
}
