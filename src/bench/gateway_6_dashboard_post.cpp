// Nest sends live data to the laptop dashboard (T07 path).
// BH1750 wiring: VCC -> 3.3V, GND -> GND, SDA -> GPIO21, SCL -> GPIO22.
// HC-SR04 wiring: VCC -> USB 5V, GND -> GND, TRIG -> GPIO26,
//   ECHO -> 1k resistor -> GPIO27 -> 2k resistor -> GND (never 5V straight into the ESP32).
// Copy config/private.example.h to config/private.h and fill in
//   LUMPI_WIFI_SSID, LUMPI_WIFI_PASSWORD, LUMPI_ADAPTER_URL, LUMPI_LAN_KEY.
// On the laptop, with the same key:
//   LUMPI_LAN_KEY=... python3 laptop/dashboard_server.py
//   (the server prints the address to put in LUMPI_ADAPTER_URL)
// Upload environment: dashboard_post. Monitor: 115200.
// Expected: one sample doll start event, then a room event every 10 s.
//   Serial shows "200 {"result": "added"}" and the dashboard nest card updates.
//   On a 400 the reason is printed here and shown in the dashboard's developer panel.
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <ctime>

#include "dashboard_event.h"
#include "gateway_cloud_logic.h"
#include "gateway_distance_logic.h"
#include "gateway_start_packet.h"
#include "real_io.h"
#include "settings.h"

char gateway_id[24];
uint64_t boot_time = 0;
uint32_t event_number = 0;
uint32_t last_room_event_ms = 0;
bool sample_start_sent = false;

float read_distance_cm() {
    digitalWrite(gateway_pins::trig, LOW);
    delayMicroseconds(2);
    digitalWrite(gateway_pins::trig, HIGH);
    delayMicroseconds(10);
    digitalWrite(gateway_pins::trig, LOW);

    unsigned long echo_us = pulseIn(
        gateway_pins::echo,
        HIGH,
        gateway_distance_logic::echo_timeout_us
    );

    if (!gateway_distance_logic::has_echo(echo_us)) {
        return NAN;
    }
    return static_cast<float>(gateway_distance_logic::echo_us_to_cm(echo_us));
}

void post_event(const char* event) {
    HTTPClient http;
    String url = String(LUMPI_ADAPTER_URL) + "/api/telemetry";

    http.begin(url);
    http.setTimeout(3000);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-LUMPI-Token", LUMPI_LAN_KEY);

    int code = http.POST(event);
    if (code > 0) {
        Serial.printf("[dashboard] %d %s\n", code, http.getString().c_str());
    } else {
        // Negative codes are connection errors, e.g. wrong address or laptop firewall.
        Serial.printf("[dashboard] cannot reach %s: %s\n", url.c_str(), http.errorToString(code).c_str());
    }
    http.end();
}

void setup() {
    Serial.begin(115200);
    delay(500);

    Wire.begin(gateway_pins::sda, gateway_pins::scl);
    hardware::light_begin();

    pinMode(gateway_pins::trig, OUTPUT);
    digitalWrite(gateway_pins::trig, LOW);
    pinMode(gateway_pins::echo, INPUT);

    uint32_t chip_id = static_cast<uint32_t>(ESP.getEfuseMac());
    std::snprintf(gateway_id, sizeof(gateway_id), "lumpi_%08lx", static_cast<unsigned long>(chip_id));

    Serial.println("=== Gateway 6: dashboard post ===");

    if (LUMPI_WIFI_SSID[0] == '\0' || LUMPI_ADAPTER_URL[0] == '\0' || LUMPI_LAN_KEY[0] == '\0') {
        Serial.println("[config] fill LUMPI_WIFI_*, LUMPI_ADAPTER_URL and LUMPI_LAN_KEY in config/private.h");
        return;
    }

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.begin(LUMPI_WIFI_SSID, LUMPI_WIFI_PASSWORD);
    configTime(0, 0, "pool.ntp.org");
    Serial.println("[wifi] connecting");
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) {
        delay(500);
        return;
    }

    // The dashboard rejects events without a real clock, so wait for NTP.
    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    if (now <= gateway_cloud_logic::valid_clock_after) {
        delay(500);
        return;
    }

    if (boot_time == 0) {
        boot_time = now;
        Serial.printf("[wifi] connected, IP %s, clock ok\n", WiFi.localIP().toString().c_str());
    }

    char event[256];

    // Once: a sample doll start packet, sent through the real parser.
    if (!sample_start_sent) {
        gateway_start_packet::StartPacket packet;
        gateway_start_packet::ParseResult result = gateway_start_packet::parse(
            "node=1&attempt=1&passage=2&uid=04A1B2C3&fsr=3100",
            packet
        );

        event_number += 1;
        if (result == gateway_start_packet::ParseResult::ok
            && dashboard_event::format_start_event(event, sizeof(event), gateway_id, boot_time, event_number, now, packet)) {
            Serial.printf("[dashboard] send %s\n", event);
            post_event(event);
        }
        sample_start_sent = true;
    }

    uint32_t now_ms = millis();
    if (now_ms - last_room_event_ms >= dashboard_event::room_event_interval_ms) {
        last_room_event_ms = now_ms;

        float lux = NAN;
        if (!hardware::light_read(lux)) {
            lux = NAN;
        }
        float distance_cm = read_distance_cm();

        event_number += 1;
        bool ready = dashboard_event::format_room_event(
            event,
            sizeof(event),
            gateway_id,
            boot_time,
            event_number,
            now,
            lux,
            distance_cm
        );

        if (ready) {
            Serial.printf("[dashboard] send %s\n", event);
            post_event(event);
        } else {
            Serial.println("[dashboard] room event was too large");
        }
    }

    delay(100);
}
