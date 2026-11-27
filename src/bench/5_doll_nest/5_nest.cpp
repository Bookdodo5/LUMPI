// THE NEST, bench version. Stages 5-8 of docs/testing-steps.md. Upload environment: s5_nest.
//
// LUMPI in one breath: a child reads a book aloud to a doll on their desk. The doll records
// the reading and sends it over Wi-Fi to the nest (one per classroom). The nest sends it to
// the cloud AI, gets back a grade, and hands the grade to the doll. The nest also tells the
// teacher's dashboard what is going on.
//
//     doll --Wi-Fi--> NEST --internet--> cloud AI          NEST --> teacher's dashboard
//
// WORDS USED BELOW
//   node     a doll's number: 1, 2 or 3
//   attempt  a doll's reading counter: 1, 2, 3 ... (starts again at 1 when the doll restarts)
//   passage  the page being read: 1, 2 or 3
//   uid      the book's RFID tag ID, in uppercase hex, like 04A1B2C3
//   fsr      the pressure mat's raw reading (a force-sensitive resistor); higher = heavier
//   chunk    one piece of the recording, at most 2048 bytes, sent as one web request
//   grade    the AI's verdict, a number: 1 accepted, 2 retry a word, 3 read louder or closer,
//            4 uncertain (try again), 6 too dark, 7 the teacher was told
//   LAN key  the shared password every board and server checks (LUMPI_LAN_KEY)
// In code names the nest is still called "gateway" (gateway_pins, LUMPI_GATEWAY_URL ...), and
// LUMPI_ADAPTER_URL means "where the nest sends grades and events": the laptop or the cloud.
//
// ONE READING, message by message. Every message carries ?node=<doll 1-3>&...
//   doll: POST /v1/start   attempt=7 passage=1 uid=04A1B2C3 fsr=2500   nest: "ok"
//   doll: PUT  /v1/chunk   attempt=7 index=0 + 2048 bytes     nest: "0"  (the index back = "got it")
//   doll: PUT  /v1/chunk   attempt=7 index=1 + 2048 bytes     nest: "1"  ... until all audio is sent
//   doll: POST /v1/finish  attempt=7 chunks=47 bytes=96000    nest: "assessing"
//   nest -> cloud: the whole recording                         cloud: "1,0" (grade, word to retry)
//   doll: GET  /v1/attempt attempt=7 (= "how did attempt 7 go?")  nest: "pending" ... "result,1,0"
//   While the nest waits for the cloud it answers nobody, so that question simply waits too.
// Smaller messages: /v1/presence (is a book on the mat?), /v1/help, /v1/noise, /v1/cancel,
// /v1/status. A message that is wrong or out of order gets 409 and the reason in words.
//
// HOW TO RUN
//   config/private.h: LUMPI_WIFI_SSID, LUMPI_WIFI_PASSWORD, LUMPI_LAN_KEY (the same on every
//   board), and LUMPI_ADAPTER_URL, where grades come from and events go:
//     ""                           no cloud: every reading gets grade 1 "accepted" (stage 5)
//     "http://<laptop>:8080"       laptop dashboard; it has no AI, so grades are 4 "uncertain"
//     "http://<laptop>:8081"       the cloud code running on the laptop (python3 api/index.py)
//     "https://<site>.vercel.app"  the real cloud
//   Monitor at 115200. The nest prints its address: put it in LUMPI_GATEWAY_URL for the dolls.
//   No doll board yet? The laptop can play one: python3 tests/fake_doll.py
//
// WIRING: config/settings.h, gateway_pins. Every part is optional: a missing sensor reports null.
//
// BENCH LIMITS (shortcuts taken on purpose; "ponytail" in a comment marks one): one doll records at a time, because there is one 96 kB buffer (3 s
// of audio). Events wait in a 16-slot queue in memory and are lost on restart.
// The real nest needs a buffer per doll (PSRAM) or to stream the audio on to the cloud.
#include <Arduino.h>
#include <HTTPClient.h>        // listed here so PlatformIO finds the libraries lumpi_net.h uses
#include <WiFiClientSecure.h>
#include <Wire.h>
#include <deque>

#include "dashboard_event.h"        // builds the dashboard's JSON events
#include "lumpi_net.h"              // Wi-Fi + sending one request to the cloud
#include "monitor.h"                // the monitor layout: title, sections, "Label ..... value"
#include "messages.h"               // route names (/v1/start ...) and grade numbers (accepted = 1 ...)
#include "real_io.h"                // light + distance sensors
#include "session.h"                // Session: the rules for one doll's reading
#include "settings.h"               // pins and private.h
#include "tiny_http.h"              // reads one request from a doll

using namespace lumpi;

constexpr size_t audio_capacity = 96000;        // 3 s at 16 000 samples/s, 2 bytes each
constexpr uint32_t cloud_wait_ms = 12000;       // how long the nest waits for a grade
constexpr uint32_t report_every_ms = 10000;     // room + doll status to the dashboard
constexpr uint32_t doll_quiet_after_ms = 60000; // no message for 60 s = stop reporting that doll
constexpr size_t queue_max = 16;                // dashboard events waiting to be sent

// --- what the nest remembers ------------------------------------------------------------
// Session (src/common/session.h) is the referee for one doll's reading. It knows which step
// the reading is at,  idle -> recording -> assessing -> result,  and its functions
// (start, add_chunk, finish, got_result) say no to a message that comes out of order.
struct Doll {
    Session session;
    String uid = "";            // the book's RFID tag ("" = no book seen yet)
    long fsr = 0;               // pressure mat reading (higher = heavier)
    bool book_on = false;       // is a book on the mat?
    uint32_t last_heard_ms = 0; // when this doll last sent anything (0 = never)
    uint32_t graded_ms = 0;     // when its last grade arrived
    uint32_t started_ms = 0;    // when its current reading started (for the upload time)
    int resent = 0;             // chunks the doll sent twice (it missed our "got it")
    bool help_wanted = false;   // the child asked for help ...
    bool help_coming = false;   // ... and the teacher pressed the ACK button
};

Doll dolls[max_dolls];
Doll& doll(int node) { return dolls[node - 1]; }  // dolls are numbered 1-3

uint8_t* audio = nullptr;       // the recording being received (one doll at a time)
int doll_to_grade = 0;          // set by /v1/finish; loop() sends that recording to the cloud
tiny_http::Request request;     // the request being answered right now
WiFiServer server(80);

char gateway_id[24];            // "lumpi_" + this chip's MAC address
uint64_t boot_time = 0;         // Unix time when the clock first worked, soon after start (0 = no clock yet)
uint32_t event_number = 0;
std::deque<String> event_queue; // dashboard events waiting to be sent, oldest first

bool has_cloud() { return LUMPI_ADAPTER_URL[0] != '\0'; }

// Shortcuts for reading the current request's ?name=value pairs.
long number(const char* name) { return tiny_http::number_arg(request, name); }  // -1 = missing
String text(const char* name) { return tiny_http::text_arg(request, name); }    // "" = missing

// --- dashboard events --------------------------------------------------------------------
dashboard_event::Stamp next_stamp() {
    return {gateway_id, boot_time, ++event_number, lumpi_net::unix_time()};
}

// Puts an event in the queue. send_one_event() sends it later, when no audio is moving.
void queue_event(const std::string& json) {
    if (!has_cloud() || boot_time == 0) {
        return;  // nowhere to send it, or no clock yet (the dashboard needs real times)
    }
    if (json.empty()) {
        monitor::fail("dashboard event not made", "a value would break the JSON (the book ID must be uppercase hex)");
        return;
    }
    if (event_queue.size() == queue_max) {
        Serial.println("  nest    dashboard  queue full (16): the oldest event is dropped");
        event_queue.pop_front();
    }
    event_queue.push_back(json.c_str());
}

bool a_doll_is_recording() {
    for (Doll& d : dolls) {
        if (d.session.state == State::recording) {
            return true;
        }
    }
    return false;
}

void send_one_event() {
    static uint32_t failed_ms = 0;
    if (event_queue.empty() || a_doll_is_recording() || millis() - failed_ms < 3000) {
        return;  // nothing to send, audio comes first, or wait 3 s after a failure
    }
    String answer;
    const String& json = event_queue.front();
    const int code = lumpi_net::request("POST", String(LUMPI_ADAPTER_URL) + "/api/telemetry",
                                        reinterpret_cast<const uint8_t*>(json.c_str()), json.length(),
                                        "application/json", answer);
    // 4xx = the server will never accept this event (401 = LUMPI_LAN_KEY differs): drop it,
    // so it does not block the rest. Except 408 and 429, which mean "try again later".
    const bool refused_for_good = code >= 400 && code < 500 && code != 408 && code != 429;
    const int type_at = json.indexOf("\"event_type\":\"") + 14;
    const String type = json.substring(type_at, json.indexOf('"', type_at));
    if (code == 200) {
        Serial.printf("  nest    dashboard  %-48s -> 200 %s\n", type.c_str(), answer.c_str());
        event_queue.pop_front();
    } else if (refused_for_good) {
        Serial.printf("  nest    dashboard  %-48s -> %d %s: dropped\n", type.c_str(), code, answer.c_str());
        Serial.printf("           raw JSON: %s\n", json.c_str());
        event_queue.pop_front();
    } else {
        Serial.printf("  nest    dashboard  %-48s -> %d %s: trying again in 3 s\n", type.c_str(), code, answer.c_str());
        failed_ms = millis();
    }
}

// --- answering the dolls: one function per message ------------------------------------------
struct Reply {
    int code;
    String text;
};
Reply ok(const String& text) { return {200, text}; }
Reply refuse(const String& reason) { return {409, reason}; }

Reply on_start(int node, Doll& d) {
    const long attempt = number("attempt"), passage = number("passage"), fsr = number("fsr");
    const String uid = text("uid");
    if (attempt < 0 || fsr < 0 || passage < 1 || passage > 3) {
        return refuse("start needs attempt, passage 1-3 and fsr");
    }
    if (!dashboard_event::is_hex_uid(uid.c_str())) {
        return refuse("uid must be uppercase hex, like 04A1B2C3");
    }
    if (a_doll_is_recording() && d.session.state != State::recording) {
        return refuse("busy: another doll is recording (the bench nest takes one at a time)");
    }
    if (!d.session.start(node, attempt, passage, millis())) {
        return refuse("busy: this doll's last reading is not finished");
    }
    d.uid = uid;
    d.fsr = fsr;
    d.book_on = true;
    d.help_wanted = d.help_coming = false;
    d.started_ms = millis();
    d.resent = 0;
    queue_event(dashboard_event::start(next_stamp(), node, uid.c_str(), passage, attempt, fsr));
    return ok("ok");
}

Reply on_chunk(int node, Doll& d) {
    const long attempt = number("attempt"), index = number("index");
    const size_t size = request.body_length;
    Session& s = d.session;
    if (attempt < 0 || index < 0 || size == 0) {
        return refuse("chunk needs attempt, index and some audio");
    }
    // s.chunks and s.bytes count what is already stored. A lower index = a re-send of a chunk we have.
    const bool is_new_piece = index >= long(s.chunks);
    if (is_new_piece && s.bytes + size > audio_capacity) {
        return refuse("too long: the bench nest keeps 3 s");
    }
    const size_t write_at = s.bytes;
    switch (s.add_chunk(node, attempt, index, size, millis())) {
    case ChunkResult::stored:
        memcpy(audio + write_at, request.body, size);
        return ok(String(index));
    case ChunkResult::already_had_it:  // the doll missed our "got it": say it again, store nothing
        d.resent += 1;
        return ok(String(index));
    default:
        return refuse("chunk out of order, or for an old attempt");
    }
}

Reply on_finish(int node, Doll& d) {
    const long attempt = number("attempt"), chunks = number("chunks"), bytes = number("bytes");
    Session& s = d.session;
    if (attempt < 0 || !s.finish(node, attempt, chunks, bytes, millis())) {
        return refuse("finish refused: unknown attempt, not recording, or totals differ (the nest has " +
                      String(s.chunks) + " chunks, " + s.bytes + " bytes)");
    }
    Serial.printf("  doll %d  audio      %ld chunks, %ld bytes = %.1f s of sound, uploaded in %lu ms (%d sent twice)\n",
                  node, chunks, bytes, bytes / 32000.0f, millis() - d.started_ms, d.resent);
    doll_to_grade = node;  // loop() sends it right after this reply
    return ok("assessing");
}

Reply on_attempt(int node, Doll& d) {
    Session& s = d.session;
    if (!s.owned_by(node, number("attempt"))) {
        return refuse("unknown attempt (an old one, or never started)");
    }
    if (s.state != State::result) {
        return ok("pending");
    }
    return ok("result," + String(s.result_code) + "," + s.result_word);
}

Reply on_cancel(int node, Doll& d) {
    if (d.session.owned_by(node, number("attempt"))) {
        d.session.cancel();
    }
    return ok("cancelled");
}

Reply on_presence(int node, Doll& d) {
    const long present = number("present");
    if (present != 0 && present != 1) {
        return refuse("present must be 0 or 1");
    }
    if (bool(present) != d.book_on) {
        Serial.printf("  doll %d  book       %s the mat (mat reads %ld)\n", node, present ? "ON" : "OFF", number("fsr"));
    }
    d.book_on = present;
    if (number("fsr") >= 0) {
        d.fsr = number("fsr");
    }
    return ok("ok");
}

// The doll asks again every few seconds until the answer is "coming".
Reply on_help(int node, Doll& d) {
    if (!d.help_wanted) {
        d.help_wanted = true;
        Serial.printf("  doll %d  HELP       the child asks for help: press the ACK button on the nest\n", node);
        queue_event(dashboard_event::help(next_stamp(), node));
    }
    if (!d.help_coming) {
        return ok("waiting");
    }
    d.help_wanted = d.help_coming = false;  // told: the next /v1/help is a new request
    return ok("coming");
}

// ponytail (a shortcut on purpose): always "ok". The real nest may answer "command,5" (too noisy) here.
Reply on_noise(int node, Doll&) {
    const String db_text = text("db");
    const float db = db_text.isEmpty() ? NAN : db_text.toFloat();
    queue_event(dashboard_event::noise(next_stamp(), node, db));
    return ok("ok");
}

// For a person or tests/fake_doll.py: every doll's step as a number,
// 0 idle, 1 recording, 2 assessing, 3 result (the order of State in session.h).
Reply on_status() {
    String json = "{\"dolls\":[";
    for (int node = 1; node <= int(max_dolls); ++node) {
        const Doll& d = doll(node);
        json += String(node > 1 ? "," : "") + "{\"node\":" + node + ",\"state\":" + int(d.session.state) +
                ",\"attempt\":" + d.session.attempt + ",\"book_on\":" + (d.book_on ? "true" : "false") + "}";
    }
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
        return refuse("node must be 1, 2 or 3");
    }
    Doll& d = doll(node);
    d.last_heard_ms = millis();
    if (path == route::start)    return on_start(node, d);
    if (path == route::chunk)    return on_chunk(node, d);
    if (path == route::finish)   return on_finish(node, d);
    if (path == route::attempt)  return on_attempt(node, d);
    if (path == route::cancel)   return on_cancel(node, d);
    if (path == route::presence) return on_presence(node, d);
    if (path == route::help)     return on_help(node, d);
    if (path == route::noise)    return on_noise(node, d);
    return {404, "unknown route"};
}

// One line per message from a doll: who, which message, the raw ?query, and our answer.
// Quiet on purpose: chunks (summed up at finish), heartbeats, and "pending" answers.
void log_request(const Reply& reply) {
    const String& path = request.path;
    const bool routine = path == route::chunk || path == route::presence ||
                         (path == route::attempt && reply.text == "pending");
    if (reply.code == 200 && routine) {
        return;
    }
    const String who = text("node").isEmpty() ? String("?") : text("node");
    Serial.printf("  doll %s  %-9s  %-48s -> %d %s\n", who.c_str(), path.substring(path.lastIndexOf('/') + 1).c_str(),
                  request.query.c_str(), reply.code, reply.text.c_str());
    if (reply.code == 401) {
        Serial.println("           means: that board's LUMPI_LAN_KEY differs from the nest's");
    }
}

void answer_one_request() {
    WiFiClient client = server.available();
    if (!client) {
        return;
    }
    if (tiny_http::read_request(client, request)) {
        const Reply reply = answer();
        tiny_http::reply(client, reply.code, reply.text);
        log_request(reply);
    }
    client.stop();
}

// --- grading: send a finished recording to the cloud ---------------------------------------
const char* grade_meaning(uint32_t grade) {
    switch (grade) {
    case accepted:      return "accepted";
    case retry_word:    return "retry a word";
    case improve_audio: return "read louder or closer";
    case uncertain:     return "uncertain, try again";
    case too_dark:      return "too dark";
    case told_teacher:  return "teacher told";
    default:            return "unknown";
    }
}

void grade_waiting_recording() {
    if (doll_to_grade == 0) {
        return;
    }
    const int node = doll_to_grade;
    doll_to_grade = 0;
    Doll& d = doll(node);
    Session& s = d.session;
    const uint32_t started_ms = millis();
    uint32_t grade = uncertain, word = 0;  // if anything goes wrong: "uncertain", the doll asks to try again

    if (!has_cloud()) {
        grade = accepted;
        Serial.printf("  doll %d  grade      no LUMPI_ADAPTER_URL: answering 1 without asking anyone\n", node);
    } else {
        const String url = String(LUMPI_ADAPTER_URL) + laptop_route::assess + "?node=" + node +
                           "&attempt=" + s.attempt + "&passage=" + s.passage + "&uid=" + d.uid;
        String answer;  // "<grade>,<word>", e.g. "2,5" = retry word 5
        const int code = lumpi_net::request("POST", url, audio, s.bytes, "application/octet-stream", answer, cloud_wait_ms);
        unsigned g = 0, w = 0;
        if (code == 200 && sscanf(answer.c_str(), "%u,%u", &g, &w) == 2) {
            grade = g;
            word = w;
        } else {
            Serial.printf("  doll %d  grade      the cloud answered %d %s, so the doll gets 4 (uncertain)\n", node, code,
                          answer.c_str());
        }
    }
    if (s.got_result(node, s.attempt, grade, word)) {  // false if the doll cancelled meanwhile
        d.graded_ms = millis();
        Serial.printf("  doll %d  GRADE      %lu = %s, word %lu (%s %lu ms)\n", node, grade, grade_meaning(grade), word,
                      has_cloud() ? "the cloud took" : "made up by the nest in", millis() - started_ms);
    }
}

// --- the nest's own parts ---------------------------------------------------------------------
float read_lux() {
    float lux = NAN;
    return hardware::light_read(lux) ? lux : NAN;
}

void report_to_dashboard() {
    static uint32_t last_ms = 0;
    if (millis() - last_ms < report_every_ms) {
        return;
    }
    last_ms = millis();
    queue_event(dashboard_event::room(next_stamp(), read_lux(), hardware::read_distance_cm()));
    for (int node = 1; node <= int(max_dolls); ++node) {
        const Doll& d = doll(node);
        if (d.last_heard_ms == 0 || millis() - d.last_heard_ms > doll_quiet_after_ms) {
            continue;  // a quiet doll: the dashboard shows it as stale, then offline
        }
        const char* activity = dashboard_event::activity_for(d.session.state, d.book_on, millis() - d.graded_ms);
        queue_event(dashboard_event::status(next_stamp(), node, activity, d.uid.c_str(), d.session.passage,
                                            d.session.attempt, d.fsr));
    }
}

// The teacher's ACK button answers every doll waiting for help. Pressed = LOW.
// It counts as a press after 30 ms of LOW (a button's contacts bounce for a few ms).
void check_ack_button() {
    static uint32_t low_since_ms = 0;
    static bool done = false;
    if (digitalRead(gateway_pins::ack) == HIGH) {
        low_since_ms = 0;
        done = false;
        return;
    }
    if (low_since_ms == 0) {
        low_since_ms = millis();
    }
    if (done || millis() - low_since_ms < 30) {
        return;
    }
    done = true;
    for (int node = 1; node <= int(max_dolls); ++node) {
        if (doll(node).help_wanted) {
            doll(node).help_coming = true;
            Serial.printf("  doll %d  ACK        the teacher is coming (the doll hears it on its next /v1/help)\n", node);
        }
    }
}

// --- start and loop -----------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(500);
    monitor::title("STAGE 5: THE NEST (dolls talk to it)",
                   "dolls send readings here; the nest gets grades from the cloud and reports to the dashboard",
                   "a doll's reading ends with a GRADE line; every dashboard line says 200");
    monitor::section("SETTINGS (config/private.h)");
    monitor::item("Wi-Fi name", "%s", LUMPI_WIFI_SSID[0] ? LUMPI_WIFI_SSID : "(EMPTY: fill LUMPI_WIFI_SSID)");
    monitor::item("Grades + events", "%s", has_cloud() ? LUMPI_ADAPTER_URL : "none: every grade is 1, no dashboard events");
    monitor::item("LAN key", "%u characters (not shown)", unsigned(strlen(LUMPI_LAN_KEY)));

    monitor::section("PARTS (all optional)");
    Wire.begin(gateway_pins::sda, gateway_pins::scl);
    monitor::item("Light (BH1750)", "%s", hardware::light_begin() ? "answered at I2C 0x23" : "no answer: reports null");
    monitor::item("ACK button", "GPIO%d, pressed = LOW", gateway_pins::ack);
    pinMode(gateway_pins::trig, OUTPUT);
    pinMode(gateway_pins::echo, INPUT);
    pinMode(gateway_pins::ack, INPUT_PULLUP);

    audio = static_cast<uint8_t*>(malloc(audio_capacity));
    if (audio == nullptr) {
        Serial.println("[memory] no room for the 96 kB audio buffer: stopped");
        while (true) {
            delay(1000);
        }
    }
    // All 48 bits of the MAC address: the first 24 are the chip maker's, the same on every board.
    snprintf(gateway_id, sizeof(gateway_id), "lumpi_%012llx", ESP.getEfuseMac());
    monitor::item("Nest ID", "%s", gateway_id);

    monitor::section("CONNECTION");
    if (lumpi_net::begin()) {
        server.begin();
    }
}

void loop() {
    static bool said_hello = false;
    if (!said_hello && lumpi_net::connected()) {
        said_hello = true;
        monitor::item("Wi-Fi", "connected, signal %d dBm", WiFi.RSSI());
        monitor::item("Nest address", "http://%s  <- put this in LUMPI_GATEWAY_URL for the dolls",
                      WiFi.localIP().toString().c_str());
        monitor::section("MESSAGES (who, message, raw query -> answer)");
    }
    if (boot_time == 0) {
        boot_time = lumpi_net::unix_time();  // stays 0 until the internet clock answers
        if (boot_time != 0 && has_cloud()) {
            Serial.println("  nest    clock      ok: dashboard events start now");
        }
    }

    answer_one_request();        // a doll's message, if one is waiting
    grade_waiting_recording();   // after /v1/finish: ask the cloud (the dolls wait meanwhile)
    for (int node = 1; node <= int(max_dolls); ++node) {
        Session& s = doll(node).session;
        const State before = s.state;
        s.check_timeouts(millis());  // a reading stuck too long (doll switched off?) is dropped
        if (s.state != before) {
            Serial.printf("  doll %d  TIMEOUT    silent too long mid-reading: the reading is dropped\n", node);
        }
    }
    check_ack_button();
    report_to_dashboard();       // every 10 s
    send_one_event();
    delay(2);
}
