#pragma once
// THE NEST'S GRADE HANDLING: reads the cloud's /v1/assess reply and builds the doll's /v1/attempt reply.
// Plain C++ (no Arduino), so the laptop tests it: tests/test_nest_logic.cpp.
//
// The cloud's reply, one record per line:
//   2,14                                   <- code, the page index of the worst word
//   S,10,6,72                              <- first page word, word count, score of the matched words
//   W,14,w,fish,f ɪ ʃ,f - ʃ,/v1/word_audio?uid=04A1B2C3&passage=2&word=14
//   W,15,r,swims,s w ɪ m z,s w ɪ m z,      <- a right word has no clip address
//   P,ɪ,w                                  <- one sound, right or wrong
// The doll's reply: "result,<command>,<worst word>", then the same S, W and P lines, except
// that each W line's clip address becomes "1" (the nest holds that word's clip) or "0".
#include <cstdint>
#include <string>
#include <string_view>

#include "query.h"
#include "quiet_study_logic.h"
#include "session.h"

namespace nest_logic {

constexpr size_t max_doll_reply = 4096; // the doll reads at most this much
constexpr int max_clips = 3;            // word clips the nest fetches per reading (flash and time)

inline std::string_view next_line(std::string_view& rest) {
    const size_t end = rest.find('\n');
    std::string_view line = rest.substr(0, end);
    rest = end == std::string_view::npos ? std::string_view() : rest.substr(end + 1);
    if (!line.empty() && line.back() == '\r') {
        line.remove_suffix(1);
    }
    return line;
}

struct Grade {
    bool ok = false;
    uint32_t code = 0;
    uint32_t worst_word = 0;
};

// Line 1, "<code>,<word>". Not ok = a broken reply: the nest then gives the doll 4 (try again).
inline Grade read_grade(std::string_view reply) {
    std::string_view line = next_line(reply);
    const size_t comma = line.find(',');
    Grade grade;
    if (comma == std::string_view::npos) {
        return grade;
    }
    grade.ok = query::read_number(line.substr(0, comma), grade.code) &&
               query::read_number(line.substr(comma + 1), grade.worst_word) && grade.code >= 1 && grade.code <= 4;
    return grade;
}

// The text after the last comma of a line: a W line's clip address.
inline std::string_view last_field(std::string_view line) {
    const size_t comma = line.rfind(',');
    return comma == std::string_view::npos ? std::string_view() : line.substr(comma + 1);
}

// The page word index of a W line ("W,14,w,..." -> 14). False when the line is not a W line.
inline bool w_line_word(std::string_view line, uint32_t& word) {
    if (line.substr(0, 2) != "W,") {
        return false;
    }
    line.remove_prefix(2);
    return query::read_number(line.substr(0, line.find(',')), word);
}

struct ClipToFetch {
    uint32_t word = 0;
    std::string path; // "/v1/word_audio?uid=..&passage=..&word=14", appended to the cloud's address
};

// The wrong words whose clip the nest fetches before it publishes the result:
// the worst word first, then the other wrong words in reading order, at most max_clips.
inline int clips_to_fetch(std::string_view reply, uint32_t worst_word, ClipToFetch out[max_clips]) {
    int count = 0;
    for (int round = 0; round < 2; ++round) {
        std::string_view rest = reply;
        next_line(rest);
        while (!rest.empty() && count < max_clips) {
            const std::string_view line = next_line(rest);
            uint32_t word = 0;
            const std::string_view path = last_field(line);
            if (!w_line_word(line, word) || path.substr(0, 1) != "/") {
                continue;
            }
            const bool is_worst = word == worst_word;
            if ((round == 0) == is_worst) {
                out[count].word = word;
                out[count].path = std::string(path);
                count += 1;
            }
        }
    }
    return count;
}

inline bool is_held(uint32_t word, const uint32_t held[], int held_count) {
    for (int i = 0; i < held_count; ++i) {
        if (held[i] == word) {
            return true;
        }
    }
    return false;
}

// The doll's /v1/attempt answer. Only commands 1 and 2 carry the S, W and P lines; a dark-room 6
// or a 3 or 4 carries none. A line that would pass max_doll_reply is left out, never cut in half.
inline std::string doll_reply(std::string_view cloud_reply, uint32_t command, uint32_t worst_word,
                              const uint32_t held[], int held_count) {
    std::string reply = "result," + std::to_string(command) + "," + std::to_string(worst_word);
    if (command != 1 && command != 2) {
        return reply;
    }
    std::string_view rest = cloud_reply;
    next_line(rest);
    while (!rest.empty()) {
        const std::string_view line = next_line(rest);
        std::string out;
        uint32_t word = 0;
        if (w_line_word(line, word)) {
            const std::string_view without_clip = line.substr(0, line.rfind(',') + 1);
            out = std::string(without_clip) + (is_held(word, held, held_count) ? "1" : "0");
        } else if (line.substr(0, 2) == "S," || line.substr(0, 2) == "P,") {
            out = std::string(line);
        } else {
            continue; // an old-format or unknown line: the doll would not understand it
        }
        if (reply.size() + 1 + out.size() > max_doll_reply) {
            break;
        }
        reply += "\n" + out;
    }
    return reply;
}

// /v1/help. The first ask is the teacher call (new_call = true: send one help event).
// A dark room refuses a NEW call, but a call already waiting still hears "coming" after the ACK.
inline const char* help_reply(lumpi::HelpState& help, bool dark, uint32_t now_ms, bool& new_call) {
    new_call = false;
    if (help.answer() == 0) {
        if (dark) {
            return "dark";
        }
        new_call = help.on_call(now_ms);
    }
    return help.answer() == 2 ? "coming" : "waiting";
}

// /v1/noise. A dark room pauses quiet mode. "command,5" = ask for quiet (30 s cooldown in NoiseState).
inline const char* noise_reply(lumpi::NoiseState& noise, bool dark, uint32_t peak, uint32_t now_ms) {
    if (dark) {
        return "dark";
    }
    return noise.on_noise(peak, lumpi::NoiseState::default_threshold, now_ms) ? "command,5" : "ok";
}

// A reading in progress: quiet-mode warnings must not interrupt it. A kept RESULT is not one.
inline bool reading_in_progress(lumpi::State state) {
    return state == lumpi::State::recording || state == lumpi::State::assessing;
}

} // namespace nest_logic
