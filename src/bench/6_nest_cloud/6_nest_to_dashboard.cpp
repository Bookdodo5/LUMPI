// NEST -> DASHBOARD. Stage 6 of docs/testing-steps.md. Upload environment: s6_nest_to_dashboard.
//
// The nest sends to the teacher's dashboard:
//   every 10 s:   a room event: light (lux) and distance (cm), null for a sensor that is not wired
//   on command:   MOCK DOLLS, typed in the monitor, to fill the dashboard without any doll board:
//     add 1          doll 1 appears, a book on its mat
//     remove 1       doll 1 goes silent (the dashboard shows it stale after 30 s, offline after 60 s)
//     book 1 off     takes the book off (book 1 on puts it back)
//     read 1 85      a reading graded 85/100: recording 3 s, waiting for the AI 2 s, score, feedback 10 s
//     help 1         the child asks for help
//     noise 1 65     a noise level of 65 dB
//     list           what each mock doll is doing          ?   this list
//   Each mock doll also sends its status every 10 s, and at once when it changes.
// Each event is JSON, built by src/common/dashboard_event.h, sent as POST <LUMPI_ADAPTER_URL>/api/telemetry.
//
// HOW TO RUN
//   config/private.h: LUMPI_WIFI_SSID, LUMPI_WIFI_PASSWORD, LUMPI_LAN_KEY, and LUMPI_ADAPTER_URL:
//     the laptop:  LUMPI_LAN_KEY=<same key> python laptop/dashboard_server.py  (it prints the URL)
//     the cloud:   "https://<site>.vercel.app"
//   Monitor so commands can be typed:
//     pio device monitor -b 115200 --echo --eol LF --filter send_on_enter
//   The monitor shows SETTINGS, PARTS, CONNECTION, then one line per event sent.
//   A failed send also prints the raw JSON, the server's answer, and what it means. Pass = every line OK.
//
// WIRING (config/settings.h, gateway_pins):
//   BH1750 light: VCC 3.3V, GND, SDA GPIO21, SCL GPIO22.
//   HC-SR04 distance: VCC 5V, GND, TRIG GPIO26, ECHO -> 1k -> GPIO27 -> 2k -> GND (never 5 V into the ESP32).
#include <Arduino.h>
#include <HTTPClient.h>        // listed here so PlatformIO finds the libraries lumpi_net.h uses
#include <WiFiClientSecure.h>
#include <Wire.h>

#include "dashboard_event.h"        // builds the JSON events
#include "gateway_distance_logic.h" // echo time -> centimetres
#include "lumpi_net.h"              // Wi-Fi + one web request
#include "monitor.h"                // the monitor layout: title, sections, "Label ..... value"
#include "real_io.h"                // light sensor
#include "settings.h"               // pins and private.h

constexpr uint32_t report_every_ms = 10000;
constexpr int rows_per_header = 20;  // repeat the column titles this often

char gateway_id[24];     // "lumpi_" + this chip's MAC address
uint64_t boot_time = 0;  // Unix time when the clock first worked
uint32_t event_number = 0;
uint32_t sent = 0, accepted = 0;
const String telemetry_url = String(LUMPI_ADAPTER_URL) + "/api/telemetry";

// --- sending, one line per event ----------------------------------------------------------------
dashboard_event::Stamp next_stamp() {
    return {gateway_id, boot_time, ++event_number, lumpi_net::unix_time()};
}

String clock_text(uint64_t unix_seconds) {  // "14:03:07"
    const time_t t = unix_seconds;
    tm parts;
    gmtime_r(&t, &parts);
    char text[12];
    strftime(text, sizeof(text), "%H:%M:%S", &parts);
    return text;
}

// What a failed answer means, and what to do about it.
const char* meaning(int code) {
    switch (code) {
    case 401: return "the key was refused: LUMPI_LAN_KEY in private.h differs from the one the server started with";
    case 400: return "the event was refused: the server's reason is in the answer above";
    case 404: return "wrong address: LUMPI_ADAPTER_URL should end with the port, like http://10.0.0.5:8080";
    case HTTPC_ERROR_CONNECTION_REFUSED:
        return "could not connect: is the server running? did the laptop's address change? (the server prints it)";
    case HTTPC_ERROR_READ_TIMEOUT: return "connected, but no answer in time: the laptop is busy or a firewall holds it";
    default: return code < 0 ? "no connection: check the address, and that laptop and nest share the Wi-Fi"
                             : "unexpected answer from the server";
    }
}

void print_header() {
    Serial.println();
    Serial.println("  #  time      event   from    what was sent                                  result");
    Serial.println("---  --------  ------  ------  ---------------------------------------------  ----------------");
}

// Sends one event and prints one line about it (more lines when it fails).
void send(const char* kind, int node, const String& what, const std::string& event) {
    if (event.empty()) {
        monitor::fail("event not made", "a value would break the JSON (the book ID must be uppercase hex)");
        return;
    }
    sent += 1;
    if ((sent - 1) % rows_per_header == 0) {
        print_header();
    }
    String answer;
    const uint32_t started_ms = millis();
    const int code = lumpi_net::request("POST", telemetry_url, reinterpret_cast<const uint8_t*>(event.c_str()),
                                        event.size(), "application/json", answer, 8000);
    const uint32_t took_ms = millis() - started_ms;
    const String from = node == 0 ? String("nest") : "doll " + String(node);
    String result = "FAIL";
    if (code == 200) {
        accepted += 1;
        result = answer.indexOf("duplicate") >= 0 ? "OK (had it already)" : "OK added";
    }
    Serial.printf("%3lu  %s  %-6s  %-6s  %-45s  %s, %lu ms  [%lu/%lu ok]\n", sent,
                  clock_text(lumpi_net::unix_time()).c_str(), kind, from.c_str(), what.c_str(), result.c_str(),
                  took_ms, accepted, sent);
    if (code != 200) {
        Serial.printf("     sent to:  %s\n", telemetry_url.c_str());
        Serial.printf("     raw JSON: %s\n", event.c_str());
        Serial.printf("     answer:   %d %s\n", code, answer.c_str());
        Serial.printf("     means:    %s\n", meaning(code));
    } else if (sent == 1) {  // show what the JSON looks like, once
        Serial.printf("     raw JSON: %s\n", event.c_str());
    }
}

// --- the nest's sensors -----------------------------------------------------------------------------
// HC-SR04: a 10 us pulse out, then time the echo. echo_us = 0 when nothing came back.
float read_distance_cm(unsigned long& echo_us) {
    digitalWrite(gateway_pins::trig, HIGH);
    delayMicroseconds(10);
    digitalWrite(gateway_pins::trig, LOW);
    echo_us = pulseIn(gateway_pins::echo, HIGH, gateway_distance_logic::echo_timeout_us);
    if (!gateway_distance_logic::has_echo(echo_us)) {
        return NAN;
    }
    return gateway_distance_logic::echo_us_to_cm(echo_us);
}

float read_lux() {
    float lux = NAN;
    return hardware::light_read(lux) ? lux : NAN;
}

void send_room() {
    unsigned long echo_us = 0;
    const float lux = read_lux();
    const float cm = read_distance_cm(echo_us);
    // "light 17.5 lux (raw 21), dist 16.0 cm (echo 933 us)"
    String what = isnan(lux) ? String("light -- (no answer)")
                             : "light " + String(lux, 1) + " lux (raw " + String(lroundf(lux * 1.2f)) + ")";
    what += isnan(cm) ? String(", dist -- (no echo)") : ", dist " + String(cm, 1) + " cm (echo " + String(echo_us) + " us)";
    send("room", 0, what, dashboard_event::room(next_stamp(), lux, cm));
}

// --- mock dolls ----------------------------------------------------------------------------------------
struct MockDoll {
    bool active = false;
    bool book_on = true;
    int passage = 1;
    int attempt = 0;
    int score = 0;               // the grade of the current reading
    uint32_t reading_ms = 0;     // when the current reading started (0 = none)
    String sent_activity = "";   // the activity the dashboard has now
};
MockDoll mocks[lumpi::max_dolls];  // doll n = mocks[n - 1]

String mock_book(int node) { return "04A1B2C" + String(node); }  // doll 1 reads book 04A1B2C1

// A reading goes: recording 3 s -> waiting_ai 2 s -> feedback 10 s -> back to book_on.
const char* activity_of(const MockDoll& d) {
    if (d.reading_ms != 0) {
        const uint32_t ms = millis() - d.reading_ms;
        if (ms < 3000) return "recording";
        if (ms < 5000) return "waiting_ai";
        if (ms < 15000) return "feedback";
    }
    return d.book_on ? "book_on" : "idle";
}

void send_status(int node) {
    MockDoll& d = mocks[node - 1];
    const char* activity = activity_of(d);
    const int fsr = d.book_on ? 2500 : 100;  // pretend mat readings: book on / off
    const String book = d.book_on ? mock_book(node) : "";
    String what = String(activity) + (d.book_on ? ", book " + book + " p" + d.passage : String(", no book")) +
                  ", mat " + fsr;
    send("status", node, what, dashboard_event::status(next_stamp(), node, activity, book.c_str(), d.passage,
                                                        d.attempt, fsr));
    d.sent_activity = activity;
}

// Called every loop: sends a status the moment a mock doll's activity changes, and the score
// when a reading moves from waiting_ai to feedback.
void update_mocks() {
    for (int node = 1; node <= int(lumpi::max_dolls); ++node) {
        MockDoll& d = mocks[node - 1];
        if (!d.active || d.sent_activity == activity_of(d)) {
            continue;
        }
        if (String(activity_of(d)) == "feedback") {
            send("score", node, String(d.score) + "/100 for attempt " + d.attempt,
                 dashboard_event::score(next_stamp(), node, mock_book(node).c_str(), d.passage, d.attempt, d.score));
        }
        if (d.reading_ms != 0 && String(activity_of(d)) == (d.book_on ? "book_on" : "idle")) {
            d.reading_ms = 0;  // the reading is over
        }
        send_status(node);
    }
}

void print_commands() {
    monitor::section("MOCK DOLL COMMANDS (type, then Enter). Dolls 1 and 2 show on the dashboard.");
    Serial.println("  add 1        doll 1 appears, a book on its mat");
    Serial.println("  remove 1     doll 1 goes silent (stale after 30 s, offline after 60 s)");
    Serial.println("  book 1 off   take the book off      book 1 on   put it back");
    Serial.println("  read 1 85    a reading graded 85/100 (recording, waiting for AI, score, feedback)");
    Serial.println("  help 1       the child asks for help");
    Serial.println("  noise 1 65   a noise level of 65 dB");
    Serial.println("  list         what each mock doll is doing      ?  this list");
}

void print_mocks() {
    monitor::section("MOCK DOLLS");
    for (int node = 1; node <= int(lumpi::max_dolls); ++node) {
        const MockDoll& d = mocks[node - 1];
        if (d.active) {
            monitor::item(("Doll " + String(node)).c_str(), "%s, book %s, passage %d, attempt %d", activity_of(d),
                          d.book_on ? mock_book(node).c_str() : "off", d.passage, d.attempt);
        } else {
            monitor::item(("Doll " + String(node)).c_str(), "not added");
        }
    }
}

// "read 1 85" -> command "read", node 1, value "85"
void run_command(const String& line) {
    char command[12] = "", value[12] = "";
    int node = 0;
    sscanf(line.c_str(), "%11s %d %11s", command, &node, value);
    const String cmd = command;
    if (cmd == "?") return print_commands();
    if (cmd == "list") return print_mocks();
    if (node < 1 || node > int(lumpi::max_dolls)) {
        Serial.printf("[command] \"%s\": give a doll number 1-%lu, like \"%s 1\". Type ? for the list.\n",
                      line.c_str(), lumpi::max_dolls, command);
        return;
    }
    if (boot_time == 0) {
        Serial.println("[command] wait: the nest is not connected yet (see CONNECTION)");
        return;
    }
    MockDoll& d = mocks[node - 1];
    if (cmd == "add") {
        d = MockDoll();
        d.active = true;
        d.attempt = 1;
        send("start", node, "book " + mock_book(node) + " on the mat, passage 1",
             dashboard_event::start(next_stamp(), node, mock_book(node).c_str(), 1, 1, 2500));
        send_status(node);
        return;
    }
    if (!d.active) {
        Serial.printf("[command] doll %d is not added yet: type \"add %d\" first\n", node, node);
        return;
    }
    if (cmd == "remove") {
        d.active = false;
        Serial.printf("[doll %d] silent now: the dashboard shows it stale after 30 s, offline after 60 s\n", node);
    } else if (cmd == "book") {
        d.book_on = String(value) != "off";
        d.reading_ms = 0;
        send_status(node);
    } else if (cmd == "read") {
        d.book_on = true;
        d.attempt += 1;
        d.score = value[0] ? constrain(atoi(value), 0, 100) : 80;
        d.reading_ms = millis();
        update_mocks();  // sends "recording" now
    } else if (cmd == "help") {
        send("help", node, "the child asks for help", dashboard_event::help(next_stamp(), node));
    } else if (cmd == "noise") {
        const float db = value[0] ? atof(value) : 60.0f;
        send("noise", node, String(db, 1) + " dB", dashboard_event::noise(next_stamp(), node, db));
    } else {
        Serial.printf("[command] unknown \"%s\". Type ? for the list.\n", command);
    }
}

void read_keyboard() {
    static String line;
    while (Serial.available()) {
        const char c = Serial.read();
        if (c == '\n' || c == '\r') {
            line.trim();
            if (line.length() > 0) {
                run_command(line);
            }
            line = "";
        } else if (line.length() < 40) {
            line += c;
        }
    }
}

// --- the connection report ------------------------------------------------------------------------------
// The host part of a URL: "http://10.0.0.5:8080/x" -> "10.0.0.5"
String host_of(const String& url) {
    String host = url.substring(url.indexOf("//") + 2);
    for (char end : {':', '/'}) {
        if (host.indexOf(end) >= 0) {
            host = host.substring(0, host.indexOf(end));
        }
    }
    return host;
}

const char* signal_word(int rssi) {  // Wi-Fi signal in dBm: closer to 0 = stronger
    return rssi > -60 ? "strong" : rssi > -70 ? "good" : rssi > -80 ? "weak" : "very weak: move closer";
}

void print_connection(uint32_t took_ms) {
    const IPAddress nest = WiFi.localIP();
    const IPAddress mask = WiFi.subnetMask();
    monitor::item("Wi-Fi", "connected in %.1f s", took_ms / 1000.0f);
    monitor::item("Nest address", "%s", nest.toString().c_str());
    monitor::item("Signal", "%d dBm (%s)", WiFi.RSSI(), signal_word(WiFi.RSSI()));
    IPAddress dashboard;
    if (!dashboard.fromString(host_of(LUMPI_ADAPTER_URL))) {
        monitor::item("Same network?", "the dashboard is a name, not a number: fine for the cloud");
    } else if ((uint32_t(dashboard) & uint32_t(mask)) == (uint32_t(nest) & uint32_t(mask))) {
        monitor::item("Same network?", "yes (nest %s, dashboard %s)", nest.toString().c_str(), dashboard.toString().c_str());
    } else {
        monitor::item("Same network?", "NO (nest %s, dashboard %s)", nest.toString().c_str(), dashboard.toString().c_str());
        monitor::fail("the nest probably cannot reach the dashboard",
                      "check the laptop's address (the server prints it) and that both use the same Wi-Fi");
    }
    monitor::item("Clock", "waiting for the internet time (the dashboard needs real times)");
}

// --- start and loop ------------------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(500);
    monitor::title("STAGE 6: NEST -> DASHBOARD",
                   "the nest sends room readings every 10 s; typed commands add mock dolls",
                   "every line under SENDING says OK, and the dashboard cards change");
    Wire.begin(gateway_pins::sda, gateway_pins::scl);
    pinMode(gateway_pins::trig, OUTPUT);
    pinMode(gateway_pins::echo, INPUT);
    snprintf(gateway_id, sizeof(gateway_id), "lumpi_%012llx", ESP.getEfuseMac());

    monitor::section("SETTINGS (config/private.h)");
    monitor::item("Wi-Fi name", "%s", LUMPI_WIFI_SSID[0] ? LUMPI_WIFI_SSID : "(EMPTY: fill LUMPI_WIFI_SSID)");
    monitor::item("Dashboard", "%s", LUMPI_ADAPTER_URL[0] ? telemetry_url.c_str() : "(EMPTY: fill LUMPI_ADAPTER_URL)");
    if (LUMPI_LAN_KEY[0]) {
        monitor::item("LAN key", "%u characters (not shown)", unsigned(strlen(LUMPI_LAN_KEY)));
    } else {
        monitor::item("LAN key", "(EMPTY: fill LUMPI_LAN_KEY)");
    }
    monitor::item("Nest ID", "%s", gateway_id);

    monitor::section("PARTS");
    if (hardware::light_begin()) {
        monitor::item("Light (BH1750)", "answered at I2C 0x23, SDA GPIO%d, SCL GPIO%d", gateway_pins::sda, gateway_pins::scl);
    } else {
        monitor::item("Light (BH1750)", "NO ANSWER (its value is sent as null)");
        monitor::fail("no light sensor", "check SDA 21, SCL 22, 3.3V and GND; the I2C scan s2a_i2c_scan should show 0x23");
    }
    unsigned long echo_us = 0;
    const float cm = read_distance_cm(echo_us);
    if (isnan(cm)) {
        monitor::item("Distance (HC-SR04)", "NO ECHO (its value is sent as null)");
        monitor::fail("no echo", "check TRIG 26, ECHO 27 through the 1k/2k divider, 5V, and that nothing is closer than 2 cm");
    } else {
        monitor::item("Distance (HC-SR04)", "%.1f cm (echo %lu us)", cm, echo_us);
    }

    monitor::section("CONNECTION");
    if (LUMPI_ADAPTER_URL[0] == '\0' || !lumpi_net::begin()) {
        Serial.println("  Stopped: fill config/private.h and upload again.");
        while (true) {
            delay(1000);
        }
    }
    monitor::item("Wi-Fi", "joining %s ...", LUMPI_WIFI_SSID);
}

void loop() {
    static bool was_connected = false;
    static uint32_t joining_since_ms = millis();
    read_keyboard();
    if (!lumpi_net::connected()) {
        if (was_connected) {
            Serial.println("\n[wifi] LOST the Wi-Fi: reconnecting ...");
            was_connected = false;
            joining_since_ms = millis();
        }
        delay(50);
        return;
    }
    if (!was_connected) {
        was_connected = true;
        print_connection(millis() - joining_since_ms);
    }
    if (lumpi_net::unix_time() == 0) {
        delay(50);
        return;
    }
    if (boot_time == 0) {
        boot_time = lumpi_net::unix_time();
        monitor::item("Clock", "ok: %s UTC", clock_text(boot_time).c_str());
        print_commands();
        monitor::section("SENDING to the dashboard (type a command any time)");
    }

    update_mocks();
    static uint32_t last_report_ms = 0;
    if (last_report_ms == 0 || millis() - last_report_ms >= report_every_ms) {
        last_report_ms = millis();
        send_room();
        for (int node = 1; node <= int(lumpi::max_dolls); ++node) {
            if (mocks[node - 1].active) {
                send_status(node);  // the heartbeat that keeps a doll "online"
            }
        }
    }
    delay(20);
}
