#include <cassert>
#include <iostream>

#include "gateway_distance_logic.h"

int main() {
    using namespace gateway_distance_logic;

    assert(!has_echo(0));
    assert(has_echo(1));
    assert(has_echo(1160));

    assert(echo_us_to_cm(0) == 0);
    assert(echo_us_to_cm(1160) == 20);
    assert(echo_us_to_cm(2900) == 50);
    assert(echo_us_to_cm(4060) == 70);
    assert(echo_us_to_cm(5800) == 100);
    assert(echo_us_to_cm(23200) == 400);

    assert(echo_timeout_us == 30000);

    std::cout << "PASS: gateway distance logic\n";
    return 0;
}
