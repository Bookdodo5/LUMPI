// Laptop tests for src/common/nest_logic.h, and the nest -> doll reply checked with the doll's own parser.
// No ESP32 needed. Run from the project folder:
//   g++ -std=c++17 -Wall -Wextra -Werror -I src/common tests/test_nest_logic.cpp -o build/test_nest_logic && ./build/test_nest_logic
#include <cstdio>
#include <cstring>
#include <string>

#include "doll_logic.h"
#include "nest_logic.h"

using namespace nest_logic;

static int failures = 0;

static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) {
        failures += 1;
    }
}

static const char* cloud_code_2 =
    "2,14\n"
    "S,12,4,61\n"
    "W,12,r,the,ð ə,ð ə,\n"
    "W,13,w,big,b ɪ ɡ,b - ɡ,/v1/word_audio?uid=04A1B2C3&passage=2&word=13\n"
    "W,14,w,fish,f ɪ ʃ,p ɪ s +s,/v1/word_audio?uid=04A1B2C3&passage=2&word=14\n"
    "W,15,w,swims,s w ɪ m z,s w - m z,/v1/word_audio?uid=04A1B2C3&passage=2&word=15\n"
    "P,ð,r\n"
    "P,ɪ,w\n";

static void grade_line() {
    Grade g = read_grade(cloud_code_2);
    check(g.ok && g.code == 2 && g.worst_word == 14, "read_grade: \"2,14\" -> code 2, worst word 14");
    g = read_grade("1,0");
    check(g.ok && g.code == 1 && g.worst_word == 0, "read_grade: \"1,0\" (no more lines)");
    g = read_grade("4,0\r\n");
    check(g.ok && g.code == 4, "read_grade: Windows line ending");
    check(!read_grade("").ok, "read_grade: empty reply -> broken");
    check(!read_grade("bad request").ok, "read_grade: 400 text -> broken");
    check(!read_grade("7,0").ok, "read_grade: code 7 never comes from the AI -> broken");
    check(!read_grade("0,0").ok, "read_grade: code 0 -> broken");
    check(!read_grade("2,").ok, "read_grade: missing word -> broken");
    check(!read_grade("2,x").ok, "read_grade: word not a number -> broken");
    check(!read_grade("{\"error\":\"x\"}").ok, "read_grade: JSON error body -> broken");
}

static void clip_choice() {
    ClipToFetch clips[max_clips];
    int n = clips_to_fetch(cloud_code_2, 14, clips);
    check(n == 3, "clips_to_fetch: 3 wrong words -> 3 clips");
    check(clips[0].word == 14, "clips_to_fetch: worst word first");
    check(clips[1].word == 13 && clips[2].word == 15, "clips_to_fetch: then the others in reading order");
    check(clips[0].path == "/v1/word_audio?uid=04A1B2C3&passage=2&word=14", "clips_to_fetch: path copied exactly");

    const std::string five_wrong = std::string("2,20\n") +
                                   "W,20,w,a,a,-,/w?word=20\nW,21,w,b,b,-,/w?word=21\nW,22,w,c,c,-,/w?word=22\n"
                                   "W,23,w,d,d,-,/w?word=23\nW,24,w,e,e,-,/w?word=24\n";
    n = clips_to_fetch(five_wrong, 20, clips);
    check(n == 3 && clips[0].word == 20 && clips[1].word == 21 && clips[2].word == 22,
          "clips_to_fetch: 5 wrong words -> only the first 3 (worst first)");
    n = clips_to_fetch(five_wrong, 24, clips);
    check(n == 3 && clips[0].word == 24 && clips[1].word == 20 && clips[2].word == 21,
          "clips_to_fetch: worst word last on the page still comes first");

    n = clips_to_fetch("1,0\nS,0,2,100\nW,0,r,a,a,a,\nW,1,r,b,b,b,\n", 0, clips);
    check(n == 0, "clips_to_fetch: all right -> no clips");
    n = clips_to_fetch("2,1\n1,fish,f ɪ ʃ,/v1/word_audio?uid=FFFFFFFF&passage=1&word=1\n", 1, clips);
    check(n == 0, "clips_to_fetch: today's old cloud format -> no clips, no crash");
    n = clips_to_fetch("4,0", 0, clips);
    check(n == 0, "clips_to_fetch: code 4 -> no clips");
}

static void reply_for_doll() {
    const uint32_t held[] = {14, 15};
    const std::string r = doll_reply(cloud_code_2, 2, 14, held, 2);
    check(r.rfind("result,2,14\nS,12,4,61\n", 0) == 0, "doll_reply: line 1 then S line");
    check(r.find("W,12,r,the,ð ə,ð ə,0") != std::string::npos, "doll_reply: right word -> clip 0");
    check(r.find("W,13,w,big,b ɪ ɡ,b - ɡ,0") != std::string::npos, "doll_reply: wrong word without clip -> 0");
    check(r.find("W,14,w,fish,f ɪ ʃ,p ɪ s +s,1") != std::string::npos, "doll_reply: held clip -> 1");
    check(r.find("word_audio") == std::string::npos, "doll_reply: no cloud address reaches the doll");
    check(r.find("P,ɪ,w") != std::string::npos, "doll_reply: P lines kept");

    check(doll_reply(cloud_code_2, 6, 14, held, 2) == "result,6,14", "doll_reply: dark room 6 -> no S/W/P lines");
    check(doll_reply("3,0", 3, 0, held, 0) == "result,3,0", "doll_reply: code 3 -> one line");
    check(doll_reply("garbage", 4, 0, held, 0) == "result,4,0", "doll_reply: broken cloud reply as 4 -> one line");
    check(doll_reply("2,1\n1,fish,f ɪ ʃ,/v1/word_audio?uid=FFFFFFFF&passage=1&word=1", 2, 1, held, 0) == "result,2,1",
          "doll_reply: old cloud format lines are dropped (doll would not read them)");

    std::string huge = "2,0\nS,0,40,10\n";
    for (int i = 0; i < 40; ++i) {
        huge += "W," + std::to_string(i) + ",w,abcdefghijklmnopqrst,a b c d e f g h i j,a b c d e f g h i j," +
                "/v1/word_audio?uid=0123456789ABCDEF0123&passage=999&word=" + std::to_string(i) + "\n";
    }
    for (int i = 0; i < 48; ++i) {
        huge += "P,s" + std::to_string(i) + ",w\n";
    }
    const std::string cut = doll_reply(huge, 2, 0, held, 0);
    check(cut.size() <= max_doll_reply, "doll_reply: 40 long W lines + 48 P lines stays <= 4096 bytes");
    check(cut.back() == '0' || cut.back() == 'w' || cut.back() == 'r', "doll_reply: last line is whole, not cut");
}

static void doll_reads_nest_reply() {
    const uint32_t held[] = {14};
    const std::string r = doll_reply(cloud_code_2, 2, 14, held, 1);
    const doll_logic::AttemptReply a = doll_logic::parse_attempt(r);
    check(a.valid && !a.pending && a.code == 2 && a.worst_word == 14, "doll parser: line 1 read");
    check(a.first_word == 12 && a.word_count == 4 && a.score == 61, "doll parser: S line read");
    check(a.words_n == 4 && a.sounds_n == 2, "doll parser: 4 W lines, 2 P lines");
    check(a.words[2].page_index == 14 && !a.words[2].right && a.words[2].has_clip, "doll parser: word 14 wrong + clip");
    check(std::strcmp(a.words[2].word, "fish") == 0 && std::strcmp(a.words[2].heard, "p ɪ s +s") == 0,
          "doll parser: word text and heard sounds intact");
    check(a.words[1].page_index == 13 && !a.words[1].has_clip, "doll parser: word 13 wrong, no clip");
    check(a.words[0].right, "doll parser: word 12 right");

    const doll_logic::AttemptReply dark = doll_logic::parse_attempt(doll_reply(cloud_code_2, 6, 14, held, 1));
    check(dark.valid && dark.code == 6 && dark.words_n == 0, "doll parser: dark 6 has no words");
    const doll_logic::AttemptReply pending = doll_logic::parse_attempt("pending");
    check(pending.valid && pending.pending, "doll parser: \"pending\"");
}

static void help_rules() {
    lumpi::HelpState h;
    bool new_call = false;
    check(std::strcmp(help_reply(h, true, 1000, new_call), "dark") == 0 && !new_call,
          "help: no call yet + dark -> \"dark\", no help event");
    check(std::strcmp(help_reply(h, false, 2000, new_call), "waiting") == 0 && new_call,
          "help: first ask -> \"waiting\" + ONE help event");
    check(std::strcmp(help_reply(h, false, 5000, new_call), "waiting") == 0 && !new_call,
          "help: asked again every 3 s -> \"waiting\", no second event");
    check(std::strcmp(help_reply(h, true, 8000, new_call), "waiting") == 0 && !new_call,
          "help: room goes dark while waiting -> still \"waiting\" (call already made)");
    uint32_t waited = 0;
    check(h.ack(12000, waited) && waited == 10, "help: ACK after 10 s -> waited 10");
    check(std::strcmp(help_reply(h, true, 12500, new_call), "coming") == 0 && !new_call,
          "help: after ACK -> \"coming\", even when dark");
    check(std::strcmp(help_reply(h, false, 15000, new_call), "coming") == 0 && !new_call,
          "help: \"coming\" lasts until the next /v1/start");
    h.on_start();
    check(std::strcmp(help_reply(h, false, 20000, new_call), "waiting") == 0 && new_call,
          "help: after a new reading, the next ask is a NEW call");
    uint32_t unused = 0;
    lumpi::HelpState quiet;
    check(!quiet.ack(1000, unused), "help: ACK with nobody waiting does nothing");
}

static void noise_rules() {
    lumpi::NoiseState n;
    const uint32_t loud = lumpi::NoiseState::default_threshold + 1;
    const uint32_t soft = lumpi::NoiseState::default_threshold - 1;
    check(std::strcmp(noise_reply(n, false, loud, 1000), "ok") == 0, "noise: nobody present -> ok");
    n.child_present = true;
    check(std::strcmp(noise_reply(n, false, soft, 2000), "ok") == 0, "noise: quiet -> ok");
    check(std::strcmp(noise_reply(n, true, loud, 3000), "dark") == 0, "noise: dark -> \"dark\" (quiet mode paused)");
    check(std::strcmp(noise_reply(n, false, loud, 4000), "command,5") == 0, "noise: loud -> command,5");
    check(std::strcmp(noise_reply(n, false, loud, 20000), "ok") == 0, "noise: loud again within 30 s -> ok (cooldown)");
    check(std::strcmp(noise_reply(n, false, loud, 34001), "command,5") == 0, "noise: loud after 30 s -> command,5");
    n.session_active = true;
    check(std::strcmp(noise_reply(n, false, loud, 90000), "ok") == 0, "noise: reading in progress -> ok");

    check(reading_in_progress(lumpi::State::recording), "reading_in_progress: recording");
    check(reading_in_progress(lumpi::State::assessing), "reading_in_progress: assessing");
    check(!reading_in_progress(lumpi::State::result), "reading_in_progress: a kept RESULT does not block quiet mode");
    check(!reading_in_progress(lumpi::State::idle), "reading_in_progress: idle");
}

static void dark_rules() {
    lumpi::DarkState d;
    const float dark_lux = 10, bright_lux = 15;
    const uint32_t hold = 3000;
    check(!d.update(5, dark_lux, bright_lux, hold, 0), "dark: below 10 lx starts the 3 s timer");
    check(!d.update(5, dark_lux, bright_lux, hold, 2999), "dark: 2.999 s -> not dark yet");
    check(d.update(5, dark_lux, bright_lux, hold, 3000), "dark: 3 s -> dark");
    check(d.update(12, dark_lux, bright_lux, hold, 4000), "dark: 12 lx (between 10 and 15) -> still dark (hysteresis)");
    check(d.update(20, dark_lux, bright_lux, hold, 5000), "dark: 20 lx starts the bright timer");
    check(d.update(5, dark_lux, bright_lux, hold, 6000), "dark: dark again before 3 s -> timer reset");
    check(d.update(20, dark_lux, bright_lux, hold, 7000), "dark: bright timer restarts");
    check(!d.update(20, dark_lux, bright_lux, hold, 10000), "dark: 3 s bright -> normal");
    check(!d.update(NAN, dark_lux, bright_lux, hold, 11000), "dark: no sensor (NAN) -> never dark");
    d.update(1, dark_lux, bright_lux, hold, 12000);
    d.update(1, dark_lux, bright_lux, hold, 15000);
    check(d.is_dark, "dark: dark again");
    check(!d.update(NAN, dark_lux, bright_lux, hold, 16000), "dark: sensor unplugged while dark -> unblocks at once");
    check(lumpi::decide_command(2, true) == lumpi::too_dark && lumpi::decide_command(2, false) == 2,
          "decide_command: dark -> 6, else the AI's code");
}

static void two_dolls_never_mix() {
    lumpi::Session s[lumpi::max_dolls];
    check(s[0].start(1, 3, 10, 0) && s[1].start(2, 9, 20, 0), "two dolls: both start at once");
    check(s[0].add_chunk(1, 3, 0, 8192, 100) == lumpi::ChunkResult::stored, "two dolls: doll 1 piece 0");
    check(s[1].add_chunk(2, 9, 0, 8192, 120) == lumpi::ChunkResult::stored, "two dolls: doll 2 piece 0");
    check(s[0].add_chunk(2, 9, 1, 8192, 130) == lumpi::ChunkResult::rejected, "two dolls: doll 2 piece in doll 1 slot -> refused");
    check(s[0].finish(1, 3, 1, 8192, 200) && s[1].finish(2, 9, 1, 8192, 200), "two dolls: both finish");
    check(s[1].got_result(2, 9, 1, 0) && !s[0].got_result(2, 9, 1, 0), "two dolls: grade only lands on its own doll");
    check(s[0].state == lumpi::State::assessing && s[1].state == lumpi::State::result, "two dolls: states stay separate");
    check(!s[0].got_result(1, 2, 1, 0), "grade for an older attempt -> ignored");
    s[0].check_timeouts(200 + 45001);
    check(s[0].state == lumpi::State::idle, "assessing > 45 s -> dropped");
    check(!s[0].got_result(1, 3, 1, 0), "a late grade after the timeout -> ignored (doll gets 409, acts 4)");
    s[1].check_timeouts(10000000);
    check(s[1].state == lumpi::State::result, "a RESULT never times out");
    check(!s[0].start(3, 1, 1, 0), "node 3 -> refused (exactly 2 dolls)");
    check(!s[0].start(1, 1, 0, 0) && !s[0].start(1, 1, 1000, 0), "passage 0 or 1000 -> refused");
    check(s[0].start(1, 1, 999, 0), "passage 999 -> ok");
    check(s[0].add_chunk(1, 1, 0, 8193, 0) == lumpi::ChunkResult::rejected, "chunk 8193 bytes -> refused");
    check(s[0].add_chunk(1, 1, 0, 7, 0) == lumpi::ChunkResult::rejected, "odd chunk size -> refused");
    for (uint32_t i = 0; i < 39; ++i) {
        s[0].add_chunk(1, 1, i, 8192, 0);
    }
    check(s[0].bytes == 319488, "39 chunks = 319488 bytes");
    check(s[0].add_chunk(1, 1, 39, 2048, 0) == lumpi::ChunkResult::rejected, "past 320000 bytes -> refused");
    check(s[0].add_chunk(1, 1, 39, 512, 0) == lumpi::ChunkResult::stored, "exactly 320000 bytes -> ok");
}

int main() {
    grade_line();
    clip_choice();
    reply_for_doll();
    doll_reads_nest_reply();
    help_rules();
    noise_rules();
    dark_rules();
    two_dolls_never_mix();
    std::printf("\n%d failure(s)\n", failures);
    return failures == 0 ? 0 : 1;
}
