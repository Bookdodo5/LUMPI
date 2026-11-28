#pragma once
// Reads web query text like "node=1&attempt=4". Plain C++ (no Arduino), so the laptop can test it:
// tests/test_query.cpp. The nest uses it through tiny_http.h.

#include <cstdint>
#include <limits>
#include <string_view>

namespace query {

// The text before the first sep; rest moves on past it ("" after the last piece).
inline std::string_view cut(std::string_view& rest, char sep) {
    const size_t at = rest.find(sep);
    const std::string_view head = rest.substr(0, at);
    rest = at == std::string_view::npos ? std::string_view() : rest.substr(at + 1);
    return head;
}

// The next line of a reply, without its "\r".
inline std::string_view next_line(std::string_view& rest) {
    std::string_view line = cut(rest, '\n');
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return line;
}

// The text after "wanted_key=", or "" when the key is not there.
inline std::string_view find_value(std::string_view text, std::string_view wanted_key) {
    while (!text.empty()) {
        const std::string_view pair = cut(text, '&');
        const size_t equals = pair.find('=');
        if (equals != std::string_view::npos && pair.substr(0, equals) == wanted_key) {
            return pair.substr(equals + 1);
        }
    }
    return {};
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
