#pragma once
// THE NEST'S PLUMBING. nest.cpp (the story) calls these by plain names.
//
//   1. Flash storage   recordings /rec/<node>.pcm, word clips /clip/<node>-<slot>.wav, outbox /out/<n>.txt
//   2. Outbox          dashboard events wait here (they survive Wi-Fi, cloud and power loss)
//   3. Cloud calls     download a word clip into flash
//   4. Doll replies    send a file (a WAV clip) to a doll
//   5. Teacher screen  OLED (SSD1306, or SH1106 with -DUSE_SH1106)
//   6. ACK button      interrupt -> wakes the room task
//   7. Wi-Fi watchdog and CSV lines for the tester
#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_system.h>
#ifdef USE_SH1106
#include <Adafruit_SH110X.h>
#else
#include <Adafruit_SSD1306.h>
#endif

#include "lumpi_net.h"
#include "settings.h"
#include "tiny_http.h"

namespace nest_parts {

// ---- 1. Flash storage ----------------------------------------------------------------------

constexpr int clips_per_doll = 3;
constexpr size_t max_clip_bytes = 48000;  // 1.5 s of 16 kHz 16-bit audio: one word is shorter

// Mounts the flash file system. A brand-new board has none yet, so it is made once (formatted).
inline bool begin_storage() {
    const bool format_if_missing = true;
    if (!LittleFS.begin(format_if_missing)) {
        return false;
    }
    LittleFS.mkdir("/rec");
    LittleFS.mkdir("/clip");
    LittleFS.mkdir("/out");
    return true;
}

inline String recording_path(int node) {
    return "/rec/" + String(node) + ".pcm";
}

inline String clip_path(int node, int slot) {
    return "/clip/" + String(node) + "-" + String(slot) + ".wav";
}

inline uint32_t free_kb() {
    return (LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024;
}

// The proposal's risk: is flash fast enough for two dolls (2 x 32 KB/s)? Writes 64 KB and times it.
inline uint32_t flash_write_kb_per_s() {
    static uint8_t block[4096];
    File test = LittleFS.open("/speed.tmp", "w");
    const uint32_t started_ms = millis();
    for (int i = 0; i < 16; ++i) {
        test.write(block, sizeof(block));
    }
    test.close();
    const uint32_t took_ms = millis() - started_ms;
    LittleFS.remove("/speed.tmp");
    return took_ms == 0 ? 0 : 64 * 1000 / took_ms;
}

// ---- 2. Outbox: one file per batch of events, one JSON event per line --------------------

// Flash is 304 blocks of 4 KB; every file takes at least one. 2 recordings (158) + 6 clips (72)
// + folders (~8) + 40 batches = 278, leaving room for the file system's own copies.
// 40 batches = about 6 minutes of a Wi-Fi outage with both dolls on.
constexpr int max_outbox_files = 40;

struct Outbox {
    uint32_t oldest = 1;  // file number of the oldest waiting batch
    uint32_t next = 1;    // file number the next batch gets
    uint32_t count() const { return next - oldest; }
};

inline String outbox_path(uint32_t number) {
    return "/out/" + String(number) + ".txt";
}

// After a restart: find the waiting files again, so nothing is lost.
inline void outbox_find(Outbox& outbox) {
    File folder = LittleFS.open("/out");
    uint32_t lowest = UINT32_MAX, highest = 0;
    for (File file = folder.openNextFile(); file; file = folder.openNextFile()) {
        const uint32_t number = String(file.name()).toInt();
        lowest = min(lowest, number);
        highest = max(highest, number);
    }
    if (highest > 0) {
        outbox.oldest = lowest;
        outbox.next = highest + 1;
    }
}

// false = the flash is full or broken (the caller says so on the monitor).
inline bool outbox_write(Outbox& outbox, const String& lines) {
    if (outbox.count() >= max_outbox_files) {
        Serial.printf("  nest    outbox     full (%d batches): the oldest is dropped\n", max_outbox_files);
        LittleFS.remove(outbox_path(outbox.oldest));
        outbox.oldest += 1;
    }
    File file = LittleFS.open(outbox_path(outbox.next), "w");
    const bool written = file && file.print(lines) == lines.length();
    file.close();
    if (written) {
        outbox.next += 1;
    }
    return written;
}

inline String outbox_read_oldest(const Outbox& outbox) {
    File file = LittleFS.open(outbox_path(outbox.oldest), "r");
    const String lines = file ? file.readString() : String();
    file.close();
    return lines;
}

inline void outbox_remove_oldest(Outbox& outbox) {
    LittleFS.remove(outbox_path(outbox.oldest));
    outbox.oldest += 1;
}

// ---- 3. Cloud calls ------------------------------------------------------------------------

// GETs one word clip from the cloud into a flash file. false = not 200, too big, or cut short.
inline bool download_clip(const String& url, const String& path, uint32_t timeout_ms) {
    static WiFiClient plain;
    static WiFiClientSecure secure;
    HTTPClient http;
    if (!lumpi_net::open(http, plain, secure, url, nullptr, timeout_ms)) {
        return false;
    }
    const int code = http.GET();
    const int size = http.getSize();
    bool saved = false;
    if (code == 200 && size > 44 && size <= int(max_clip_bytes)) {
        LittleFS.remove(path);  // free the old clip's blocks first (flash is tight)
        File file = LittleFS.open(path, "w");
        saved = file && http.writeToStream(&file) == size;
        file.close();
    }
    http.end();
    if (!saved) {
        Serial.printf("  nest    clip       not saved: cloud answered %d, %d bytes (limit %u)\n", code, size,
                      unsigned(max_clip_bytes));
    }
    return saved;
}

// ---- 4. Doll replies -----------------------------------------------------------------------

// Sends a whole flash file with its Content-Length (the doll reads the 44-byte WAV header first).
inline bool reply_file(WiFiClient& client, const String& path, const char* type) {
    File file = LittleFS.open(path, "r");
    if (!file) {
        return false;
    }
    tiny_http::reply_head(client, 200, type, file.size());
    uint8_t piece[1024];
    while (file.available()) {
        const size_t got = file.read(piece, sizeof(piece));
        client.write(piece, got);
    }
    file.close();
    return true;
}

// ---- 5. Teacher screen (only the room task draws on it: one I2C bus) -----------------------

#ifdef USE_SH1106
inline Adafruit_SH1106G oled(128, 64, &Wire, -1);
constexpr uint16_t oled_white = SH110X_WHITE;
inline bool oled_start() { return oled.begin(0x3C, false); }
#else
inline Adafruit_SSD1306 oled(128, 64, &Wire, -1);
constexpr uint16_t oled_white = SSD1306_WHITE;
inline bool oled_start() { return oled.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false); }
#endif
inline bool oled_ok = false;

inline void oled_begin() {
    oled_ok = oled_start();
    if (oled_ok) {
        oled.clearDisplay();
        oled.setTextSize(1);
        oled.setTextColor(oled_white);
        oled.display();
    }
}

// Up to 8 lines of 21 characters. No lines = a blank screen (the teacher is not near).
inline void oled_show(const String lines[], int count) {
    if (!oled_ok) {
        return;
    }
    oled.clearDisplay();
    for (int i = 0; i < count; ++i) {
        oled.setCursor(0, i * 8);
        oled.print(lines[i]);
    }
    oled.display();
}

// ---- 6. ACK button: the interrupt only wakes the room task; the task does the work --------

inline TaskHandle_t ack_task = nullptr;

// static (not inline): an IRAM function must live in this one file, or the linker refuses it.
static void IRAM_ATTR on_ack_press() {
    BaseType_t woke = pdFALSE;
    vTaskNotifyGiveFromISR(ack_task, &woke);
    portYIELD_FROM_ISR(woke);
}

inline void attach_ack_button(TaskHandle_t room_task) {
    ack_task = room_task;
    pinMode(gateway_pins::ack, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(gateway_pins::ack), on_ack_press, FALLING);
}

// ---- 7. Wi-Fi watchdog and CSV lines -------------------------------------------------------

constexpr uint32_t wifi_restart_after_ms = 5 * 60 * 1000;

inline uint32_t wifi_drops = 0;
inline uint32_t wifi_down_since_ms = 0;  // 0 = Wi-Fi is up

// Called every second. Wi-Fi reconnects by itself; down for 5 minutes with no reading
// in flight = restart the board (the outbox is kept in flash).
inline void wifi_watch(bool reading_in_flight) {
    if (lumpi_net::connected()) {
        wifi_down_since_ms = 0;
        return;
    }
    if (wifi_down_since_ms == 0) {
        wifi_down_since_ms = millis();
        wifi_drops += 1;
        Serial.println("  nest    wifi       lost: reconnecting");
        WiFi.reconnect();
    }
    if (millis() - wifi_down_since_ms > wifi_restart_after_ms && !reading_in_flight) {
        Serial.println("  nest    wifi       down for 5 minutes: restarting the nest");
        ESP.restart();
    }
}

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

inline void csv_boot(uint32_t outbox_pending) {
    Serial.printf("CSV,boot,0,%s,%u,%u\n", reset_reason(), unsigned(ESP.getFreeHeap()), unsigned(outbox_pending));
}

inline void csv_beat(uint32_t outbox_pending) {
    Serial.printf("CSV,beat,0,%lu,%d,%u,%u,%u,%u\n", millis() / 1000, lumpi_net::connected() ? WiFi.RSSI() : 0,
                  unsigned(wifi_drops), unsigned(ESP.getFreeHeap()), unsigned(ESP.getMinFreeHeap()),
                  unsigned(outbox_pending));
}

} // namespace nest_parts
