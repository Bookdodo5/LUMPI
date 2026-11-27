#pragma once
// THE REFEREE: the rules for one doll's reading.
// Build guide: https://lumpi-build-guide.vercel.app/gateway/rules
//
// Plain C++: no Arduino, no Wi-Fi, no pins. So the laptop can test it
// (src/test/test_round_trip.cpp) without any board.
//
// One Session per doll. The gateway keeps sessions[max_dolls];
// doll n uses sessions[n - 1]. One doll recording never blocks another.
//
//     IDLE ---start---> RECORDING ---finish---> ASSESSING ---got_result---> RESULT
//      ^                    |                       |                         |
//      +--- cancel / 10 s --+--- cancel / 45 s -----+                         |
//                           ^                                                 |
//                           +-------------------- next start -----------------+
#include <cmath>
#include <cstdint>

#include "messages.h"

namespace lumpi {

constexpr uint32_t max_dolls = 2; // exactly 2 physical dolls: nodes 1..2

// Same order as the "state" number in /v1/status: 0 IDLE, 1 RECORDING, 2 ASSESSING, 3 RESULT.
enum class State { idle, recording, assessing, result };

// What happened to an uploaded audio piece.
enum class ChunkResult {
    stored,         // new piece, accepted: save it and send the ACK
    already_had_it, // a re-send (our ACK was lost): ACK again, do NOT save twice
    rejected        // wrong doll, wrong attempt, wrong order, or bad size
};

class Session {
  public:
    State state = State::idle;
    uint32_t node = 0;        // which doll: 1 or 2
    uint32_t attempt = 0;     // that doll's reading number
    uint32_t passage = 0;     // page being read: 1..999
    uint32_t chunks = 0;      // audio pieces received so far
    uint32_t bytes = 0;       // audio bytes received so far
    uint32_t result_code = 0; // command for the doll: 1-4 or 6 (0 = none yet)
    uint32_t result_word = 0; // which page word to retry; only meaningful for code 2

    static constexpr uint32_t max_bytes = 320000;     // 10 s of 16 kHz 16-bit audio
    static constexpr uint32_t max_chunk_bytes = 8192; // one piece = 256 ms (4096 samples)
    static constexpr uint32_t max_passage = 999;
    static constexpr uint32_t recording_timeout_ms = 10000;
    static constexpr uint32_t assessing_timeout_ms = 45000;

    // Allowed from IDLE and from RESULT (the new reading replaces the old result).
    bool start(uint32_t new_node, uint32_t new_attempt, uint32_t new_passage, uint32_t now_ms) {
        if (state == State::recording || state == State::assessing) {
            return false;
        }
        if (new_node < 1 || new_node > max_dolls) {
            return false;
        }
        if (new_passage < 1 || new_passage > max_passage) {
            return false;
        }
        node = new_node;
        attempt = new_attempt;
        passage = new_passage;
        chunks = 0;
        bytes = 0;
        result_code = 0;
        result_word = 0;
        timer_started_ms_ = now_ms;
        state = State::recording;
        return true;
    }

    // Is this message about the current reading? A late message from an older attempt is not.
    bool owned_by(uint32_t from_node, uint32_t from_attempt) const {
        if (state == State::idle) {
            return false;
        }
        return from_node == node && from_attempt == attempt;
    }

    // Pieces must arrive in order: 0, 1, 2 ... Each stored piece restarts the 10 s silence timer.
    ChunkResult add_chunk(uint32_t from_node, uint32_t from_attempt, uint32_t index, uint32_t size,
                          uint32_t now_ms) {
        if (state != State::recording || !owned_by(from_node, from_attempt)) {
            return ChunkResult::rejected;
        }
        // The duplicate check comes BEFORE the size check, so a re-sent piece is always ACKed again.
        if (index < chunks) {
            return ChunkResult::already_had_it;
        }
        if (index != chunks) {
            return ChunkResult::rejected; // a gap: a piece before it is missing
        }
        // Each sample is 2 bytes, so a valid size is even.
        if (size == 0 || size > max_chunk_bytes || size % 2 != 0 || bytes + size > max_bytes) {
            return ChunkResult::rejected;
        }
        chunks += 1;
        bytes += size;
        timer_started_ms_ = now_ms;
        return ChunkResult::stored;
    }

    // The doll says "I sent N pieces and B bytes". Accept only an exact match,
    // so a recording with a hole in it never reaches the AI.
    bool finish(uint32_t from_node, uint32_t from_attempt, uint32_t chunk_count, uint32_t byte_count,
                uint32_t now_ms) {
        if (state != State::recording || !owned_by(from_node, from_attempt)) {
            return false;
        }
        if (bytes == 0 || chunk_count != chunks || byte_count != bytes) {
            return false;
        }
        timer_started_ms_ = now_ms; // the 45 s limit counts from the end of the recording
        state = State::assessing;
        return true;
    }

    // The gateway decided the command. Kept until this doll's next start(),
    // so a doll that lost the reply can ask again and get the same answer.
    bool got_result(uint32_t from_node, uint32_t from_attempt, uint32_t code, uint32_t word) {
        if (state != State::assessing || !owned_by(from_node, from_attempt)) {
            return false;
        }
        result_code = code;
        result_word = word;
        state = State::result;
        return true;
    }

    void cancel() {
        state = State::idle;
    }

    // Called every 10 ms for every doll. A RESULT never times out.
    void check_timeouts(uint32_t now_ms) {
        // Subtracting unsigned numbers stays correct when millis() wraps after 49 days.
        const uint32_t elapsed = now_ms - timer_started_ms_;
        if (state == State::recording && elapsed > recording_timeout_ms) {
            cancel();
        }
        if (state == State::assessing && elapsed > assessing_timeout_ms) {
            cancel();
        }
    }

  private:
    uint32_t timer_started_ms_ = 0; // when the current state's time limit started
};

// Dark-room hysteresis for the nest's BH1750 light sensor.
// Dark after staying below dark_lux for hold_ms; bright again at/above bright_lux for hold_ms.
// Missing sensor (NAN) is never dark and never blocks.
struct DarkState {
    bool is_dark = false;
    bool timing = false;
    uint32_t since_ms = 0;

    bool update(float lux, float dark_lux, float bright_lux, uint32_t hold_ms, uint32_t now_ms) {
        if (!std::isfinite(lux)) {
            is_dark = false;
            timing = false;
            return false;
        }
        const bool want_change = is_dark ? (lux >= bright_lux) : (lux < dark_lux);
        if (!want_change) {
            timing = false;
            return is_dark;
        }
        if (!timing) {
            timing = true;
            since_ms = now_ms;
        } else if (now_ms - since_ms >= hold_ms) {
            is_dark = !is_dark;
            timing = false;
        }
        return is_dark;
    }
};

// Combines the AI grade with the room light state: a dark room overrides any grade with command 6.
inline uint32_t decide_command(uint32_t ai_code, bool dark) {
    return dark ? static_cast<uint32_t>(too_dark) : ai_code;
}

// Teacher-call state, one per doll (help[max_dolls]).
// "stuck" is set by the doll's teacher call (/v1/help); mistake counts live on the doll.
struct HelpState {
    bool stuck = false;          // waiting for the teacher
    bool coming = false;         // the teacher pressed ACK
    uint32_t stuck_since_ms = 0; // when the child called, for acked_after_s

    // Returns true on the first call so the nest sends one help event.
    bool on_call(uint32_t now_ms) {
        if (stuck || coming) {
            return false;
        }
        stuck = true;
        coming = false;
        stuck_since_ms = now_ms;
        return true;
    }

    // The teacher pressed ACK.
    bool ack(uint32_t now_ms, uint32_t& waited_s) {
        if (!stuck) {
            return false;
        }
        stuck = false;
        coming = true;
        waited_s = (now_ms - stuck_since_ms) / 1000;
        return true;
    }

    // A new reading ends the wait and clears "coming".
    void on_start() {
        stuck = false;
        coming = false;
    }

    // 2 = "coming", 1 = "waiting", 0 = "not stuck" (the web server turns it into the reply).
    int answer() const {
        if (coming) {
            return 2;
        }
        if (stuck) {
            return 1;
        }
        return 0;
    }
};

} // namespace lumpi
