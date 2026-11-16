#pragma once
// THE MESSAGE CONTRACT: the names and numbers every board and the laptop must agree on.
// Build guide: https://lumpi-build-guide.vercel.app/reference/messages
//
// Plain C++ (no Arduino), so the doll, the gateway and the laptop test all include
// this one file instead of typing the numbers again.
// Change a number here and in laptop/adapter/reading_ai.py in the SAME commit.
//
// Every local message is HTTP with the header  Authorization: Bearer <LUMPI_LAN_KEY>
// and carries node (1..max_dolls). Reading messages also carry attempt.
#include <cstdint>

namespace lumpi {

// The version is the "/v1" at the start of every route.
// A change that old firmware cannot understand gets a new prefix ("/v2/...").
constexpr uint32_t protocol_version = 1;

// Doll -> gateway (the 10 routes on the nest).
namespace route {
constexpr const char* start = "/v1/start";         // POST ?node&attempt&passage&uid&fsr -> "ok" | 409 <reason>
constexpr const char* chunk = "/v1/chunk";         // PUT  ?node&attempt&index, body <= 2048 bytes -> "<index>" (the ACK)
constexpr const char* finish = "/v1/finish";       // POST ?node&attempt&chunks&bytes -> "assessing" | 409 totals do not match
constexpr const char* attempt = "/v1/attempt";     // GET  ?node&attempt -> "pending" | "result,<code>,<word>" | 409 unknown attempt
constexpr const char* word_clip = "/v1/word_clip"; // GET  ?node&attempt -> WAV | 409
constexpr const char* cancel = "/v1/cancel";       // POST ?node&attempt -> "cancelled"
constexpr const char* presence = "/v1/presence";   // POST ?node&present=0|1 -> "ok"
constexpr const char* help = "/v1/help";           // GET  ?node -> "waiting" | "coming"
constexpr const char* noise = "/v1/noise";         // POST ?node&db -> "ok" | "command,5"
constexpr const char* status = "/v1/status";       // GET  -> JSON (room + every doll)
} // namespace route

// Gateway -> laptop.
namespace laptop_route {
constexpr const char* assess = "/v1/assess";         // POST ?node&attempt&passage&uid, body = recording -> "<code>,<word>"
constexpr const char* word_audio = "/v1/word_audio"; // GET  ?uid&passage&word -> WAV | 404
} // namespace laptop_route

// Commands the doll acts out.
// 1-4: the laptop AI's grade. 6, 7: made by the nest only.
// 5: the nest's answer to a noise report. 8: the doll acting out "coming" from /v1/help.
enum Command : uint32_t {
    accepted = 1,
    retry_word = 2,
    improve_audio = 3,
    uncertain = 4,
    too_noisy = 5,
    too_dark = 6,
    told_teacher = 7,
    teacher_coming = 8
};

// Audio format of every recording: 16 kHz, 16-bit, mono.
constexpr uint32_t sample_rate_hz = 16000;
constexpr uint32_t bytes_per_sample = 2;

} // namespace lumpi
