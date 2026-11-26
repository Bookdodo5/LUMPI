// THE DOLL, bench version. Stages 5, 7 and 8 of docs/testing-steps.md.
// Upload environment: s5_doll (doll 1) or s5_doll_2 (doll 2). Needs s5_nest running on another board.
//
// LUMPI in one breath: a child reads a book aloud to a doll on their desk. The doll records
// the reading and sends it over Wi-Fi to the nest (one per classroom). The nest gets it graded
// by the cloud AI and hands the grade back to the doll.
//
// WHAT THIS SKETCH DOES when RECORD is pressed (or the key r is typed):
//   1. record 3 s from the microphone (STOP ends it early)
//   2. tell the nest a reading starts       POST /v1/start
//   3. send the audio in 2048-byte chunks    PUT  /v1/chunk  (the nest answers each with its number)
//   4. say "that was all of it"              POST /v1/finish
//   5. ask for the grade every 300 ms        GET  /v1/attempt  until "result,<grade>,<word>"
//      (while the nest is busy asking the cloud, that question just waits for an answer)
//   6. print the grade and the time it took. Goal: under 4 s from the end of recording.
// Also: every 10 s it tells the nest if a book is on the pressure mat (POST /v1/presence).
// Words: node = this doll's number (1-3); attempt = its reading counter; passage = the page (1-3);
// fsr = the pressure mat's raw reading; grade = the AI's verdict (1 = accepted ... see grade_meaning).
//
// KEYS (type in the monitor, then Enter):
//   r reading    h ask for help    n noise report    b book on/off (no mat wired)
//   p next passage (page 1-3)      c cancel the reading    ? this list
//
// HOW TO RUN
//   config/private.h: LUMPI_WIFI_SSID, LUMPI_WIFI_PASSWORD, LUMPI_LAN_KEY (same as the nest),
//   LUMPI_GATEWAY_URL = the address the nest prints, like "http://192.168.1.50".
//   Monitor so keys can be typed:  pio device monitor -b 115200 --echo --eol LF --filter send_on_enter
//
// WIRING: config/settings.h, doll_pins. Every part is optional, so a bare board works too:
//   no microphone = 3 s of silence is sent; no RFID reader = the test book 04A1B2C3.
#include <Arduino.h>
#include <HTTPClient.h>        // listed here so PlatformIO finds the libraries lumpi_net.h uses
#include <WiFiClientSecure.h>
#include <MFRC522.h>           // RFID reader
#include <SPI.h>
#include <driver/i2s.h>        // microphone

#include "lumpi_net.h"         // Wi-Fi + one web request
#include "monitor.h"           // the monitor layout: title, sections, "Label ..... value"
#include "messages.h"          // route names (/v1/start ...) and grade numbers (accepted = 1 ...)
#include "settings.h"          // pins and private.h

using namespace lumpi;

constexpr int node = LUMPI_NODE_ID;          // which doll this is: 1, 2 or 3
constexpr size_t audio_max = 96000;          // 3 s at 16 000 samples/s, 2 bytes each
constexpr size_t chunk_size = 2048;          // bytes per /v1/chunk (the nest takes at most this)
constexpr int mic_shift = 14;                // mic sample (32-bit) -> 16-bit. Lower = louder. Tune by ear.
constexpr uint32_t heartbeat_ms = 10000;     // tell the nest about the book this often
constexpr uint32_t chunk_wait_ms = 1500;     // a chunk with no "got it" after this is sent again (Wi-Fi hiccups)
constexpr int chunk_tries = 5;
constexpr uint32_t grade_wait_ms = 40000;    // give up on a grade after this (the nest drops a reading after 30 s)
constexpr uint32_t one_ask_ms = 15000;       // one "how did it go?" may wait while the nest asks the cloud (12 s)

uint8_t* audio = nullptr;
MFRC522 rfid(doll_pins::rfid_cs, MFRC522::UNUSED_PIN);
bool rfid_ok = false;
bool mic_ok = false;
String book = "04A1B2C3";   // the book's RFID tag; this test tag until a real one is seen
int attempt = 0;            // counts this doll's readings
int passage = 1;            // the page being read: 1, 2 or 3
bool book_on = false;       // is a book on the pressure mat?
bool pretend_book = false;  // key b: act as if a book is on the mat
bool waiting_for_help = false;

// The nest's address for a message. extra = more query, like "&attempt=3".
String nest_url(const char* route, const String& extra) {
    return String(LUMPI_GATEWAY_URL) + route + "?node=" + node + extra;
}

// Sends one message to the nest. Returns the HTTP code (negative = no answer).
int tell_nest(const char* method, const char* route, const String& extra, String& answer,
              const uint8_t* body = nullptr, size_t length = 0, uint32_t wait_ms = 5000) {
    return lumpi_net::request(method, nest_url(route, extra), body, length,
                              body ? "application/octet-stream" : nullptr, answer, wait_ms);
}

// --- parts -------------------------------------------------------------------------------------
void start_mic() {  // INMP441 on I2S: 32-bit samples, 16 000 per second, left channel
    i2s_config_t config = {};
    config.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX);
    config.sample_rate = sample_rate_hz;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    config.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;  // L/R on GND. Yes, RIGHT: this ESP32 core swaps left and right (tested 2026-11-22)
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 8;
    config.dma_buf_len = 512;
    i2s_pin_config_t pins = {};
    pins.bck_io_num = doll_pins::mic_sck;
    pins.ws_io_num = doll_pins::mic_ws;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = doll_pins::mic_sd;
    mic_ok = i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) == ESP_OK && i2s_set_pin(I2S_NUM_0, &pins) == ESP_OK;
    if (mic_ok) {  // INMP441 timing fix, same as the microphone test (1d_microphone)
        REG_SET_BIT(I2S_TIMING_REG(I2S_NUM_0), BIT(9));
        REG_SET_BIT(I2S_CONF_REG(I2S_NUM_0), I2S_RX_MSB_SHIFT);
    }
}

// Records into audio[] for 3 s, or until STOP. Returns the number of bytes recorded.
size_t record() {
    if (!mic_ok) {
        memset(audio, 0, audio_max);
        Serial.println("           no microphone: 3 s of silence instead (still a valid link test)");
        return audio_max;
    }
    i2s_zero_dma_buffer(I2S_NUM_0);
    int16_t* samples = reinterpret_cast<int16_t*>(audio);
    const size_t samples_max = audio_max / 2;
    size_t count = 0;
    int32_t loudest = 0;
    static int32_t raw[256];
    while (count < samples_max) {
        if (digitalRead(doll_pins::stop) == LOW && count > sample_rate_hz / 2) {  // STOP, after at least 0.5 s
            Serial.println("           STOP pressed: recording ended early");
            break;
        }
        size_t got = 0;
        i2s_read(I2S_NUM_0, raw, sizeof(raw), &got, pdMS_TO_TICKS(100));
        for (size_t i = 0; i < got / 4 && count < samples_max; ++i) {
            const int32_t s = constrain(raw[i] >> mic_shift, -32768, 32767);
            loudest = max(loudest, abs(s));
            samples[count++] = s;
        }
    }
    Serial.printf("           %.1f s recorded (%u samples), loudest sample %ld of 32767%s\n", count / 16000.0f,
                  unsigned(count), long(loudest),
                  loudest == 0 ? "  (all zero: check the SD wire and L/R)" : loudest >= 32767 ? "  (too loud: raise mic_shift)" : "");
    return count * 2;
}

void read_rfid() {
    if (!rfid_ok || !rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
        return;
    }
    String seen;
    for (byte i = 0; i < rfid.uid.size; ++i) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", rfid.uid.uidByte[i]);  // uppercase: the cloud needs it
        seen += hex;
    }
    rfid.PICC_HaltA();
    if (seen != book) {
        book = seen;
        Serial.printf("  book     RFID tag %s seen: readings now use this book\n", book.c_str());
    }
}

int read_mat() {
    return analogRead(doll_pins::fsr);  // higher = heavier
}

// --- one reading, steps 1-6 from the top of this file ---------------------------------------------
const char* grade_meaning(unsigned grade) {
    switch (grade) {
    case accepted:      return "accepted: well read";
    case retry_word:    return "retry a word";
    case improve_audio: return "read louder or closer";
    case uncertain:     return "uncertain: try again (AI or cloud trouble?)";
    case too_dark:      return "too dark";
    case told_teacher:  return "the teacher was told";
    default:            return "unknown grade";
    }
}

// Steps 3 and 4. false = the nest stopped answering.
bool send_audio(size_t bytes) {
    String answer;
    const int chunks = (bytes + chunk_size - 1) / chunk_size;
    for (int index = 0; index < chunks; ++index) {
        const size_t offset = index * chunk_size;
        const size_t size = min(chunk_size, bytes - offset);
        bool got_it = false;
        for (int tries = 1; tries <= chunk_tries && !got_it; ++tries) {  // no "got it"? send the same chunk again
            const int code = tell_nest("PUT", route::chunk, "&attempt=" + String(attempt) + "&index=" + index, answer,
                                       audio + offset, size, chunk_wait_ms);
            got_it = code == 200 && answer == String(index);
            if (!got_it) {
                Serial.printf("           chunk %d, try %d: no \"got it\" (%d %s)\n", index, tries, code, answer.c_str());
            }
        }
        if (!got_it) {
            return false;
        }
    }
    const int code = tell_nest("POST", route::finish,
                               "&attempt=" + String(attempt) + "&chunks=" + chunks + "&bytes=" + bytes, answer);
    if (code != 200) {
        Serial.printf("           finish refused: %d %s\n", code, answer.c_str());
    }
    return code == 200;
}

void reading() {
    attempt += 1;
    Serial.printf("\nREADING %d (doll %d, book %s, passage %d)\n", attempt, node, book.c_str(), passage);
    Serial.println("  1/6 record   3 s... read now (STOP ends early)");
    const size_t bytes = record();                                  // step 1
    const uint32_t recorded_ms = millis();
    Serial.printf("  2/6 start    POST /v1/start attempt=%d passage=%d uid=%s fsr=%d\n", attempt, passage,
                  book.c_str(), read_mat());

    String answer;
    int code = tell_nest("POST", route::start, "&attempt=" + String(attempt) + "&passage=" + passage +
                                               "&uid=" + book + "&fsr=" + read_mat(), answer);  // step 2
    if (code != 200) {
        Serial.printf("           refused: %d %s\n", code, answer.c_str());
        if (code < 0) {
            Serial.println("           means: no answer from the nest: is s5_nest running? is LUMPI_GATEWAY_URL right?");
        }
        return;
    }
    Serial.printf("  3/6 send     %u bytes in %u chunks of up to %u (PUT /v1/chunk)\n", unsigned(bytes),
                  unsigned((bytes + chunk_size - 1) / chunk_size), unsigned(chunk_size));
    if (!send_audio(bytes)) {                                       // steps 3, 4
        Serial.println("           the nest stopped answering: reading cancelled");
        tell_nest("POST", route::cancel, "&attempt=" + String(attempt), answer);
        return;
    }
    const uint32_t sent_ms = millis();
    Serial.printf("  4/6 finish   all sent in %lu ms; the nest is asking the cloud\n", sent_ms - recorded_ms);
    Serial.println("  5/6 wait     GET /v1/attempt every 300 ms ...");

    while (millis() - sent_ms < grade_wait_ms) {                    // step 5
        code = lumpi_net::request("GET", nest_url(route::attempt, "&attempt=" + String(attempt)), answer, one_ask_ms);
        if (code < 0) {
            continue;  // no answer in time: the nest is still busy, ask again
        }
        if (code != 200) {
            Serial.printf("           refused: %d %s\n", code, answer.c_str());
            return;
        }
        unsigned grade = 0, word = 0;
        if (sscanf(answer.c_str(), "result,%u,%u", &grade, &word) == 2) {  // step 6
            const uint32_t now = millis();
            Serial.printf("  6/6 GRADE    %u = %s%s (raw answer \"%s\")\n", grade, grade_meaning(grade),
                          grade == retry_word ? (", word " + String(word)).c_str() : "", answer.c_str());
            const uint32_t total = now - recorded_ms;
            Serial.printf("      time     send %lu ms + grade %lu ms = %lu ms after recording: %s (goal under 4000)\n",
                          sent_ms - recorded_ms, now - sent_ms, total, total < 4000 ? "PASS" : "TOO SLOW");
            return;
        }
        delay(300);  // "pending": ask again
    }
    Serial.println("           no grade after 40 s: is the nest stuck? look at its monitor");
}

// --- the smaller messages ------------------------------------------------------------------------
void send_presence() {
    String answer;
    const int code = tell_nest("POST", route::presence, "&present=" + String(book_on ? 1 : 0) + "&fsr=" + read_mat(), answer);
    if (code != 200) {
        Serial.printf("  nest     no answer to the heartbeat: %d %s\n", code, answer.c_str());
        Serial.println("           means: is s5_nest running? is LUMPI_GATEWAY_URL the address the nest printed?");
    }
}

void ask_for_help() {
    String answer;
    tell_nest("GET", route::help, "", answer);
    Serial.printf("  help     %s\n", answer == "waiting" ? "waiting (press ACK on the nest)"
                                    : answer == "coming"  ? "COMING: the teacher pressed ACK" : answer.c_str());
    waiting_for_help = answer == "waiting";
}

// ponytail (a shortcut on purpose): the "dB" is 20 x log10(loudest sample over 1 s), not calibrated against a sound meter.
void report_noise() {
    int32_t loudest = 1;
    if (mic_ok) {
        static int32_t raw[256];
        const uint32_t from = millis();
        while (millis() - from < 1000) {
            size_t got = 0;
            i2s_read(I2S_NUM_0, raw, sizeof(raw), &got, pdMS_TO_TICKS(100));
            for (size_t i = 0; i < got / 4; ++i) {
                loudest = max(loudest, abs(raw[i] >> 16));
            }
        }
    }
    const float db = 20.0f * log10f(loudest);
    String answer;
    const int code = tell_nest("POST", route::noise, "&db=" + String(db, 1), answer);
    Serial.printf("  noise    loudest sample %ld -> %.1f dB (not calibrated) -> nest: %d %s\n", long(loudest), db, code,
                  answer.c_str());
}

void print_keys() {
    monitor::section("KEYS (type, then Enter)");
    Serial.println("  r reading   h ask for help   n noise report   b book on/off (no mat)");
    Serial.println("  p next passage   c cancel the reading   ? this list");
}

void on_key(char key) {
    String answer;
    switch (key) {
    case 'r': reading(); break;
    case 'h': ask_for_help(); break;
    case 'n': report_noise(); break;
    case 'b':
        pretend_book = !pretend_book;
        Serial.printf("  book     pretend book %s\n", pretend_book ? "ON" : "OFF");
        break;
    case 'p':
        passage = passage % 3 + 1;
        Serial.printf("  book     passage %d\n", passage);
        break;
    case 'c':
        tell_nest("POST", route::cancel, "&attempt=" + String(attempt), answer);
        Serial.printf("  cancel   nest: %s\n", answer.c_str());
        break;
    case '?': print_keys(); break;
    default: break;
    }
}

// True once per press. Pressed = LOW; it counts after 35 ms of LOW (button contacts bounce).
bool record_pressed() {
    static uint32_t low_since_ms = 0;
    static bool done = false;
    if (digitalRead(doll_pins::record) == HIGH) {
        low_since_ms = 0;
        done = false;
        return false;
    }
    if (low_since_ms == 0) {
        low_since_ms = millis();
    }
    if (done || millis() - low_since_ms < 35) {
        return false;
    }
    done = true;
    return true;
}

// --- start and loop -----------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(500);
    monitor::title(("STAGE 5: DOLL " + String(node) + " (talks to the nest)").c_str(),
                   "press RECORD (or type r): record, send to the nest, get the grade",
                   "a reading ends with 6/6 GRADE and PASS (under 4000 ms)");
    pinMode(doll_pins::record, INPUT_PULLUP);
    pinMode(doll_pins::stop, INPUT_PULLUP);

    audio = static_cast<uint8_t*>(malloc(audio_max));
    if (audio == nullptr) {
        Serial.println("[memory] no room for the 96 kB audio buffer: stopped");
        while (true) {
            delay(1000);
        }
    }
    SPI.begin(doll_pins::rfid_sck, doll_pins::rfid_miso, doll_pins::rfid_mosi, doll_pins::rfid_cs);
    rfid.PCD_Init();
    const byte version = rfid.PCD_ReadRegister(MFRC522::VersionReg);
    rfid_ok = version != 0x00 && version != 0xFF;  // 0x00 or 0xFF = nothing answered
    start_mic();
    monitor::section("PARTS (all optional)");
    monitor::item("RFID reader", "%s", rfid_ok ? "answered" : "no answer: uses test book 04A1B2C3");
    monitor::item("Microphone", "%s", mic_ok ? "started" : "not started: sends silence");
    monitor::item("Pressure mat", "reads %d (a book is ON above %d)", read_mat(), doll_tuning::book_on_fsr);
    monitor::item("RECORD / STOP", "GPIO%d / GPIO%d, pressed = LOW", doll_pins::record, doll_pins::stop);
    monitor::section("SETTINGS (config/private.h)");
    monitor::item("Wi-Fi name", "%s", LUMPI_WIFI_SSID);
    monitor::item("Nest", "%s", LUMPI_GATEWAY_URL[0] ? LUMPI_GATEWAY_URL : "(EMPTY: fill LUMPI_GATEWAY_URL)");
    monitor::item("LAN key", "%u characters (not shown)", unsigned(strlen(LUMPI_LAN_KEY)));
    monitor::section("CONNECTION");

    if (LUMPI_GATEWAY_URL[0] == '\0') {
        Serial.println("[config] set LUMPI_GATEWAY_URL in config/private.h (s5_nest prints it)");
    }
    lumpi_net::begin();
}

void loop() {
    if (!lumpi_net::connected()) {
        delay(200);
        return;
    }
    static bool said_hello = false;
    if (!said_hello) {
        said_hello = true;
        monitor::item("Wi-Fi", "connected as %s, signal %d dBm", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        print_keys();
        monitor::section("EVENTS");
    }

    read_rfid();

    // The book: tell the nest when it changes, and every 10 s anyway (so the nest knows the doll is on).
    const bool now_on = pretend_book || read_mat() >= doll_tuning::book_on_fsr;
    static uint32_t told_ms = 0;
    if (now_on != book_on || told_ms == 0 || millis() - told_ms >= heartbeat_ms) {
        if (now_on != book_on) {
            Serial.printf("  book     %s the mat (mat reads %d, threshold %d)\n", now_on ? "ON" : "OFF", read_mat(),
                          doll_tuning::book_on_fsr);
        }
        book_on = now_on;
        told_ms = millis();
        send_presence();
    }

    static uint32_t help_asked_ms = 0;
    if (waiting_for_help && millis() - help_asked_ms >= 3000) {  // keep asking until "coming"
        help_asked_ms = millis();
        ask_for_help();
    }

    if (record_pressed()) {
        reading();
    }
    while (Serial.available()) {
        on_key(Serial.read());
    }
    delay(10);
}
