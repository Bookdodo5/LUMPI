// T04 check: one reading round trip through the message contract, on the laptop.
// Build guide: https://lumpi-build-guide.vercel.app/gateway/rules ("The functions, walked through")
//              https://lumpi-build-guide.vercel.app/reference/messages
//
// No board needed. Run from the project folder:
//   g++ -std=c++17 -Wall -Wextra -I src/common src/test/test_round_trip.cpp -o build/test_round_trip.exe && ./build/test_round_trip.exe
// Each line prints the request a doll would send, the gateway's reply, and PASS or FAIL.
#include <cmath>
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

    // Doll 1 reads attempt 5, passage 250 (up to 999). Doll 2 starts in the middle.
    check(sessions[0].start(1, 5, 250, now),
          "doll 1  POST /v1/start?node=1&attempt=5&passage=250  -> ok");
    check(sessions[1].start(2, 1, 999, now),
          "doll 2  POST /v1/start?node=2&attempt=1&passage=999  -> ok (doll 1 does not block it)");
    check(!sessions[0].start(1, 6, 2, now),
          "doll 1  POST /v1/start?node=1&attempt=6&passage=2  -> 409 (already recording)");

    // Audio pieces up to 8192 bytes. Re-sent piece 0 is ACKed again; stored piece restarts 10 s timer.
    check(sessions[0].add_chunk(1, 5, 0, 8192, now + 6000) == ChunkResult::stored,
          "doll 1  PUT /v1/chunk?node=1&attempt=5&index=0 (8192 B) -> 0 (stored)");
    check(sessions[0].add_chunk(1, 5, 0, 8192, now + 7000) == ChunkResult::already_had_it,
          "doll 1  PUT /v1/chunk?node=1&attempt=5&index=0 -> 0 again (re-send, not saved twice)");
    check(sessions[0].add_chunk(1, 5, 2, 8192, now + 7500) == ChunkResult::rejected,
          "doll 1  PUT /v1/chunk?node=1&attempt=5&index=2 -> rejected (piece 1 missing)");
    check(sessions[0].add_chunk(1, 5, 1, 8192, now + 8000) == ChunkResult::stored,
          "doll 1  PUT /v1/chunk?node=1&attempt=5&index=1 (8192 B) -> 1 (stored)");
    check(sessions[0].add_chunk(2, 1, 0, 8192, now + 8000) == ChunkResult::rejected,
          "doll 2's piece sent to doll 1's referee -> rejected (wrong doll)");

    // Chunk at now + 8000 restarted the 10 s silence timer, so at now + 15000 doll 1 is still recording.
    sessions[0].check_timeouts(now + 15000);
    check(sessions[0].state == State::recording,
          "doll 1 still recording 7 s after last stored chunk (timer restarted per chunk)");

    // Finish: the totals must match exactly.
    check(!sessions[0].finish(1, 5, 3, 24576, now + 15000),
          "doll 1  POST /v1/finish?node=1&attempt=5&chunks=3&bytes=24576 -> 409 totals differ");
    check(sessions[0].finish(1, 5, 2, 16384, now + 15000),
          "doll 1  POST /v1/finish?node=1&attempt=5&chunks=2&bytes=16384 -> assessing");

    // Assessing timeout is now 45 s (not 30 s).
    sessions[0].check_timeouts(now + 15000 + 40000);
    check(sessions[0].state == State::assessing,
          "doll 1 still assessing after 40 s (limit is 45 s)");

    // Gateway -> cloud: POST /v1/assess ... -> "2,1". The gateway stores the command.
    check(sessions[0].got_result(1, 5, retry_word, 1),
          "cloud   POST /v1/assess?node=1&attempt=5&passage=250 -> 2,1 (retry word 1)");

    // The doll polls. The answer is the same every time (idempotent).
    for (int poll = 0; poll < 3; poll++) {
        const bool same = sessions[0].state == State::result && sessions[0].result_code == retry_word &&
                          sessions[0].result_word == 1;
        check(same, "doll 1  GET /v1/attempt?node=1&attempt=5 -> result,2,1");
    }
    check(!sessions[0].owned_by(1, 4),
          "doll 1  GET /v1/attempt?node=1&attempt=4 -> 409 unknown attempt (old reading)");

    // A stored result never times out; a stuck recording does after 10 s with no new chunk.
    sessions[0].check_timeouts(now + 120000);
    check(sessions[0].state == State::result, "later doll 1 still has its result");
    sessions[1].check_timeouts(now + 10001);
    check(sessions[1].state == State::idle, "doll 2 silent for over 10 s while recording -> cancelled");

    // The next reading replaces the old result.
    check(sessions[0].start(1, 6, 3, now) && sessions[0].result_code == 0,
          "doll 1  POST /v1/start?node=1&attempt=6&passage=3 -> ok (old result cleared)");

    // Bad node numbers and bad passages are refused (only nodes 1..2, passages 1..999).
    Session spare;
    check(!spare.start(0, 1, 1, now), "start with node=0 -> refused");
    check(!spare.start(3, 1, 1, now), "start with node=3 -> refused (only 2 dolls)");
    check(!spare.start(1, 1, 0, now), "start with passage=0 -> refused");
    check(!spare.start(1, 1, 1000, now), "start with passage=1000 -> refused");

    // Dark-room hysteresis: dark below 10 lx for 3 s, bright at/above 15 lx for 3 s, NAN = not dark.
    DarkState dark;
    check(!dark.update(8.0f, 10.0f, 15.0f, 3000, 1000), "dark starts timer at 8 lx");
    check(!dark.update(8.0f, 10.0f, 15.0f, 3000, 3999), "still bright before 3 s");
    check(dark.update(8.0f, 10.0f, 15.0f, 3000, 4000), "becomes dark after 3 s below 10 lx");
    check(dark.update(12.0f, 10.0f, 15.0f, 3000, 10000), "12 lx inside hysteresis gap stays dark");
    check(dark.update(15.0f, 10.0f, 15.0f, 3000, 11000), "15 lx starts bright timer");
    check(!dark.update(15.0f, 10.0f, 15.0f, 3000, 14000), "becomes bright after 3 s at 15 lx");
    dark.update(5.0f, 10.0f, 15.0f, 3000, 20000);
    dark.update(5.0f, 10.0f, 15.0f, 3000, 23000);
    check(!dark.update(NAN, 10.0f, 15.0f, 3000, 24000), "missing sensor (NAN) is never dark");
    check(decide_command(accepted, true) == too_dark, "decide_command overrides with 6 when dark");
    check(decide_command(retry_word, false) == retry_word, "decide_command keeps AI code when bright");

    // Doll calls teacher (/v1/help) -> waiting; teacher presses ACK -> coming; next start clears it.
    HelpState help;
    check(help.on_call(5000) && help.answer() == 1,
          "teacher call -> GET /v1/help -> waiting");
    check(!help.on_call(6000), "second poll while waiting does not re-trigger help event");
    uint32_t waited_s = 0;
    check(help.ack(65000, waited_s) && waited_s == 60 && help.answer() == 2,
          "teacher presses ACK after 60 s -> GET /v1/help -> coming");
    help.on_start();
    check(help.answer() == 0, "next reading clears coming");

    std::printf("\n%s: %d failure(s)\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
