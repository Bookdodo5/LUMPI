// Laptop test for src/common/dashboard_event.h. No ESP32 needed.
// Run from the project folder:
//   g++ -std=c++17 -Wall -Wextra -I src/common tests/test_dashboard_event.cpp -o build/test_dashboard_event.exe && ./build/test_dashboard_event.exe
// It also prints one event of each kind; tests/test_dashboard_server.py checks them with the real server code.
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

#include "dashboard_event.h"

using namespace dashboard_event;
using lumpi::State;

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

int main() {
    const Stamp stamp{"lumpi_1a2b3c4d", 1794794400, 7, 1794794410};

    // The full outside, once.
    const std::string room_event = room(stamp, 230.5f, 160.0f);
    assert(room_event ==
           "{\"event_id\":\"lumpi_1a2b3c4d/1794794400-7\",\"node_id\":0,\"event_type\":\"room\","
           "\"recorded_at\":1794794410,\"activity\":null,"
           "\"data\":{\"lux\":230.5,\"distance_cm\":160.0}}");

    // A failed sensor is null, never a made-up number.
    assert(contains(room(stamp, NAN, NAN), "\"data\":{\"lux\":null,\"distance_cm\":null}"));

    // Start: the book, with activity book_on.
    const std::string start_event = start(stamp, 2, "04D5E6F7", 3, 1, 2850);
    assert(contains(start_event, "\"node_id\":2,\"event_type\":\"start\""));
    assert(contains(start_event, "\"activity\":\"book_on\""));
    assert(contains(start_event, "\"data\":{\"uid\":\"04D5E6F7\",\"passage\":3,\"attempt\":1,\"fsr\":2850}}"));

    // UID must be 8-20 uppercase hex characters (the cloud refuses shorter, longer or lowercase).
    assert(start(stamp, 1, "04\",\"x\":\"1", 1, 1, 0).empty());
    assert(start(stamp, 1, "04a1b2c3", 1, 1, 0).empty());
    assert(!is_hex_uid(""));
    assert(!is_hex_uid("04A1B2C"));                 // 7 characters: too short
    assert(is_hex_uid("04A1B2C3"));                 // 8 characters: ok
    assert(!is_hex_uid("0123456789ABCDEF01234"));   // 21 characters: too long

    // Status: with a book, and with no book yet (nulls).
    assert(contains(status(stamp, 1, "recording", "04A1B2C3", 2, 1, 3100),
                    "\"activity\":\"recording\",\"data\":{\"uid\":\"04A1B2C3\",\"passage\":2,\"attempt\":1,\"fsr\":3100}}"));
    assert(contains(status(stamp, 2, "idle", "", 0, 0, 40),
                    "\"data\":{\"uid\":null,\"passage\":null,\"attempt\":null,\"fsr\":40}}"));

    // Help and noise (nodes 1..2).
    assert(contains(help(stamp, 1), "\"node_id\":1,\"event_type\":\"help\""));
    assert(contains(help(stamp, 1), "\"data\":{}}"));
    assert(contains(noise(stamp, 2, 68.5f), "\"data\":{\"db\":68.5}}"));

    // Score (test data form: no words).
    assert(contains(score(stamp, 1, "04A1B2C3", 2, 3, 85),
                    "\"activity\":\"feedback\",\"data\":{\"uid\":\"04A1B2C3\",\"passage\":2,\"attempt\":3,\"score\":85,\"words\":[]}}"));
    assert(score(stamp, 1, "04A1B2C3", 2, 3, 101).empty());

    // Only dolls 1 and 2 exist (max_dolls = 2).
    assert(help(stamp, 3).empty());
    assert(help(stamp, 4).empty());

    // A doll's step + mode -> what the dashboard shows.
    assert(std::string(activity_for(State::idle, false, 0)) == "idle");
    assert(std::string(activity_for(State::idle, true, 0)) == "book_on");
    assert(std::string(activity_for(State::idle, false, 0, true)) == "quiet_study");
    assert(std::string(activity_for(State::idle, true, 0, true)) == "quiet_study");
    assert(std::string(activity_for(State::recording, true, 0)) == "recording");
    assert(std::string(activity_for(State::assessing, true, 0)) == "waiting_ai");
    assert(std::string(activity_for(State::result, true, 500)) == "feedback");
    assert(std::string(activity_for(State::result, true, feedback_ms)) == "book_on");

    // One of each, for the Python check.
    std::cout << room_event << "\n"
              << room(stamp, NAN, NAN) << "\n"
              << start_event << "\n"
              << status(stamp, 1, "recording", "04A1B2C3", 2, 1, 3100) << "\n"
              << status(stamp, 2, "idle", "", 0, 0, 40) << "\n"
              << help(stamp, 1) << "\n"
              << noise(stamp, 2, 68.5f) << "\n"
              << score(stamp, 1, "04A1B2C3", 2, 3, 85) << "\n";
    std::cerr << "dashboard event tests passed\n";
    return 0;
}
