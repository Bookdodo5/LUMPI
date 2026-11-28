#pragma once
// Reads web query text like "node=1&attempt=4". Plain C++ (no Arduino), so the laptop can test it:
// tests/test_query.cpp. The nest uses it through tiny_http.h.

#include <cstdint>
#include <limits>
#include <string_view>

namespace query {

// The text after "wanted_key=", or "" when the key is not there.
inline std::string_view find_value(std::string_view text, std::string_view wanted_key) {
    size_t pair_start = 0;

    while (pair_start <= text.size()) {
        size_t pair_end = text.find('&', pair_start);
        if (pair_end == std::string_view::npos) {
            pair_end = text.size();
        }

        std::string_view pair = text.substr(pair_start, pair_end - pair_start);
        size_t equals = pair.find('=');

        if (equals != std::string_view::npos) {
            std::string_view key = pair.substr(0, equals);
            if (key == wanted_key) {
                return pair.substr(equals + 1);
            }
        }

        if (pair_end == text.size()) {
            break;
        }
        pair_start = pair_end + 1;
    }

    return {};
}

// The next line of a reply, without its "\r\n"; rest moves on past it.
inline std::string_view next_line(std::string_view& rest) {
    const size_t end = rest.find('\n');
    std::string_view line = rest.substr(0, end);
    rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return line;
}

// Digits only -> number. False for "", a non-digit, or a number too big for 32 bits.
inline bool read_number(std::string_view text, uint32_t& number) {
    if (text.empty()) {
        return false;
    }

    uint32_t result = 0;

    for (char character : text) {
        if (character < '0' || character > '9') {
            return false;
        }

        uint32_t digit = static_cast<uint32_t>(character - '0');
        if (result > (std::numeric_limits<uint32_t>::max() - digit) / 10) {
            return false;  // one more digit would overflow
        }

        result = result * 10 + digit;
    }

    number = result;
    return true;
}

}  // namespace query
