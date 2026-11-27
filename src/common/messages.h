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

// Doll -> gateway (the 11 routes on the nest).
namespace route {
constexpr const char* start = "/v1/start";         // POST ?node&attempt&passage&uid&fsr -> "ok" | 409 dark | 409
constexpr const char* chunk = "/v1/chunk";         // PUT  ?node&attempt&index, body 2..8192 even bytes -> "<index>" | 409
constexpr const char* finish = "/v1/finish";       // POST ?node&attempt&chunks&bytes -> "assessing" | 409 totals differ
constexpr const char* attempt = "/v1/attempt";     // GET  ?node&attempt -> "pending" | "result,<code>,<word>" (+ S/W/P) | 409
constexpr const char* word_clip = "/v1/word_clip"; // GET  ?node&attempt&word -> 200 audio/wav | 409 no clip
constexpr const char* cancel = "/v1/cancel";       // POST ?node&attempt -> "cancelled"
constexpr const char* presence = "/v1/presence";   // POST ?node&present=0|1&fsr&mode=read|quiet -> "ok" | "dark"
constexpr const char* help = "/v1/help";           // GET  ?node -> "waiting" | "coming" | "dark"
constexpr const char* noise = "/v1/noise";         // POST ?node&db&peak -> "ok" | "command,5" | "dark"
constexpr const char* book = "/v1/book";           // GET  ?node&uid&passage -> "ok,<title>" | "unknown" | "offline"
constexpr const char* status = "/v1/status";       // GET  -> JSON (room + every doll)
} // namespace route

// Gateway -> cloud.
namespace laptop_route {
constexpr const char* assess = "/v1/assess";         // POST ?node&attempt&passage&uid, body = recording -> "<code>,<word>" (+ S/W/P)
constexpr const char* word_audio = "/v1/word_audio"; // GET  ?uid&passage&word -> WAV | 404
constexpr const char* page = "/v1/page";             // GET  ?uid&passage -> JSON page + book title | 404
} // namespace laptop_route

// Commands the doll acts out.
// 1-4: cloud AI grade. 5: nest's answer to /v1/noise. 6: nest's dark-room block (or doll's dark screen).
// 7: the doll itself, right after its teacher call (/v1/help). 8: teacher's ACK ("coming" from /v1/help).
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
