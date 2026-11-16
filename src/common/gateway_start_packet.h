#pragma once

#include <cstdint>
#include <limits>
#include <string_view>

namespace gateway_start_packet {

constexpr uint32_t max_dolls = 3;

struct StartPacket {
    uint32_t node = 0;
    uint32_t attempt = 0;
    uint32_t passage = 0;
    uint32_t fsr = 0;
    std::string_view uid;
};

enum class ParseResult {
    ok,
    missing_value,
    invalid_number,
    bad_node,
    bad_passage
};

inline std::string_view find_value(
    std::string_view query,
    std::string_view wanted_key
) {
    size_t pair_start = 0;

    while (pair_start <= query.size()) {
        size_t pair_end = query.find('&', pair_start);
        if (pair_end == std::string_view::npos) {
            pair_end = query.size();
        }

        std::string_view pair = query.substr(pair_start, pair_end - pair_start);
        size_t equals = pair.find('=');

        if (equals != std::string_view::npos) {
            std::string_view key = pair.substr(0, equals);
            if (key == wanted_key) {
                return pair.substr(equals + 1);
            }
        }

        if (pair_end == query.size()) {
            break;
        }
        pair_start = pair_end + 1;
    }

    return {};
}

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
            return false;
        }

        result = result * 10 + digit;
    }

    number = result;
    return true;
}

inline ParseResult parse(std::string_view query, StartPacket& packet) {
    std::string_view node_text = find_value(query, "node");
    std::string_view attempt_text = find_value(query, "attempt");
    std::string_view passage_text = find_value(query, "passage");
    std::string_view fsr_text = find_value(query, "fsr");
    std::string_view uid = find_value(query, "uid");

    if (node_text.empty() || attempt_text.empty() || passage_text.empty()
        || fsr_text.empty() || uid.empty()) {
        return ParseResult::missing_value;
    }

    StartPacket parsed;
    parsed.uid = uid;

    if (!read_number(node_text, parsed.node)
        || !read_number(attempt_text, parsed.attempt)
        || !read_number(passage_text, parsed.passage)
        || !read_number(fsr_text, parsed.fsr)) {
        return ParseResult::invalid_number;
    }

    if (parsed.node < 1 || parsed.node > max_dolls) {
        return ParseResult::bad_node;
    }

    if (parsed.passage < 1 || parsed.passage > 3) {
        return ParseResult::bad_passage;
    }

    packet = parsed;
    return ParseResult::ok;
}

inline const char* result_text(ParseResult result) {
    switch (result) {
        case ParseResult::ok:
            return "ok";
        case ParseResult::missing_value:
            return "missing value";
        case ParseResult::invalid_number:
            return "invalid number";
        case ParseResult::bad_node:
            return "bad node";
        case ParseResult::bad_passage:
            return "bad passage";
    }

    return "unknown error";
}

}
