// Laptop tests for src/common/doll_logic.h. No ESP32 needed.
// Run from the project folder:
//   g++ -std=c++17 -Wall -Wextra -I src/common tests/test_doll_logic.cpp -o build/test_doll_logic.exe && ./build/test_doll_logic.exe
#include <cmath>
#include <cstdio>
#include <cstring>

#include "doll_logic.h"

using namespace doll_logic;

static int failures = 0;
static int logged_changes = 0;

static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures += 1;
}

static void count_logger(const char*, uint8_t) {
    logged_changes += 1;
}

int main() {
    // 1. Parse "pending" and full S/W/P reply.
    {
        const AttemptReply p = parse_attempt("pending");
        check(p.valid && p.pending, "parse pending");

        const char* sample =
            "result,2,14\n"
            "S,12,3,67\n"
            "W,12,r,three,θ ɹ i,θ ɹ i,0\n"
            "W,13,w,green,ɡ ɹ i n,ɡ - i n +ə,1\n"
            "W,14,w,trees,t ɹ i z,tʃ ɹ i z,1\n"
            "P,θ,r\n"
            "P,ɹ,w\n"
            "P,t,w\n";
        const AttemptReply r = parse_attempt(sample);
        check(r.valid && !r.pending && r.code == 2 && r.worst_word == 14, "parse result line");
        check(r.words_n == 3 && r.words[0].right && !r.words[1].right && r.words[1].has_clip,
              "parse W lines and clip flags");
        check(std::strcmp(r.words[1].word, "green") == 0 &&
                  std::strcmp(r.words[1].ipa, "ɡ ɹ i n") == 0 &&
                  std::strcmp(r.words[1].heard, "ɡ - i n +ə") == 0,
              "parse W word, IPA and heard sounds");
        check(r.sounds_n == 3 && r.sounds[0].right && !r.sounds[1].right &&
                  std::strcmp(r.sounds[1].sound, "ɹ") == 0,
              "parse P lines");
    }

    // 2. Mistake counts: 4 misses then a recording without that word stays 4; 5th miss triggers.
    {
        MistakeCounts m;
        const AttemptReply miss_cat = parse_attempt(
            "result,2,0\nW,0,w,cat,k a t,k a -,1\nP,t,w\n");
        for (int i = 0; i < 4; ++i) {
            check(!m.apply(miss_cat), "miss 1..4 does not trigger teacher proposal yet");
        }
        check(m.word_count("cat") == 4 && m.sound_count("t") == 4, "counts reach 4");

        // Recording without "cat" or "t": both stay at 4.
        const AttemptReply other = parse_attempt(
            "result,1,0\nW,1,r,dog,d ɒ ɡ,d ɒ ɡ,0\nP,d,r\n");
        check(!m.apply(other), "unrelated recording does not trigger");
        check(m.word_count("cat") == 4 && m.sound_count("t") == 4, "absent word and sound stay at 4");

        // Codes 3 and 4 change nothing.
        check(!m.apply(parse_attempt("result,3,0")) && !m.apply(parse_attempt("result,4,0")),
              "codes 3 and 4 do not trigger");
        check(m.word_count("cat") == 4 && m.sound_count("t") == 4, "codes 3 and 4 keep counts");

        // 5th miss triggers teacher proposal and logs the two changes.
        logged_changes = 0;
        check(m.apply(miss_cat, count_logger), "5th miss triggers teacher proposal");
        check(m.word_count("cat") == 5 && m.sound_count("t") == 5 && logged_changes == 2,
              "count reaches 5 and calls logger for CSV");

        // Restarting reached counts clears 5 -> 0.
        m.reset_reached();
        check(m.word_count("cat") == 0 && m.sound_count("t") == 0, "reset_reached restarts 5 -> 0");
    }

    // 3. Repeated word in one recording counts once; right resets to 0.
    {
        MistakeCounts m;
        const AttemptReply twice = parse_attempt(
            "result,2,0\nW,0,w,the,ð ə,d ə,1\nW,3,w,the,ð ə,d ə,1\nP,ð,w\n");
        m.apply(twice);
        check(m.word_count("the") == 1, "two misses of same word in one recording count once");

        const AttemptReply fixed = parse_attempt(
            "result,1,0\nW,0,r,the,ð ə,ð ə,0\nP,ð,r\n");
        m.apply(fixed);
        check(m.word_count("the") == 0 && m.sound_count("ð") == 0, "right resets count to 0");
    }

    // 4. Table slot reuse: zeroed slots are reused so the table does not clog up.
    {
        MistakeCounts m;
        for (int i = 0; i < 80; ++i) {
            char buf[96];
            std::snprintf(buf, sizeof(buf), "result,2,0\nW,0,w,w%d,a,b,0\n", i);
            m.apply(parse_attempt(buf));
            std::snprintf(buf, sizeof(buf), "result,1,0\nW,0,r,w%d,a,a,0\n", i);
            m.apply(parse_attempt(buf));
        }
        m.apply(parse_attempt("result,2,0\nW,0,w,final,a,b,0\n"));
        check(m.word_count("final") == 1, "zero-count table slots are reused");
    }

    // 5. Knob steps: relative steps, hysteresis, clamped to 1..999, reset on new book.
    {
        PageKnob knob;
        knob.reset(2000);
        check(!knob.step(2100, 200), "jitter under one step does not turn page");
        check(knob.step(2200, 200) && knob.page == 2, "one step up -> page 2");
        check(!knob.step(2050, 200) && knob.page == 2, "small dip back stays on page 2");
        check(knob.step(2000, 200) && knob.page == 1, "full step down -> page 1");
        check(!knob.step(1800, 200) && knob.page == 1, "clamped at page 1");
        knob.page = 998;
        knob.anchor = 1000;
        check(knob.step(1600, 200) && knob.page == 999, "clamped at page 999");
        knob.reset(3500);
        check(knob.page == 1, "new book resets to page 1 regardless of knob position");
    }

    // 6. Quiet-mode noise summary.
    {
        const uint32_t peaks[4] = {12000, 9000, 15000, 11000};
        check(quietest_of_four(peaks) == 9000, "quietest_of_four picks minimum");
        check(std::fabs(peak_to_db(1000) - 60.0f) < 0.01f, "peak_to_db(1000) == 60 dB");
        check(std::fabs(peak_to_db(0) - 0.0f) < 0.01f, "peak_to_db(0) floors at 0 dB");
    }

    std::printf("\n%s: %d failure(s)\n", failures == 0 ? "ALL PASS" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
