// T04 check: one reading round trip through the message contract, on the laptop.
// Build guide: https://lumpi-build-guide.vercel.app/gateway/rules ("The functions, walked through")
//              https://lumpi-build-guide.vercel.app/reference/messages
//
// No board needed. Run from the lumpi-starter folder:
//   "/mnt/c/Program Files/LLVM/bin/clang++.exe" -std=c++17 -Wall -Wextra -I src/common src/test/test_round_trip.cpp -o build/test_round_trip.exe
//   ./build/test_round_trip.exe
// Each line prints the request a doll would send, the gateway's reply, and PASS or FAIL.
#include <cstdio>

#include "messages.h"
#include "session.h"

using namespace lumpi;

static int failures = 0;

static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        failures += 1;
    }
}

int main() {
    Session sessions[max_dolls]; // doll n uses sessions[n - 1]
    uint32_t now = 1000;

    // Doll 1 reads attempt 5, passage 2. Doll 2 starts in the middle.
    check(sessions[0].start(1, 5, 2, now),
          "doll 1  POST /v1/start?node=1&attempt=5&passage=2  -> ok");
    check(sessions[1].start(2, 1, 1, now),
          "doll 2  POST /v1/start?node=2&attempt=1&passage=1  -> ok (doll 1 does not block it)");
    check(!sessions[0].start(1, 6, 2, now),
          "doll 1  POST /v1/start?node=1&attempt=6&passage=2  -> 409 (already recording)");

    // Audio pieces. The doll re-sends piece 0 because our ACK was lost.
    check(sessions[0].add_chunk(1, 5, 0, 2048) == ChunkResult::stored,
          "doll 1  PUT /v1/chunk?node=1&attempt=5&index=0  -> 0 (stored)");
    check(sessions[0].add_chunk(1, 5, 0, 2048) == ChunkResult::already_had_it,
          "doll 1  PUT /v1/chunk?node=1&attempt=5&index=0  -> 0 again (re-send, not saved twice)");
    check(sessions[0].add_chunk(1, 5, 2, 2048) == ChunkResult::rejected,
          "doll 1  PUT /v1/chunk?node=1&attempt=5&index=2  -> rejected (piece 1 missing)");
    check(sessions[0].add_chunk(1, 5, 1, 2048) == ChunkResult::stored,
          "doll 1  PUT /v1/chunk?node=1&attempt=5&index=1  -> 1 (stored)");
    check(sessions[0].add_chunk(2, 1, 0, 2048) == ChunkResult::rejected,
          "doll 2's piece sent to doll 1's referee  -> rejected (wrong doll)");

    // Finish: the totals must match exactly.
    check(!sessions[0].finish(1, 5, 3, 6144, now),
          "doll 1  POST /v1/finish?node=1&attempt=5&chunks=3&bytes=6144  -> 409 totals do not match");
    check(sessions[0].finish(1, 5, 2, 4096, now),
          "doll 1  POST /v1/finish?node=1&attempt=5&chunks=2&bytes=4096  -> assessing");

    // Gateway -> laptop: POST /v1/assess ... -> "2,1". The gateway stores the command.
    check(sessions[0].got_result(1, 5, retry_word, 1),
          "laptop  POST /v1/assess?node=1&attempt=5&passage=2  -> 2,1 (retry word 1)");

    // The doll polls. The answer is the same every time (idempotent).
    for (int poll = 0; poll < 3; poll++) {
        const bool same = sessions[0].state == State::result && sessions[0].result_code == retry_word &&
                          sessions[0].result_word == 1;
        check(same, "doll 1  GET /v1/attempt?node=1&attempt=5  -> result,2,1");
    }
    check(!sessions[0].owned_by(1, 4),
          "doll 1  GET /v1/attempt?node=1&attempt=4  -> 409 unknown attempt (old reading)");

    // A stored result never times out; a stuck recording does.
    sessions[0].check_timeouts(now + 60000);
    check(sessions[0].state == State::result, "60 s later doll 1 still has its result");
    sessions[1].check_timeouts(now + 10001);
    check(sessions[1].state == State::idle, "doll 2 silent for over 10 s while recording -> cancelled");

    // The next reading replaces the old result.
    check(sessions[0].start(1, 6, 3, now) && sessions[0].result_code == 0,
          "doll 1  POST /v1/start?node=1&attempt=6&passage=3  -> ok (old result cleared)");

    // Bad node numbers are refused; the simulated doll 3 is allowed.
    Session spare;
    check(!spare.start(0, 1, 1, now), "start with node=0 -> refused");
    check(!spare.start(4, 1, 1, now), "start with node=4 -> refused");
    check(spare.start(3, 1, 1, now), "start with node=3 (simulated doll) -> ok");

    // Third miss on the same passage -> command 7; the teacher's ACK -> "coming".
    HelpState help;
    help.on_grade(2, false, 1, 0, 3);
    help.on_grade(2, false, 1, 0, 3);
    check(help.on_grade(2, false, 1, 5000, 3) && help.answer() == 1,
          "3rd miss on passage 2 -> command 7, GET /v1/help -> waiting");
    uint32_t waited_s = 0;
    check(help.ack(65000, waited_s) && waited_s == 60 && help.answer() == 2,
          "teacher presses ACK after 60 s -> GET /v1/help -> coming (doll plays code 8)");

    std::printf("\n%s: %d failure(s)\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
