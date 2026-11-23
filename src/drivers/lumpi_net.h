#pragma once
// WI-FI AND ONE WEB REQUEST, for the boards that talk over the network (the s5 and s6 sketches).
//
//   lumpi_net::begin();                                   // join the Wi-Fi in config/private.h
//   lumpi_net::request("GET", url, answer);               // returns 200, 409 ... (negative = no connection)
//   lumpi_net::request("POST", url, data, size, "application/json", answer);
//
// Every request carries the LAN key twice, so every receiver finds it where it looks:
//   Authorization: Bearer <key>   the nest and the cloud
//   X-LUMPI-Token: <key>          the laptop dashboard
//
// https:// addresses are encrypted. With LUMPI_CLOUD_CA set in private.h the site's certificate is
// also checked. Without it a fake site could pose as the cloud and collect the key: fine on the
// bench, not in a real class.
#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <ctime>

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

inline bool connected() {
    return WiFi.status() == WL_CONNECTED;
}

// Unix seconds, or 0 until the internet clock has answered (the board starts at 1970).
inline uint64_t unix_time() {
    const uint64_t now = std::time(nullptr);
    return now > 1700000000 ? now : 0;
}

// Sends one request and waits for the answer. Returns the HTTP code (200, 409 ...) or a negative
// number when there was no connection; answer gets the reply text (or what went wrong).
inline int request(const char* method, const String& url, const uint8_t* body, size_t length,
                   const char* content_type, String& answer, uint32_t timeout_ms = 5000) {
    static WiFiClient plain;
    static WiFiClientSecure secure;

    HTTPClient http;
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
        answer = "bad url";
        return -1;
    }
    http.setReuse(false);  // a fresh connection every time: no half-dead connection left over
    http.setTimeout(timeout_ms);
    http.setConnectTimeout(timeout_ms);
    http.addHeader("Authorization", String("Bearer ") + LUMPI_LAN_KEY);
    http.addHeader("X-LUMPI-Token", LUMPI_LAN_KEY);
    if (content_type != nullptr) {
        http.addHeader("Content-Type", content_type);
    }
    const int code = http.sendRequest(method, const_cast<uint8_t*>(body), length);
    answer = code > 0 ? http.getString() : HTTPClient::errorToString(code);
    http.end();
    return code;
}

// The same, with no body.
inline int request(const char* method, const String& url, String& answer, uint32_t timeout_ms = 5000) {
    return request(method, url, nullptr, 0, nullptr, answer, timeout_ms);
}

} // namespace lumpi_net
