#pragma once
// DASHBOARD EVENTS: the JSON messages the nest sends to the teacher's dashboard
// (POST <LUMPI_ADAPTER_URL>/api/telemetry). Each function returns one event as text.
//
// Every event has the same outside; only "data" changes:
//
//   {"event_id":"lumpi_a1b2c3d4e5f6/1794794400-7",  <- nest id / nest start time - event number
//    "node_id":1,                                   <- 0 = the nest itself, 1-3 = a doll
//    "event_type":"help",                           <- room, start, status, help or noise
//    "recorded_at":1794794410,                      <- Unix seconds, from the nest's clock
//    "activity":null,                               <- start/status only: what the doll is doing
//    "data":{}}                                     <- the event's own values
//
// The event_id lets the server drop an event it already has (the nest may send one twice).
// The start time is in it so the IDs stay new after the nest restarts and counts from 1 again.
//
// Plain C++ (no Arduino), so the laptop tests it: tests/test_dashboard_event.cpp.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "session.h"

namespace dashboard_event {

// The outside of one event: which nest, which event number, when.
struct Stamp {
    const char* gateway_id;  // "lumpi_" + the nest chip's MAC address
    uint64_t boot_time;      // when the nest's clock first worked (soon after it started), Unix seconds
    uint32_t number;         // 1, 2, 3 ... counted since the nest started
    uint64_t recorded_at;    // now, Unix seconds
};

// The RFID tag's ID goes inside the JSON, so only 0-9 and A-F are allowed: anything else
// could break the JSON. Uppercase only, because the cloud accepts only uppercase.
inline bool is_hex_uid(std::string_view uid) {
    if (uid.empty() || uid.size() > 20) {
        return false;
    }
    for (char c : uid) {
        if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F'))) {
            return false;
        }
    }
    return true;
}

// A sensor value with one decimal, or null when the sensor failed (NAN). Never a made-up number.
inline std::string number_or_null(float value) {
    if (!std::isfinite(value)) {
        return "null";
    }
    char text[24];
    std::snprintf(text, sizeof(text), "%.1f", static_cast<double>(value));
    return text;
}

inline std::string quoted(std::string_view text) {
    return "\"" + std::string(text) + "\"";
}

// The shared outside. activity = nullptr sends null. Returns "" for a node that cannot exist.
inline std::string event(const Stamp& stamp, uint32_t node, const char* type, const char* activity,
                         const std::string& data) {
    if (node > lumpi::max_dolls) {
        return "";
    }
    const std::string id = std::string(stamp.gateway_id) + "/" + std::to_string(stamp.boot_time) + "-" +
                           std::to_string(stamp.number);
    return "{\"event_id\":" + quoted(id) +
           ",\"node_id\":" + std::to_string(node) +
           ",\"event_type\":" + quoted(type) +
           ",\"recorded_at\":" + std::to_string(stamp.recorded_at) +
           ",\"activity\":" + (activity == nullptr ? std::string("null") : quoted(activity)) +
           ",\"data\":{" + data + "}}";
}

// The book part of start and status: "uid":"04A1B2C3","passage":2,"attempt":1
inline std::string book(std::string_view uid, uint32_t passage, uint32_t attempt) {
    return "\"uid\":" + quoted(uid) + ",\"passage\":" + std::to_string(passage) +
           ",\"attempt\":" + std::to_string(attempt);
}

// --- the five events ------------------------------------------------------------------

// The nest's room sensors, every 10 s.
inline std::string room(const Stamp& stamp, float lux, float distance_cm) {
    return event(stamp, 0, "room", nullptr,
                 "\"lux\":" + number_or_null(lux) + ",\"distance_cm\":" + number_or_null(distance_cm));
}

// A doll started a reading. "" when the uid is not uppercase hex.
// Its activity is "book_on": the dashboard's word for "a book is open"; "recording" comes from status.
inline std::string start(const Stamp& stamp, uint32_t node, std::string_view uid, uint32_t passage,
                         uint32_t attempt, uint32_t fsr) {
    if (!is_hex_uid(uid)) {
        return "";
    }
    return event(stamp, node, "start", "book_on", book(uid, passage, attempt) + ",\"fsr\":" + std::to_string(fsr));
}

// A doll's heartbeat, every 10 s. activity: idle, book_on, recording, waiting_ai or feedback.
// uid "" = no book known yet: uid, passage and attempt are sent as null.
inline std::string status(const Stamp& stamp, uint32_t node, const char* activity, std::string_view uid,
                          uint32_t passage, uint32_t attempt, uint32_t fsr) {
    std::string book_part = "\"uid\":null,\"passage\":null,\"attempt\":null";
    if (!uid.empty()) {
        if (!is_hex_uid(uid)) {
            return "";
        }
        book_part = book(uid, passage, attempt);
    }
    return event(stamp, node, "status", activity, book_part + ",\"fsr\":" + std::to_string(fsr));
}

// A reading's grade, 0-100. The cloud AI sends the real ones (with every word and sound in
// "words"); the nest sends this short form only as test data (s6_nest_to_dashboard's mock dolls).
inline std::string score(const Stamp& stamp, uint32_t node, std::string_view uid, uint32_t passage,
                         uint32_t attempt, uint32_t percent) {
    if (!is_hex_uid(uid) || percent > 100) {
        return "";
    }
    return event(stamp, node, "score", "feedback",
                 book(uid, passage, attempt) + ",\"score\":" + std::to_string(percent) + ",\"words\":[]");
}

// A child asked for help.
inline std::string help(const Stamp& stamp, uint32_t node) {
    return event(stamp, node, "help", nullptr, "");
}

// A doll's noise level. db = NAN sends null.
inline std::string noise(const Stamp& stamp, uint32_t node, float db) {
    return event(stamp, node, "noise", nullptr, "\"db\":" + number_or_null(db));
}

// What the dashboard shows a doll doing, from its reading's step.
// "feedback" lasts 10 s after the grade; then the doll is back to book_on (or idle).
constexpr uint32_t feedback_ms = 10000;

inline const char* activity_for(lumpi::State step, bool book_on, uint32_t ms_since_grade) {
    if (step == lumpi::State::recording) {
        return "recording";
    }
    if (step == lumpi::State::assessing) {
        return "waiting_ai";
    }
    if (step == lumpi::State::result && ms_since_grade < feedback_ms) {
        return "feedback";
    }
    return book_on ? "book_on" : "idle";
}

} // namespace dashboard_event
