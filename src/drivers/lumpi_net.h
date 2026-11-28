#pragma once
// WI-FI AND ONE WEB REQUEST, for the boards that talk over the network (the s5 and s6 sketches).
//
//   lumpi_net::begin();                                   // join the Wi-Fi in config/private.h
//   lumpi_net::request("GET", url, answer);               // returns 200, 409 ... (negative = no connection)
//   lumpi_net::request("POST", url, data, size, "application/json", answer);
//
// Every request carries the LAN key as "Authorization: Bearer <key>" (the nest and the cloud check it).
//
// https:// addresses are encrypted. With LUMPI_CLOUD_CA set in private.h the site's certificate is
// also checked. Without it a fake site could pose as the cloud and collect the key: fine on the
// bench, not in a real class.
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ctime>
#include <esp_system.h>

#include "settings.h"

namespace lumpi_net {

// Starts joining the Wi-Fi (it carries on in the background) and asks the internet for the time.
// false = config/private.h is not filled in.
inline bool begin() {
    if (LUMPI_WIFI_SSID[0] == '\0' || LUMPI_LAN_KEY[0] == '\0') {
        Serial.println("[config] fill LUMPI_WIFI_SSID, LUMPI_WIFI_PASSWORD and LUMPI_LAN_KEY in config/private.h");
        return false;
    }
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);  // no Wi-Fi power saving: answers come back faster
    WiFi.begin(LUMPI_WIFI_SSID, LUMPI_WIFI_PASSWORD);
    configTime(0, 0, "pool.ntp.org", "time.google.com");
    Serial.printf("[wifi] joining %s\n", LUMPI_WIFI_SSID);
    return true;
}

// Why the board last started, for the CSV boot line.
inline const char* reset_reason() {
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "poweron";
    case ESP_RST_SW:       return "restart";
    case ESP_RST_PANIC:    return "panic";
    case ESP_RST_TASK_WDT: return "task_wdt";
    case ESP_RST_BROWNOUT: return "brownout";
    default:               return "other";
    }
}

inline bool connected() {
    return WiFi.status() == WL_CONNECTED;
}

// Unix seconds, or 0 until the internet clock has answered (the board starts at 1970).
inline uint64_t unix_time() {
    const uint64_t now = std::time(nullptr);
    return now > 1700000000 ? now : 0;
}

// Opens one request: picks plain or encrypted, adds the LAN key. false = the address is broken.
// Call it from ONE task only: the connections passed in are reused from request to request.
inline bool open(HTTPClient& http, WiFiClient& plain, WiFiClientSecure& secure, const String& url,
                 const char* content_type, uint32_t timeout_ms) {
    bool ok;
    if (url.startsWith("https://")) {
        if (LUMPI_CLOUD_CA[0] != '\0') {
            secure.setCACert(LUMPI_CLOUD_CA);
        } else {
            secure.setInsecure();  // encrypted, but the site is not checked (see the top of this file)
            static bool warned = false;
            if (!warned) {
                Serial.println("[net] WARNING: no LUMPI_CLOUD_CA, so the cloud's certificate is not checked");
                warned = true;
            }
        }
        secure.setHandshakeTimeout((timeout_ms + 999) / 1000);  // seconds; the default 120 s would freeze the board
        ok = http.begin(secure, url);
    } else {
        ok = http.begin(plain, url);
    }
    if (!ok) {
        return false;
    }
    http.setReuse(false);  // a fresh connection every time: no half-dead connection left over
    http.setTimeout(timeout_ms);
    http.setConnectTimeout(timeout_ms);
    http.addHeader("Authorization", String("Bearer ") + LUMPI_LAN_KEY);
    if (content_type != nullptr) {
        http.addHeader("Content-Type", content_type);
    }
    return true;
}

// Sends one request and waits for the answer. Returns the HTTP code (200, 409 ...) or a negative
// number when there was no connection; answer gets the reply text (or what went wrong).
// The body is either bytes in RAM or, when stream is given, read from it as it is sent.
inline int request(const char* method, const String& url, const uint8_t* body, size_t length,
                   const char* content_type, String& answer, uint32_t timeout_ms = 5000, Stream* stream = nullptr) {
    static WiFiClient plain;
    static WiFiClientSecure secure;
    HTTPClient http;
    if (!open(http, plain, secure, url, content_type, timeout_ms)) {
        answer = "bad url";
        return -1;
    }
    const int code = stream != nullptr ? http.sendRequest(method, stream, length)
                                       : http.sendRequest(method, const_cast<uint8_t*>(body), length);
    answer = code > 0 ? http.getString() : HTTPClient::errorToString(code);
    http.end();
    return code;
}

// The same, but the body is read from a Stream (a recording file in flash), so it never sits in RAM.
inline int request(const char* method, const String& url, Stream& body, size_t length,
                   const char* content_type, String& answer, uint32_t timeout_ms) {
    return request(method, url, nullptr, length, content_type, answer, timeout_ms, &body);
}

// The same, with no body.
inline int request(const char* method, const String& url, String& answer, uint32_t timeout_ms = 5000) {
    return request(method, url, nullptr, 0, nullptr, answer, timeout_ms);
}

} // namespace lumpi_net
