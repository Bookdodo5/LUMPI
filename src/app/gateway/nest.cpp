// THE NEST, real firmware: one per classroom. Upload environment: nest (nest_sh1106 for an SH1106 OLED).
//
// WIRING (config/settings.h, gateway_pins): BH1750 light SDA 21 / SCL 22 (I2C 0x23), OLED on the same
// bus (0x3C), HC-SR04 TRIG 26 / ECHO 27 (through the 1k/2k divider), ACK button GPIO 32 to GND.
// Every part is optional: a missing sensor reports null and never blocks the dolls.
// UPLOAD: config/private.h needs LUMPI_WIFI_SSID, LUMPI_WIFI_PASSWORD, LUMPI_LAN_KEY (same on every
// board), LUMPI_ADAPTER_URL = "https://lumpi.vercel.app" and LUMPI_CLOUD_CA (the site's root certificates).
// EXPECTED on the monitor (115200): the settings, "Nest address http://<ip>" (put it in the dolls'
// LUMPI_GATEWAY_URL), then one line per doll message, "GRADE" lines and "CSV," lines for the tester.
//
// WORDS USED BELOW
//   node     a doll's number: 1 or 2 (0 = the nest itself in dashboard events)
//   attempt  a doll's reading counter: 1, 2, 3 ... (starts again at 1 when the doll restarts)
//   passage  the page being read: 1 to 999
//   uid      the book's RFID tag ID, uppercase hex, 8-20 characters, like 04A1B2C3
//   fsr      the pressure mat's raw reading; higher = heavier
//   chunk    one piece of a recording, up to 8192 bytes = 256 ms of audio
//   command  what the doll acts out: 1 well done, 2 practise a word, 3 read closer, 4 try again,
//            5 too noisy, 6 too dark, 7 teacher told (the doll's own), 8 teacher coming
//   outbox   dashboard events waiting in flash until the cloud has them
//
// THE THREE TASKS (FreeRTOS), highest priority first:
//   http  (3) answers every doll message at once. It never waits for the cloud.
//   room  (2) every second: light, dark room, teacher distance, ACK button, OLED, time limits;
//             every 10 s: room and doll status for the dashboard.
//   cloud (1) sends a finished recording to the AI, fetches word clips, then empties the outbox.
//             Lowest: it waits seconds for the internet, and nobody may wait for it.
// They share dolls[] under one lock (dolls_lock), never held during network or flash work.
#include <Arduino.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <esp_task_wdt.h>

#include "dashboard_event.h"
#include "lumpi_net.h"
#include "messages.h"
#include "monitor.h"
#include "nest_logic.h"
#include "nest_parts.h"
#include "quiet_study_logic.h"
#include "real_io.h"
#include "session.h"
#include "settings.h"
#include "tiny_http.h"

using namespace lumpi;

constexpr uint32_t cloud_wait_ms = 18000;         // the AI's budget (14 s) + upload time
constexpr uint32_t clip_wait_ms = 3000;           // per word clip
constexpr uint32_t event_wait_ms = 5000;          // per dashboard event
constexpr uint32_t outbox_retry_ms = 3000;        // after a failed event, wait before trying again
constexpr uint32_t report_every_ms = 10000;       // room + doll status to the dashboard
constexpr uint32_t doll_quiet_after_ms = 60000;   // no message for 60 s = stop reporting that doll
constexpr uint32_t noise_event_every_ms = 10000;  // at most one ordinary noise point per doll per 10 s
constexpr uint32_t ack_debounce_ms = 30;          // a button's contacts bounce for a few ms
constexpr uint32_t beat_every_ms = 60000;
// A task silent this long restarts the board. Above the cloud task's worst single call:
// connect + TLS handshake + wait for the grade can each take up to 18 s on bad Wi-Fi.
constexpr uint32_t watchdog_s = 90;

// --- what the nest remembers about each doll ----------------------------------------------------
struct ReadingStats {
    uint32_t resent = 0;       // chunks sent twice (the doll missed our ACK)
    uint32_t refused = 0;      // chunks answered 409
    uint32_t flash_ms = 0;     // time spent writing chunks to flash
    uint32_t finished_ms = 0;  // when /v1/finish arrived (queue wait starts)
};

struct Doll {
    Session session;           // the referee: idle -> recording -> assessing -> result (session.h)
    HelpState help;            // teacher call: waiting -> coming
    NoiseState noise;          // quiet mode: when to ask for quiet
    String uid = "";
    uint32_t fsr = 0;
    bool book_on = false;
    bool quiet = false;
    uint32_t last_heard_ms = 0;
    uint32_t graded_ms = 0;
    uint32_t last_noise_event_ms = 0;
    String reply = "";         // the /v1/attempt answer, kept until this doll's next start
    uint32_t clip_words[nest_parts::clips_per_doll] = {};  // page word of each clip file slot
    int clip_count = 0;
    ReadingStats stats;
};

struct GradeJob {
    int node;
    uint32_t attempt;
};

Doll dolls[max_dolls];
SemaphoreHandle_t dolls_lock;
SemaphoreHandle_t outbox_lock;
QueueHandle_t grade_queue;      // http -> cloud: a finished recording to grade
int uploading_node = 0;         // the doll whose recording file the cloud task is reading (under dolls_lock)

volatile bool room_is_dark = false;
volatile float room_lux = NAN;
volatile float room_distance_cm = NAN;

File recording[max_dolls];      // each doll's recording file, used by the http task only
WiFiServer server(80);
tiny_http::Request request;     // the doll message being answered (http task only)

nest_parts::Outbox outbox;      // under outbox_lock
char gateway_id[24];            // "lumpi_" + this chip's MAC address
volatile uint64_t boot_time = 0;  // Unix time when the clock first worked (0 = no clock: no events yet)
uint32_t event_number = 0;      // under outbox_lock

Doll& doll(int node) { return dolls[node - 1]; }
void lock_dolls() { xSemaphoreTake(dolls_lock, portMAX_DELAY); }
void unlock_dolls() { xSemaphoreGive(dolls_lock); }

void stop_here(const char* what, const char* means) {
    monitor::fail(what, means);
    while (true) {
        delay(1000);
    }
}

// =====================================================================================================
// DASHBOARD EVENTS: every event is written to the flash outbox first; the cloud task sends it later.
// =====================================================================================================
dashboard_event::Stamp next_stamp() {
    xSemaphoreTake(outbox_lock, portMAX_DELAY);
    const uint32_t number = ++event_number;
    xSemaphoreGive(outbox_lock);
    return {gateway_id, boot_time, number, lumpi_net::unix_time()};
}

void keep_events(const String& lines) {
    xSemaphoreTake(outbox_lock, portMAX_DELAY);
    const bool kept = nest_parts::outbox_write(outbox, lines);
    xSemaphoreGive(outbox_lock);
    if (!kept) {
        monitor::fail("event not kept", "the flash outbox could not be written (flash full?)");
    }
}

// No clock yet = no events: the dashboard needs real times.
void keep_event(const std::string& json) {
    if (boot_time == 0) {
        return;
    }
    if (json.empty()) {
        monitor::fail("event not made", "a value would break the JSON (the book ID must be uppercase hex)");
        return;
    }
    keep_events(json.c_str());
}

// =====================================================================================================
// HTTP TASK: one function per doll message. Every message carries ?node=<1-2>&...
// =====================================================================================================
struct Reply {
    int code;
    String text;
    String clip_file = "";  // set = send this WAV file instead of text
};
Reply ok(const String& text) { return {200, text}; }
Reply refuse(const String& reason) { return {409, reason}; }

long number(const char* name) { return tiny_http::number_arg(request, name); }  // -1 = missing
String text(const char* name) { return tiny_http::text_arg(request, name); }    // "" = missing

// A new, empty recording file. The old one is removed first: flash has room for only two
// recordings at once, and "w" alone would keep the old blocks until the new file closes.
bool open_recording(int node) {
    recording[node - 1].close();
    LittleFS.remove(nest_parts::recording_path(node));
    recording[node - 1] = LittleFS.open(nest_parts::recording_path(node), "w");
    return bool(recording[node - 1]);
}

Reply on_start(int node) {
    const long attempt = number("attempt"), passage = number("passage"), fsr = number("fsr");
    const String uid = text("uid");
    if (attempt < 0 || fsr < 0) {
        return refuse("start needs attempt, passage, uid and fsr");
    }
    if (!dashboard_event::is_hex_uid(uid.c_str())) {
        return refuse("uid must be uppercase hex, 8-20 characters, like 04A1B2C3");
    }
    if (room_is_dark) {
        return refuse("dark: the room is too dark to read");
    }
    lock_dolls();
    Doll& d = doll(node);
    const bool still_sending = uploading_node == node;
    const bool started = !still_sending && d.session.start(node, attempt, passage, millis());
    if (started) {
        d.uid = uid;
        d.fsr = fsr;
        d.book_on = true;
        d.help.on_start();
        d.reply = "";
        d.clip_count = 0;
        d.stats = ReadingStats();
    }
    unlock_dolls();
    if (still_sending) {
        return refuse("busy: the nest is still sending this doll's last reading");
    }
    if (!started) {
        return refuse("start refused: passage must be 1-999, or this doll's last reading is not finished");
    }
    if (!open_recording(node)) {
        lock_dolls();
        doll(node).session.cancel();
        unlock_dolls();
        return refuse("the nest cannot write its flash: reading cancelled");
    }
    keep_event(dashboard_event::start(next_stamp(), node, uid.c_str(), passage, attempt, fsr));
    return ok("ok");
}

// Written to flash BEFORE the ACK, so an ACKed chunk is never lost.
bool write_chunk(int node) {
    const uint32_t started_ms = millis();
    const size_t written = recording[node - 1].write(request.body, request.body_length);
    lock_dolls();
    doll(node).stats.flash_ms += millis() - started_ms;
    unlock_dolls();
    return written == request.body_length;
}

Reply on_chunk(int node) {
    const long attempt = number("attempt"), index = number("index");
    if (attempt < 0 || index < 0) {
        return refuse("chunk needs attempt and index");
    }
    lock_dolls();
    Doll& d = doll(node);
    const ChunkResult result = d.session.add_chunk(node, attempt, index, request.body_length, millis());
    if (result == ChunkResult::already_had_it) {
        d.stats.resent += 1;
    }
    if (result == ChunkResult::rejected) {
        d.stats.refused += 1;
    }
    unlock_dolls();
    if (result == ChunkResult::rejected) {
        return refuse("chunk refused: old attempt, out of order, odd size, or over 10 s of audio");
    }
    if (result == ChunkResult::stored && !write_chunk(node)) {
        lock_dolls();
        doll(node).session.cancel();
        unlock_dolls();
        return refuse("the nest's flash is full: reading cancelled");
    }
    return ok(String(index));
}

Reply on_finish(int node) {
    const long attempt = number("attempt"), chunks = number("chunks"), bytes = number("bytes");
    lock_dolls();
    Doll& d = doll(node);
    const bool finished = attempt >= 0 && d.session.finish(node, attempt, chunks, bytes, millis());
    const String counts = String(d.session.chunks) + " chunks, " + d.session.bytes + " bytes";
    if (finished) {
        d.stats.finished_ms = millis();
    }
    unlock_dolls();
    if (!finished) {
        return refuse("finish refused: unknown attempt, not recording, or totals differ (the nest has " + counts + ")");
    }
    recording[node - 1].close();  // flushes the last chunk to flash
    const GradeJob job = {node, uint32_t(attempt)};
    xQueueSend(grade_queue, &job, 0);
    return ok("assessing");
}

Reply on_attempt(int node) {
    lock_dolls();
    Doll& d = doll(node);
    const bool known = d.session.owned_by(node, number("attempt"));
    const bool graded = d.session.state == State::result;
    const String answer = d.reply;
    unlock_dolls();
    if (!known) {
        return refuse("unknown attempt (an old one, or the nest restarted)");
    }
    return ok(graded ? answer : String("pending"));
}

Reply on_word_clip(int node) {
    const long word = number("word");
    lock_dolls();
    Doll& d = doll(node);
    int slot = -1;
    if (d.session.owned_by(node, number("attempt")) && d.session.state == State::result) {
        for (int i = 0; i < d.clip_count; ++i) {
            if (long(d.clip_words[i]) == word) {
                slot = i;
            }
        }
    }
    unlock_dolls();
    if (slot < 0) {
        return refuse("no clip for that word");
    }
    return {200, "", nest_parts::clip_path(node, slot)};
}

Reply on_cancel(int node) {
    lock_dolls();
    if (doll(node).session.owned_by(node, number("attempt"))) {
        doll(node).session.cancel();
    }
    unlock_dolls();
    return ok("cancelled");
}

// The doll's heartbeat every 3 s, and its fast way to learn the room is dark.
Reply on_presence(int node) {
    const long present = number("present");
    if (present != 0 && present != 1) {
        return refuse("present must be 0 or 1");
    }
    const bool quiet = text("mode") == "quiet";
    lock_dolls();
    Doll& d = doll(node);
    const bool changed = bool(present) != d.book_on || quiet != d.quiet;
    d.book_on = present;
    d.quiet = quiet;
    d.noise.child_present = present || quiet;  // quiet mode counts the child as present
    if (number("fsr") >= 0) {
        d.fsr = number("fsr");
    }
    unlock_dolls();
    if (changed) {
        Serial.printf("  doll %d  presence   book %s, %s mode\n", node, present ? "ON" : "OFF", quiet ? "quiet" : "read");
    }
    return ok(room_is_dark ? "dark" : "ok");
}

// The teacher call. The first ask is the call; the doll then asks every 3 s until "coming".
Reply on_help(int node) {
    bool new_call = false;
    lock_dolls();
    const char* answer = nest_logic::help_reply(doll(node).help, room_is_dark, millis(), new_call);
    unlock_dolls();
    if (new_call) {
        Serial.printf("  doll %d  HELP       the child calls the teacher: press ACK on the nest\n", node);
        keep_event(dashboard_event::help(next_stamp(), node));
    }
    return ok(answer);
}

// Quiet mode, every 1 s: the doll's noise level. "command,5" = the doll asks the class for quiet.
Reply on_noise(int node) {
    const long peak = number("peak");
    const float db = text("db").isEmpty() ? NAN : text("db").toFloat();
    if (peak < 0) {
        return refuse("noise needs peak");
    }
    lock_dolls();
    Doll& d = doll(node);
    d.noise.session_active = nest_logic::reading_in_progress(d.session.state);
    const String answer = nest_logic::noise_reply(d.noise, room_is_dark, peak, millis());
    const bool warned = answer == "command,5";
    const bool send_point = answer != "dark" && (warned || millis() - d.last_noise_event_ms >= noise_event_every_ms);
    if (send_point) {
        d.last_noise_event_ms = millis();
    }
    unlock_dolls();
    Serial.printf("CSV,noise,%d,%ld,%.1f,%d\n", node, peak, db, warned ? 1 : 0);
    if (send_point) {
        keep_event(dashboard_event::noise(next_stamp(), node, db));
    }
    return ok(answer);
}

// For people: every doll's step as a number, 0 idle, 1 recording, 2 assessing, 3 result.
Reply on_status() {
    String json = "{\"dark\":" + String(room_is_dark ? "true" : "false") + ",\"dolls\":[";
    lock_dolls();
    for (int node = 1; node <= int(max_dolls); ++node) {
        const Doll& d = doll(node);
        json += String(node > 1 ? "," : "") + "{\"node\":" + node + ",\"state\":" + int(d.session.state) +
                ",\"attempt\":" + d.session.attempt + ",\"book_on\":" + (d.book_on ? "true" : "false") + "}";
    }
    unlock_dolls();
    return ok(json + "]}");
}

Reply answer() {
    if (request.authorization != String("Bearer ") + LUMPI_LAN_KEY) {
        return {401, "wrong LUMPI_LAN_KEY"};
    }
    const String& path = request.path;
    if (path == route::status) {
        return on_status();
    }
    const long node = number("node");
    if (node < 1 || node > long(max_dolls)) {
        return refuse("node must be 1 or 2");
    }
    lock_dolls();
    doll(node).last_heard_ms = millis();
    unlock_dolls();
    if (path == route::start)     return on_start(node);
    if (path == route::chunk)     return on_chunk(node);
    if (path == route::finish)    return on_finish(node);
    if (path == route::attempt)   return on_attempt(node);
    if (path == route::word_clip) return on_word_clip(node);
    if (path == route::cancel)    return on_cancel(node);
    if (path == route::presence)  return on_presence(node);
    if (path == route::help)      return on_help(node);
    if (path == route::noise)     return on_noise(node);
    return {404, "unknown route"};
}

// One line per doll message. Quiet on purpose: chunks, heartbeats, noise and "pending".
void log_request(const Reply& reply) {
    const String& path = request.path;
    const bool routine = path == route::chunk || path == route::presence || path == route::noise ||
                         (path == route::attempt && reply.text == "pending");
    if (reply.code == 200 && routine) {
        return;
    }
    const String shown = reply.clip_file.isEmpty() ? reply.text.substring(0, reply.text.indexOf('\n')) : reply.clip_file;
    Serial.printf("  doll %s  %-9s  %-44s -> %d %s\n", text("node").c_str(), path.substring(path.lastIndexOf('/') + 1).c_str(),
                  request.query.c_str(), reply.code, shown.c_str());
}

void answer_one_request() {
    WiFiClient client = server.available();
    if (!client) {
        return;
    }
    if (tiny_http::read_request(client, request)) {
        Reply reply = answer();
        if (reply.clip_file.isEmpty()) {
            tiny_http::reply(client, reply.code, reply.text);
        } else if (!nest_parts::reply_file(client, reply.clip_file, "audio/wav")) {
            reply = refuse("clip file missing from flash");
            tiny_http::reply(client, reply.code, reply.text);
        }
        log_request(reply);
    }
    client.stop();
}

void http_task(void*) {
    esp_task_wdt_add(nullptr);
    while (true) {
        esp_task_wdt_reset();
        answer_one_request();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

// =====================================================================================================
// CLOUD TASK: grade one finished recording, then send waiting dashboard events.
// =====================================================================================================
struct Reading {
    String uid;
    uint32_t passage, bytes, chunks;
    ReadingStats stats;
};

// Still waiting for its grade? (The doll may have cancelled, or the 45 s limit passed.)
bool claim_reading(const GradeJob& job, Reading& reading) {
    lock_dolls();
    Doll& d = doll(job.node);
    const bool waiting = d.session.state == State::assessing && d.session.owned_by(job.node, job.attempt);
    if (waiting) {
        reading = {d.uid, d.session.passage, d.session.bytes, d.session.chunks, d.stats};
        uploading_node = job.node;
    }
    unlock_dolls();
    return waiting;
}

// Streams the recording file straight from flash to the AI: the audio never sits in RAM.
int send_recording(const GradeJob& job, const Reading& reading, String& cloud_reply) {
    File file = LittleFS.open(nest_parts::recording_path(job.node), "r");
    if (!file || file.size() != reading.bytes) {
        cloud_reply = "recording file missing or the wrong size";
        return -1;
    }
    const String url = String(LUMPI_ADAPTER_URL) + laptop_route::assess + "?node=" + job.node + "&attempt=" +
                       job.attempt + "&passage=" + reading.passage + "&uid=" + reading.uid;
    const int code = lumpi_net::request("POST", url, file, reading.bytes, "application/octet-stream", cloud_reply,
                                        cloud_wait_ms);
    file.close();
    return code;
}

// For a word to practise: fetch up to 3 word clips into flash BEFORE the doll hears the grade.
int fetch_clips(int node, const String& cloud_reply, uint32_t worst_word, uint32_t held[]) {
    nest_logic::ClipToFetch wanted[nest_logic::max_clips];
    const int count = nest_logic::clips_to_fetch(cloud_reply.c_str(), worst_word, wanted);
    int held_count = 0;
    for (int i = 0; i < count; ++i) {
        esp_task_wdt_reset();
        const String url = String(LUMPI_ADAPTER_URL) + wanted[i].path.c_str();
        if (nest_parts::download_clip(url, nest_parts::clip_path(node, held_count), clip_wait_ms)) {
            held[held_count] = wanted[i].word;
            held_count += 1;
        } else {
            Serial.printf("  doll %d  clip       word %u not fetched: the doll shows it on the OLED only\n", node,
                          unsigned(wanted[i].word));
        }
    }
    return held_count;
}

void publish_result(const GradeJob& job, uint32_t command, uint32_t worst_word, const std::string& reply,
                    const uint32_t held[], int held_count) {
    lock_dolls();
    Doll& d = doll(job.node);
    const bool landed = d.session.got_result(job.node, job.attempt, command, worst_word);
    if (landed) {
        d.reply = reply.c_str();
        d.graded_ms = millis();
        d.clip_count = held_count;
        for (int i = 0; i < held_count; ++i) {
            d.clip_words[i] = held[i];
        }
    }
    uploading_node = 0;
    unlock_dolls();
    if (!landed) {
        Serial.printf("  doll %d  grade      too late: the doll cancelled or the 45 s limit passed\n", job.node);
    }
}

void grade(const GradeJob& job) {
    Reading reading;
    if (!claim_reading(job, reading)) {
        return;
    }
    const uint32_t queue_wait_ms = millis() - reading.stats.finished_ms;
    const uint32_t sent_ms = millis();
    String cloud_reply;
    const int http_code = send_recording(job, reading, cloud_reply);
    const uint32_t cloud_ms = millis() - sent_ms;

    nest_logic::Grade ai;
    if (http_code == 200) {
        ai = nest_logic::read_grade(cloud_reply.c_str());
    }
    if (!ai.ok) {
        Serial.printf("  doll %d  grade      the cloud answered %d %s: the doll gets 4 (try again)\n", job.node, http_code,
                      cloud_reply.substring(0, 60).c_str());
    }
    const uint32_t ai_code = ai.ok ? ai.code : uint32_t(uncertain);
    const uint32_t command = decide_command(ai_code, room_is_dark);  // a dark room overrides any grade

    uint32_t held[nest_logic::max_clips];
    const int held_count = command == retry_word ? fetch_clips(job.node, cloud_reply, ai.worst_word, held) : 0;
    const std::string reply = nest_logic::doll_reply(cloud_reply.c_str(), command, ai.worst_word, held, held_count);
    publish_result(job, command, ai.worst_word, reply, held, held_count);
    LittleFS.remove(nest_parts::recording_path(job.node));  // graded: free its flash for the other doll

    Serial.printf("  doll %d  GRADE      command %u, word %u, %d clips (cloud %u ms)\n", job.node, unsigned(command),
                  unsigned(ai.worst_word), held_count, unsigned(cloud_ms));
    Serial.printf("CSV,reading,%d,%u,%u,%u,%u,%u,%u,%u,%d,%u,%u,%d,%u\n", job.node, unsigned(job.attempt),
                  unsigned(reading.bytes), unsigned(reading.chunks), unsigned(reading.stats.resent),
                  unsigned(reading.stats.refused), unsigned(queue_wait_ms), unsigned(cloud_ms), http_code,
                  unsigned(ai_code), unsigned(command), held_count, unsigned(reading.stats.flash_ms));
}

// A value from an event's JSON text, for the CSV line: "event_type", "node_id" or "recorded_at".
String json_value(const String& json, const char* key) {
    const int at = json.indexOf(String("\"") + key + "\":");
    if (at < 0) {
        return "";
    }
    const int start = at + strlen(key) + 3;
    int end = start;
    while (end < int(json.length()) && json[end] != ',' && json[end] != '}') {
        end += 1;
    }
    String value = json.substring(start, end);
    value.replace("\"", "");
    return value;
}

// 200 = stored. 400 / 401 = refused for good: drop it (log the raw JSON). Anything else: send again later.
bool send_event(const String& json, uint32_t tries) {
    String answer;
    const int code = lumpi_net::request("POST", String(LUMPI_ADAPTER_URL) + "/api/telemetry",
                                        reinterpret_cast<const uint8_t*>(json.c_str()), json.length(),
                                        "application/json", answer, event_wait_ms);
    const uint64_t recorded_at = strtoull(json_value(json, "recorded_at").c_str(), nullptr, 10);
    Serial.printf("CSV,event,%s,%s,%d,%u,%llu\n", json_value(json, "event_type").c_str(),
                  json_value(json, "node_id").c_str(), code, unsigned(tries), lumpi_net::unix_time() - recorded_at);
    if (code == 400 || code == 401) {
        Serial.printf("  nest    event      refused for good (%d %s): %s\n", code, answer.c_str(), json.c_str());
    }
    return code == 200 || code == 400 || code == 401;
}

// Oldest batch first. A new recording to grade interrupts it (the batch is sent again whole;
// the cloud answers "duplicate" for events it already has).
void send_oldest_events() {
    static uint32_t failed_ms = 0;
    static uint32_t tries = 0;
    if (!lumpi_net::connected() || millis() - failed_ms < outbox_retry_ms) {
        return;
    }
    xSemaphoreTake(outbox_lock, portMAX_DELAY);
    const bool empty = outbox.count() == 0;
    const uint32_t sending = outbox.oldest;
    const String lines = empty ? String() : nest_parts::outbox_read_oldest(outbox);
    xSemaphoreGive(outbox_lock);
    if (empty) {
        return;
    }
    tries += 1;
    int start = 0;
    while (start < int(lines.length())) {
        int end = lines.indexOf('\n', start);
        end = end < 0 ? lines.length() : end;
        esp_task_wdt_reset();
        if (!send_event(lines.substring(start, end), tries)) {
            failed_ms = millis();
            return;
        }
        if (uxQueueMessagesWaiting(grade_queue) > 0) {
            return;
        }
        start = end + 1;
    }
    // A full outbox may have dropped this batch while it was being sent: then remove nothing,
    // or the next, unsent batch would go.
    xSemaphoreTake(outbox_lock, portMAX_DELAY);
    if (outbox.oldest == sending) {
        nest_parts::outbox_remove_oldest(outbox);
    }
    xSemaphoreGive(outbox_lock);
    tries = 0;
}

void cloud_task(void*) {
    esp_task_wdt_add(nullptr);
    while (true) {
        esp_task_wdt_reset();
        GradeJob job;
        if (xQueueReceive(grade_queue, &job, pdMS_TO_TICKS(200)) == pdTRUE) {
            grade(job);
        } else {
            send_oldest_events();
        }
    }
}

// =====================================================================================================
// ROOM TASK: the nest's own sensors and real-time decisions, every second.
// =====================================================================================================
DarkState dark_state;

void read_room() {
    float lux = NAN;
    if (!hardware::light_read(lux)) {
        lux = NAN;  // no sensor: never dark, never blocks
    }
    room_lux = lux;
    room_distance_cm = hardware::read_distance_cm();
    const bool was_dark = room_is_dark;
    room_is_dark = dark_state.update(lux, room::dark_lux, room::bright_lux, room::dark_hold_ms, millis());
    if (room_is_dark != was_dark) {
        Serial.printf("  nest    ROOM       %s (%.0f lx)\n", room_is_dark ? "DARK: dolls blocked" : "light again", lux);
        Serial.printf("CSV,dark,0,%d,%.1f\n", room_is_dark ? 1 : 0, lux);
    }
}

// The ACK button: one press tells every waiting doll "coming".
void handle_ack_press() {
    vTaskDelay(pdMS_TO_TICKS(ack_debounce_ms));
    ulTaskNotifyTake(pdTRUE, 0);  // forget the bounces
    if (digitalRead(gateway_pins::ack) != LOW) {
        return;
    }
    for (int node = 1; node <= int(max_dolls); ++node) {
        uint32_t waited_s = 0;
        lock_dolls();
        const bool answered = doll(node).help.ack(millis(), waited_s);
        unlock_dolls();
        if (answered) {
            Serial.printf("  doll %d  ACK        the teacher is coming (waited %u s)\n", node, unsigned(waited_s));
            Serial.printf("CSV,ack,%d,%u\n", node, unsigned(waited_s));
        }
    }
}

// A reading stuck too long (doll switched off mid-reading?) is dropped.
void drop_stuck_readings() {
    for (int node = 1; node <= int(max_dolls); ++node) {
        lock_dolls();
        Session& s = doll(node).session;
        const State before = s.state;
        s.check_timeouts(millis());
        const bool dropped = s.state != before;
        unlock_dolls();
        if (dropped) {
            Serial.printf("  doll %d  TIMEOUT    silent too long mid-reading: the reading is dropped\n", node);
        }
    }
}

bool a_reading_in_flight() {
    bool busy = false;
    lock_dolls();
    for (Doll& d : dolls) {
        busy = busy || nest_logic::reading_in_progress(d.session.state);
    }
    unlock_dolls();
    return busy;
}

void start_clock_once() {
    if (boot_time == 0 && lumpi_net::unix_time() != 0) {
        boot_time = lumpi_net::unix_time();
        Serial.println("  nest    clock      ok: dashboard events start now");
    }
}

void say_address_once() {
    static bool said = false;
    if (!said && lumpi_net::connected()) {
        said = true;
        monitor::item("Nest address", "http://%s  <- LUMPI_GATEWAY_URL for the dolls", WiFi.localIP().toString().c_str());
        monitor::section("MESSAGES (who, message, raw query -> answer)");
    }
}

String doll_line(int node, const Doll& d) {
    const String name = "Doll " + String(node) + ": ";
    if (d.last_heard_ms == 0 || millis() - d.last_heard_ms > doll_quiet_after_ms) {
        return name + "offline";
    }
    if (d.help.answer() == 1) {
        return name + "HELP " + String((millis() - d.help.stuck_since_ms) / 1000) + " s";
    }
    if (nest_logic::reading_in_progress(d.session.state)) {
        return name + "reading";
    }
    return name + (d.quiet ? "quiet mode" : d.book_on ? "book on" : "idle");
}

// The teacher's private screen: on only while someone stands nearer than teacher_margin_cm,
// so a child's teacher call is not shown to the class.
void show_teacher_screen() {
    static bool blank = false;
    if (!(room_distance_cm < room::teacher_margin_cm)) {
        if (!blank) {
            nest_parts::oled_show(nullptr, 0);  // cleared once, not every second: the I2C bus stays free
            blank = true;
        }
        return;
    }
    blank = false;
    String lines[5];
    lines[0] = String("LUMPI nest  ") + (lumpi_net::connected() ? "online" : "NO WI-FI");
    lines[1] = room_is_dark ? String("DARK: turn on light") : "Light " + String(room_lux, 0) + " lx";
    lock_dolls();
    lines[2] = doll_line(1, doll(1));
    lines[3] = doll_line(2, doll(2));
    const bool someone_waits = doll(1).help.answer() == 1 || doll(2).help.answer() == 1;
    unlock_dolls();
    lines[4] = someone_waits ? "Press ACK = coming" : "";
    nest_parts::oled_show(lines, 5);
}

// Every 10 s: the room, and each doll heard from in the last 60 s, as one outbox batch.
void report_room_and_dolls() {
    if (boot_time == 0) {
        return;
    }
    String batch = dashboard_event::room(next_stamp(), room_lux, room_distance_cm).c_str();
    for (int node = 1; node <= int(max_dolls); ++node) {
        lock_dolls();
        const Doll& d = doll(node);
        const bool heard = d.last_heard_ms != 0 && millis() - d.last_heard_ms <= doll_quiet_after_ms;
        const char* activity = dashboard_event::activity_for(d.session.state, d.book_on, millis() - d.graded_ms, d.quiet);
        const String uid = d.uid;
        const uint32_t passage = d.session.passage, attempt = d.session.attempt, fsr = d.fsr;
        unlock_dolls();
        if (heard) {
            batch += "\n";
            batch += dashboard_event::status(next_stamp(), node, activity, uid.c_str(), passage, attempt, fsr).c_str();
        }
    }
    keep_events(batch);
}

uint32_t outbox_pending() {
    xSemaphoreTake(outbox_lock, portMAX_DELAY);
    const uint32_t count = outbox.count();
    xSemaphoreGive(outbox_lock);
    return count;
}

void room_task(void*) {
    esp_task_wdt_add(nullptr);
    uint32_t last_report_ms = 0, last_beat_ms = 0;
    while (true) {
        esp_task_wdt_reset();
        const bool ack_pressed = ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) > 0;
        if (ack_pressed) {
            handle_ack_press();
        }
        read_room();
        drop_stuck_readings();
        start_clock_once();
        say_address_once();
        nest_parts::wifi_watch(a_reading_in_flight());
        show_teacher_screen();
        if (millis() - last_report_ms >= report_every_ms) {
            last_report_ms = millis();
            report_room_and_dolls();
        }
        if (millis() - last_beat_ms >= beat_every_ms) {
            last_beat_ms = millis();
            nest_parts::csv_beat(outbox_pending());
        }
    }
}

// =====================================================================================================
// START
// =====================================================================================================
void show_settings() {
    monitor::title("LUMPI NEST", "answers the dolls, grades readings with the cloud, reports to the dashboard",
                   "each reading ends with a GRADE line; CSV lines are for the test sheet");
    monitor::section("SETTINGS (config/private.h)");
    monitor::item("Wi-Fi name", "%s", LUMPI_WIFI_SSID[0] ? LUMPI_WIFI_SSID : "(EMPTY)");
    monitor::item("Cloud", "%s", LUMPI_ADAPTER_URL);
    monitor::item("LAN key", "%u characters (not shown)", unsigned(strlen(LUMPI_LAN_KEY)));
    monitor::item("CA set", "%s", LUMPI_CLOUD_CA[0] ? "yes" : "NO: the cloud's certificate is not checked");
    if (strncmp(LUMPI_ADAPTER_URL, "http", 4) != 0) {
        stop_here("no cloud address", "set LUMPI_ADAPTER_URL in config/private.h, e.g. \"https://lumpi.vercel.app\"");
    }
}

void start_parts() {
    monitor::section("PARTS (all optional)");
    Wire.begin(gateway_pins::sda, gateway_pins::scl);
    monitor::item("Light (BH1750)", "%s", hardware::light_begin() ? "answered at 0x23" : "no answer: never blocks");
    nest_parts::oled_begin();
    monitor::item("Teacher screen", "%s", nest_parts::oled_ok ? "answered at 0x3C" : "no answer");
    pinMode(gateway_pins::trig, OUTPUT);
    pinMode(gateway_pins::echo, INPUT);
    snprintf(gateway_id, sizeof(gateway_id), "lumpi_%012llx", ESP.getEfuseMac());
    monitor::item("Nest ID", "%s", gateway_id);
}

void start_storage() {
    monitor::section("FLASH");
    if (!nest_parts::begin_storage()) {
        stop_here("flash file system", "LittleFS did not start: check board_build.partitions in platformio.ini");
    }
    nest_parts::outbox_find(outbox);
    monitor::item("Free flash", "%u KB (2 recordings + 6 clips need 920 KB)", unsigned(nest_parts::free_kb()));
    monitor::item("Flash write speed", "%u KB/s (2 dolls need 64 KB/s)", unsigned(nest_parts::flash_write_kb_per_s()));
    monitor::item("Outbox", "%u batches waiting from before", unsigned(outbox.count()));
    monitor::item("Largest free RAM block", "%u bytes (TLS needs about 45000)",
                  unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)));
    nest_parts::csv_boot(outbox.count());
}

void start_tasks() {
    dolls_lock = xSemaphoreCreateMutex();
    outbox_lock = xSemaphoreCreateMutex();
    grade_queue = xQueueCreate(8, sizeof(GradeJob));
    esp_task_wdt_init(watchdog_s, true);
    TaskHandle_t room = nullptr;
    // Wi-Fi and TLS run on core 0, so the cloud task sits there; the dolls' answers run on core 1.
    xTaskCreatePinnedToCore(http_task, "http", 8192, nullptr, 3, nullptr, 1);
    xTaskCreatePinnedToCore(room_task, "room", 6144, nullptr, 2, &room, 1);
    xTaskCreatePinnedToCore(cloud_task, "cloud", 12288, nullptr, 1, nullptr, 0);
    nest_parts::attach_ack_button(room);
}

void setup() {
    Serial.begin(115200);
    delay(500);
    show_settings();
    start_parts();
    start_storage();
    monitor::section("CONNECTION");
    if (!lumpi_net::begin()) {
        stop_here("Wi-Fi settings", "fill LUMPI_WIFI_SSID, LUMPI_WIFI_PASSWORD and LUMPI_LAN_KEY in config/private.h");
    }
    server.begin();
    start_tasks();
}

// Everything runs in the three tasks above.
void loop() {
    vTaskDelete(nullptr);
}
