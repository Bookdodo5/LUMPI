#include <cassert>
#include <iostream>
#include <string_view>

#include "gateway_start_packet.h"

int main() {
    using namespace gateway_start_packet;

    StartPacket packet;

    ParseResult result = parse(
        "node=1&attempt=4&passage=2&uid=04A1B2C3&fsr=3100",
        packet
    );

    assert(result == ParseResult::ok);
    assert(packet.node == 1);
    assert(packet.attempt == 4);
    assert(packet.passage == 2);
    assert(packet.uid == "04A1B2C3");
    assert(packet.fsr == 3100);

    assert(parse(
        "fsr=2500&uid=11223344&passage=1&attempt=9&node=2",
        packet
    ) == ParseResult::ok);
    assert(packet.node == 2);

    assert(parse(
        "node=3&attempt=1&passage=3&uid=SIMULATED&fsr=0",
        packet
    ) == ParseResult::ok);
    assert(packet.node == 3);

    assert(parse(
        "attempt=4&passage=2&uid=04A1B2C3&fsr=3100",
        packet
    ) == ParseResult::missing_value);

    assert(parse(
        "node=one&attempt=4&passage=2&uid=04A1B2C3&fsr=3100",
        packet
    ) == ParseResult::invalid_number);

    assert(parse(
        "node=4&attempt=4&passage=2&uid=04A1B2C3&fsr=3100",
        packet
    ) == ParseResult::bad_node);

    assert(parse(
        "node=1&attempt=4&passage=4&uid=04A1B2C3&fsr=3100",
        packet
    ) == ParseResult::bad_passage);

    assert(parse(
        "node=1&attempt=4294967296&passage=2&uid=04A1B2C3&fsr=3100",
        packet
    ) == ParseResult::invalid_number);

    assert(std::string_view(result_text(ParseResult::bad_node)) == "bad node");

    std::cout << "PASS: gateway start packet parser\n";
    return 0;
}
