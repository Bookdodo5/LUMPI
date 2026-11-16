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
//      +--- cancel / 10 s --+--- cancel / 30 s -----+                         |
//                           ^                                                 |
//                           +-------------------- next start -----------------+
#include <cstdint>

namespace lumpi {

constexpr uint32_t max_dolls = 3; // 2 physical + 1 simulated; nodes 1..3

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
    uint32_t node = 0;        // which doll: 1, 2 or 3
    uint32_t attempt = 0;     // that doll's reading number
    uint32_t passage = 0;     // page being read: 1, 2 or 3
    uint32_t chunks = 0;      // audio pieces received so far
    uint32_t bytes = 0;       // audio bytes received so far
    uint32_t result_code = 0; // command for the doll: 1-4, 6 or 7 (0 = none yet)
    uint32_t result_word = 0; // which word to retry; only meaningful for code 2

    static constexpr uint32_t max_bytes = 256000;     // 8 s of 16 kHz 16-bit audio
    static constexpr uint32_t max_chunk_bytes = 2048; // one piece = 1024 samples
    static constexpr uint32_t recording_timeout_ms = 10000;
    static constexpr uint32_t assessing_timeout_ms = 30000;

    // Allowed from IDLE and from RESULT (the new reading replaces the old result).
    bool start(uint32_t new_node, uint32_t new_attempt, uint32_t new_passage, uint32_t now_ms) {
        if (state == State::recording || state == State::assessing) {
            return false;
        }
        if (new_node < 1 || new_node > max_dolls) {
            return false;
        }
        if (new_passage < 1 || new_passage > 3) {
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

    // Pieces must arrive in order: 0, 1, 2 ...
    ChunkResult add_chunk(uint32_t from_node, uint32_t from_attempt, uint32_t index, uint32_t size) {
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
        timer_started_ms_ = now_ms; // the 30 s limit counts from the end of the recording
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

// The stuck counter and the help answer, one per doll (help[max_dolls]).
// Build guide: https://lumpi-build-guide.vercel.app/gateway/rules (struct HelpState)
struct HelpState {
    uint32_t count = 0;          // non-accepted readings in a row
    uint32_t passage = 0;        // the passage count belongs to
    bool stuck = false;          // waiting for the teacher
    bool coming = false;         // the teacher pressed ACK
    uint32_t word = 0;           // the missed word, for the screen and the alert
    uint32_t stuck_since_ms = 0; // when it became stuck, for acked_after_s

    // Returns true when the doll should get command 7 (told_teacher).
    bool on_grade(uint32_t new_passage, bool accepted, uint32_t missed_word, uint32_t now_ms,
                  uint32_t stuck_after) {
        if (new_passage != passage) {
            passage = new_passage;
            count = 0;
        }
        if (accepted) {
            count = 0;
            return false;
        }
        count += 1;
        if (count >= stuck_after) {
            stuck = true;
            coming = false;
            word = missed_word;
            stuck_since_ms = now_ms;
            return true;
        }
        return false;
    }

    // The teacher pressed ACK.
    bool ack(uint32_t now_ms, uint32_t& waited_s) {
        if (!stuck) {
            return false;
        }
        stuck = false;
        coming = true;
        count = 0;
        waited_s = (now_ms - stuck_since_ms) / 1000;
        return true;
    }

    // A new reading ends the wait. count stays: the same passage still counts.
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
