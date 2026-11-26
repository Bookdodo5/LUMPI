#pragma once
// A TINY WEB SERVER: reads one request from a doll so the nest sketch can answer it.
//
// Why not the ESP32's own WebServer library? With a binary body it loses the ?node=1&...
// part, and it cuts audio at the first zero byte. So this reads the request by hand.
// A request from a doll looks like this:
//
//   PUT /v1/chunk?node=1&attempt=7&index=0 HTTP/1.1     <- method, path, ?query
//   Authorization: Bearer <LUMPI_LAN_KEY>                <- headers: only Authorization and
//   Content-Length: 2048                                 <-   Content-Length are kept
//                                                        <- empty line = end of headers
//   <2048 bytes of audio>                                <- body (only /v1/chunk has one)
//
// Anyone on the Wi-Fi can connect, so there are limits: 5 s for the first lines, 3 s more for
// the body, 512 characters per line, 32 header lines, 2048 bytes of body. Over a limit = hang up.
#include <Arduino.h>
#include <WiFi.h>
#include <string_view>

// query::find_value and query::read_number read "a=1&b=2" text. They are plain C++ in their own
// file so the laptop can test them: tests/test_query.cpp.
#include "query.h"

namespace tiny_http {

constexpr size_t max_body = 2048;
constexpr size_t max_line = 512;
constexpr int max_headers = 32;
constexpr uint32_t time_limit_ms = 5000;

struct Request {
    String method;         // "GET", "POST" or "PUT"
    String path;           // "/v1/chunk"
    String query;          // "node=1&attempt=7&index=0"
    String authorization;  // "Bearer <key>"
    uint8_t body[max_body];
    size_t body_length = 0;
};

// One line, without its line ending. false = too long, too slow, or the client left.
inline bool read_line(WiFiClient& client, String& line, uint32_t started_ms) {
    line = "";
    while (millis() - started_ms < time_limit_ms) {
        if (!client.available()) {
            if (!client.connected()) {
                return false;
            }
            delay(1);
            continue;
        }
        const char c = static_cast<char>(client.read());
        if (c == '\n') {
            line.trim();  // removes the '\r' before the '\n'
            return true;
        }
        if (line.length() >= max_line) {
            return false;
        }
        line += c;
    }
    return false;
}

// Reads a whole request. false = broken or over a limit: hang up without answering.
inline bool read_request(WiFiClient& client, Request& request) {
    const uint32_t started_ms = millis();
    client.setTimeout(3);  // seconds, for reading the body

    String line;
    if (!read_line(client, line, started_ms)) {
        return false;
    }
    const int space1 = line.indexOf(' ');
    const int space2 = line.indexOf(' ', space1 + 1);
    if (space1 < 0 || space2 < 0) {
        return false;
    }
    request.method = line.substring(0, space1);
    const String target = line.substring(space1 + 1, space2);  // "/v1/chunk?node=1&..."
    const int question = target.indexOf('?');
    request.path = question < 0 ? target : target.substring(0, question);
    request.query = question < 0 ? "" : target.substring(question + 1);

    long length = 0;
    request.authorization = "";
    for (int count = 0; count < max_headers; ++count) {
        if (!read_line(client, line, started_ms)) {
            return false;
        }
        if (line.isEmpty()) {  // end of headers: read the body
            if (length < 0 || length > static_cast<long>(max_body)) {
                return false;
            }
            request.body_length = client.readBytes(request.body, length);
            return request.body_length == static_cast<size_t>(length);
        }
        const int colon = line.indexOf(':');  // "Name: value"
        if (colon < 0) {
            continue;
        }
        const String name = line.substring(0, colon);
        String value = line.substring(colon + 1);
        value.trim();
        if (name.equalsIgnoreCase("Content-Length")) {
            length = value.toInt();
        } else if (name.equalsIgnoreCase("Authorization")) {
            request.authorization = value;
        }
    }
    return false;  // too many headers
}

inline void reply(WiFiClient& client, int code, const String& text, const char* type = "text/plain") {
    client.printf("HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %u\r\nConnection: close\r\n\r\n",
                  code, code == 200 ? "OK" : "Error", type, static_cast<unsigned>(text.length()));
    client.print(text);
}

// The text after "name=" in the query, or "" when it is not there.
inline String text_arg(const Request& request, const char* name) {
    std::string_view all(request.query.c_str(), request.query.length());
    std::string_view value = query::find_value(all, name);
    return String(value.data(), value.size());
}

// A whole number from the query, or -1 when it is missing or not a whole number.
inline long number_arg(const Request& request, const char* name) {
    std::string_view all(request.query.c_str(), request.query.length());
    uint32_t value = 0;
    if (!query::read_number(query::find_value(all, name), value) || value > 2000000000) {
        return -1;
    }
    return static_cast<long>(value);
}

} // namespace tiny_http
