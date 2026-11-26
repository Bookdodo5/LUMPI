#include <cassert>
#include <iostream>
#include <string_view>

#include "query.h"

int main() {
    using namespace query;

    // find_value: any order, missing key, key with no value
    std::string_view text = "node=1&attempt=4&passage=2&uid=04A1B2C3&fsr=3100";
    assert(find_value(text, "node") == "1");
    assert(find_value(text, "uid") == "04A1B2C3");
    assert(find_value(text, "fsr") == "3100");
    assert(find_value("fsr=2500&uid=11223344&node=2", "node") == "2");
    assert(find_value(text, "word").empty());
    assert(find_value("", "node").empty());
    assert(find_value("node=&fsr=1", "node").empty());
    assert(find_value("nodes=5&node=3", "node") == "3");  // whole key must match

    // read_number: digits only, fits in 32 bits
    uint32_t number = 0;
    assert(read_number("3100", number) && number == 3100);
    assert(read_number("0", number) && number == 0);
    assert(read_number("4294967295", number) && number == 4294967295u);
    number = 7;
    assert(!read_number("4294967296", number) && number == 7);  // overflow: number left unchanged
    assert(!read_number("one", number));
    assert(!read_number("-1", number));
    assert(!read_number("", number));

    std::cout << "PASS: query find_value + read_number\n";
    return 0;
}
