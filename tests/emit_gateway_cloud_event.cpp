#include <iostream>

#include "gateway_cloud_logic.h"

int main() {
    char event[256];

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

    std::cout << event;
    return 0;
}
