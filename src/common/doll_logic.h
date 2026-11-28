#pragma once
// PURE DOLL LOGIC: four rules, plain C++ (no Arduino), laptop-tested by tests/test_doll_logic.cpp.
//   1. parse_attempt()    reads the nest's /v1/attempt reply ("pending" or "result,..." + S/W/P)
//   2. MistakeCounts      tracks consecutive misses per word and sound (5 -> propose teacher)
//   3. PageKnob           turns relative potentiometer steps into page numbers 1..999
//   4. quietest_of_four() / peak_to_db()   one-second room noise summary
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "query.h"

namespace doll_logic {

constexpr int max_reply_words = 40;
constexpr int max_reply_sounds = 48;
constexpr int word_slots = 64;
constexpr int sound_slots = 48;
constexpr int teacher_limit = 5;

// ---- 1. Parse /v1/attempt reply ----------------------------------------------------------

struct WordLine {
    uint32_t page_index = 0;  // page word index (for /v1/word_clip)
    bool right = true;
    char word[24] = {};
    char ipa[32] = {};        // book sounds
    char heard[40] = {};      // what was heard ("-" = missed, "+x" = extra)
    bool has_clip = false;    // "1" = nest holds this word's clip
};

struct SoundLine {
    char sound[12] = {};
    bool right = true;
};

struct AttemptReply {
    bool valid = false, pending = false;
    uint32_t code = 0, worst_word = 0;
    WordLine words[max_reply_words] = {};
    int words_n = 0;
    SoundLine sounds[max_reply_sounds] = {};
    int sounds_n = 0;
};

using query::cut;

inline void store(char* dst, size_t cap, std::string_view src) {
    const size_t n = src.size() < cap - 1 ? src.size() : cap - 1;
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

inline AttemptReply parse_attempt(std::string_view rest) {
    AttemptReply r;

    std::string_view f = query::next_line(rest);
    if (f == "pending") {
        r.valid = r.pending = true;
        return r;
    }
    if (cut(f, ',') != "result" || !query::read_number(cut(f, ','), r.code) ||
        !query::read_number(cut(f, ','), r.worst_word)) {
        return r;
    }
    r.valid = true;

    while (!rest.empty()) {
        f = query::next_line(rest);
        const std::string_view tag = cut(f, ',');
        if (tag == "W" && r.words_n < max_reply_words) {
            WordLine& w = r.words[r.words_n];
            if (query::read_number(cut(f, ','), w.page_index)) {
                w.right = cut(f, ',') == "r";
                store(w.word, sizeof(w.word), cut(f, ','));
                store(w.ipa, sizeof(w.ipa), cut(f, ','));
                store(w.heard, sizeof(w.heard), cut(f, ','));
                w.has_clip = cut(f, ',') == "1";
                r.words_n += 1;
            }
        } else if (tag == "P" && r.sounds_n < max_reply_sounds) {
            SoundLine& p = r.sounds[r.sounds_n];
            store(p.sound, sizeof(p.sound), cut(f, ','));
            p.right = cut(f, ',') == "r";
            if (p.sound[0] != '\0') {
                r.sounds_n += 1;
            }
        }
    }
    return r;
}

// ---- 2. Mistake counts for one reading session -------------------------------------------
// Wrong -> +1, right -> 0, absent -> unchanged. Codes 3, 4, 6 change nothing.
// Repeated misses of the same word or sound inside one recording count once.

struct Slot {
    char key[24] = {};
    uint8_t count = 0;
    uint32_t run = 0;    // marks slots seen in the current recording so repeats count once
    bool wrong = false;
};

using CountLogger = void (*)(const char* key, uint8_t new_count);

// Marks one word or sound as seen in this recording; reuses any 0-count slot for new misses.
inline void mark_seen(Slot* table, int cap, const char* key, bool wrong, uint32_t run) {
    if (key[0] == '\0') {
        return;
    }
    int slot = -1, spare = -1;
    for (int i = 0; i < cap; ++i) {
        if (std::strcmp(table[i].key, key) == 0) {
            slot = i;
            break;
        }
        if (spare < 0 && table[i].count == 0) {
            spare = i;
        }
    }
    if (slot < 0) {
        if (!wrong || spare < 0) {
            return;
        }
        slot = spare;
        store(table[slot].key, sizeof(table[slot].key), key);
        table[slot].count = 0;
    }
    if (table[slot].run != run) {
        table[slot].run = run;
        table[slot].wrong = wrong;
    } else if (wrong) {
        table[slot].wrong = true;  // any wrong occurrence in this recording makes it wrong
    }
}

// Commits every slot seen in this recording: wrong -> +1, right -> 0. Returns true if any hit 5.
inline bool commit_run(Slot* table, int cap, uint32_t run, CountLogger log) {
    bool hit = false;
    for (int i = 0; i < cap; ++i) {
        if (table[i].run != run) {
            continue;
        }
        const uint8_t before = table[i].count;
        table[i].count = table[i].wrong ? static_cast<uint8_t>(before < 255 ? before + 1 : 255) : 0;
        if (log && table[i].count != before) {
            log(table[i].key, table[i].count);
        }
        if (table[i].wrong && table[i].count >= teacher_limit) {
            hit = true;
        }
    }
    return hit;
}

struct MistakeCounts {
    Slot words[word_slots] = {};
    Slot sounds[sound_slots] = {};
    uint32_t run = 0;

    void clear() {
        std::fill(std::begin(words), std::end(words), Slot{});
        std::fill(std::begin(sounds), std::end(sounds), Slot{});
        run = 0;
    }

    // Restarts any count that reached 5 (after a teacher call, or when reading on without calling).
    void reset_reached() {
        auto restart = [](Slot& s) { s.count = s.count >= teacher_limit ? 0 : s.count; };
        std::for_each(std::begin(words), std::end(words), restart);
        std::for_each(std::begin(sounds), std::end(sounds), restart);
    }

    bool apply(const AttemptReply& reply, CountLogger log = nullptr) {
        if (reply.code != 1 && reply.code != 2) {
            return false;
        }
        run += 1;
        for (int i = 0; i < reply.words_n; ++i) {
            mark_seen(words, word_slots, reply.words[i].word, !reply.words[i].right, run);
        }
        for (int i = 0; i < reply.sounds_n; ++i) {
            mark_seen(sounds, sound_slots, reply.sounds[i].sound, !reply.sounds[i].right, run);
        }
        const bool w_hit = commit_run(words, word_slots, run, log);
        const bool s_hit = commit_run(sounds, sound_slots, run, log);
        return w_hit || s_hit;
    }
};

// ---- 3. Relative page knob (1..999) ------------------------------------------------------

struct PageKnob {
    int page = 1, anchor = 0;
    bool ready = false;

    void reset(int adc) {
        page = 1;
        anchor = adc;
        ready = true;
    }

    bool step(int adc, int step_counts, int max_page = 999) {
        if (!ready) {
            reset(adc);
            return false;
        }
        const int before = page;
        while (step_counts > 0 && adc - anchor >= step_counts) {
            anchor += step_counts;
            if (page < max_page) {
                page += 1;
            }
        }
        while (step_counts > 0 && anchor - adc >= step_counts) {
            anchor -= step_counts;
            if (page > 1) {
                page -= 1;
            }
        }
        return page != before;
    }
};

// ---- 4. Quiet-mode noise over one second -------------------------------------------------

inline uint32_t quietest_of_four(const uint32_t peaks[4]) {
    return *std::min_element(peaks, peaks + 4);
}

inline float peak_to_db(uint32_t peak) {
    return 20.0f * std::log10(static_cast<float>(peak > 0 ? peak : 1));
}

}  // namespace doll_logic
