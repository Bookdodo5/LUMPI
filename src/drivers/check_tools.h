#pragma once
// Helpers shared by the two FULL WIRING CHECKS:
//   4_nest_check.cpp  (the nest)      3_doll_check.cpp  (one doll)
// Each check prints [PASS], [FAIL] or [SKIP] with a hint (what to check), and a
// summary with the totals at the end.
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_system.h>
#include "settings.h"  // pin maps, and LUMPI_WIFI_SSID if config/private.h exists

#ifdef USE_SH1106  // set by the *_check_sh1106 environments
#include <Adafruit_SH110X.h>
#define SCREEN_WHITE SH110X_WHITE
#else
#include <Adafruit_SSD1306.h>
#define SCREEN_WHITE SSD1306_WHITE
#endif

namespace check {

// ---------------------------------------------------------------------------
// Results: every check adds one line; the summary prints them all again.
// ---------------------------------------------------------------------------
struct Line {
    const char* part;
    const char* verdict;  // "PASS", "FAIL", "SKIP" or "NOTE"
    String hint;
};
inline Line lines[40];
inline int line_count = 0;

inline void report(const char* part, const char* verdict, const String& hint) {
    if (line_count < 40) {
        lines[line_count++] = {part, verdict, hint};
    }
    Serial.printf("  [%s] %s: %s\n", verdict, part, hint.c_str());
}
inline void pass(const char* part, const String& hint) { report(part, "PASS", hint); }
inline void fail(const char* part, const String& hint) { report(part, "FAIL", hint); }
inline void skip(const char* part, const String& hint) { report(part, "SKIP", hint); }
inline void note(const char* part, const String& hint) { report(part, "NOTE", hint); }

// A 10-character level bar for the live data: value 0 -> "..........", full -> "##########".
inline String bar(int value, int full) {
    const int filled = constrain(value * 10 / full, 0, 10);
    String text;
    for (int i = 0; i < 10; ++i) text += i < filled ? '#' : '.';
    return text;
}

inline void print_summary(const char* board) {
    int failed = 0, passed = 0, skipped = 0;
    Serial.printf("\n================ %s SUMMARY ================\n", board);
    Serial.println("PASS = works, FAIL = fix it (the text says what to check), SKIP = not tested, NOTE = for info.\n");
    for (int i = 0; i < line_count; ++i) {
        Serial.printf("%-4s  %-20s %s\n", lines[i].verdict, lines[i].part, lines[i].hint.c_str());
        failed += strcmp(lines[i].verdict, "FAIL") == 0;
        passed += strcmp(lines[i].verdict, "PASS") == 0;
        skipped += strcmp(lines[i].verdict, "SKIP") == 0;
    }
    Serial.printf("\nTotal: %d PASS, %d FAIL, %d SKIP.\n", passed, failed, skipped);
    if (failed) {
        Serial.printf("\n%d FAIL line(s). Fix the FIRST one first: later ones often share its cause.\n", failed);
    } else {
        Serial.println("\nNo FAIL lines: every tested part works.");
    }
    Serial.println("Press the board's EN (reset) button to run the whole check again.");
}

// ---------------------------------------------------------------------------
// Talking to the person at the Serial Monitor.
// Start the monitor so typed text is sent when you press Enter:
//   pio device monitor -b 115200 --echo --eol LF --filter send_on_enter
// ---------------------------------------------------------------------------
inline void flush_input() {
    while (Serial.available()) {
        Serial.read();
    }
}

// Wait for one line of typed text. It ends when Enter arrives, OR 300 ms after the
// last character: some monitors (VS Code Serial Monitor with line ending "None")
// send the text without any Enter character at all.
inline String read_line() {
    String text;
    unsigned long last_char = 0;
    while (true) {
        if (Serial.available()) {
            const char c = Serial.read();
            if (c == '\n' || c == '\r') {
                text.trim();
                return text;
            }
            if (text.length() < 40) {
                text += c;
            }
            last_char = millis();
        } else if (last_char && millis() - last_char > 300) {
            text.trim();
            return text;
        }
        delay(5);
    }
}

// Show an instruction, then wait for Enter. Returns false if the person typed s (skip).
inline bool ready(const char* instruction) {
    Serial.printf("\n>> %s\n   Type g + Enter when ready (s + Enter = skip).\n", instruction);
    flush_input();
    return !read_line().equalsIgnoreCase("s");
}

// Ask a yes/no question about something only a person can see or hear.
inline bool ask_yes(const char* question) {
    Serial.printf("?? %s  (y/n + Enter)\n", question);
    while (true) {
        flush_input();
        const String answer = read_line();
        if (answer.equalsIgnoreCase("y")) return true;
        if (answer.equalsIgnoreCase("n")) return false;
        Serial.println("   Please type y or n, then Enter.");
    }
}

// Check condition() again and again until it is true, or the time is up.
// Prints a dot every second so you can see it is waiting.
template <typename Condition>
bool wait_until(Condition condition, int seconds) {
    const unsigned long start = millis();
    unsigned long last_dot = start;
    while (millis() - start < seconds * 1000UL) {
        if (condition()) {
            Serial.println();
            return true;
        }
        if (millis() - last_dot >= 1000) {
            Serial.print('.');
            last_dot = millis();
        }
        delay(5);
    }
    Serial.println(" time is up");
    return false;
}

// ---------------------------------------------------------------------------
// Checks used by both boards.
// ---------------------------------------------------------------------------

// Why did the board last restart? A BROWNOUT means the 3.3 V supply dipped:
// a short, a weak USB cable or port, or a motor pulling power from the board.
inline void check_boot_power() {
    switch (esp_reset_reason()) {
    case ESP_RST_BROWNOUT:
        fail("Power at boot", "last reset was a BROWNOUT (supply dipped): look for a short, try another USB cable/port, keep servos off the ESP32's power");
        break;
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
        fail("Power at boot", "last reset was a CRASH of the previous program (not a wiring fault by itself)");
        break;
    default:
        pass("Power at boot", "normal start, no brownout");
    }
}

// The Wi-Fi radio is part of the hardware too. No password needed: just listen.
inline void check_wifi_radio() {
    Serial.println("\n== Wi-Fi radio: listening for networks ==");
    WiFi.mode(WIFI_STA);
    const int count = WiFi.scanNetworks();
    if (count <= 0) {
        fail("Wi-Fi radio", "no network seen: is there 2.4 GHz Wi-Fi nearby? Is the antenna end of the board covered by metal?");
    } else if (strlen(LUMPI_WIFI_SSID) == 0) {
        pass("Wi-Fi radio", String(count) + " networks seen (create config/private.h to also look for ours)");
    } else {
        int ours = -1;
        for (int i = 0; i < count; ++i) {
            if (WiFi.SSID(i) == LUMPI_WIFI_SSID) ours = i;
        }
        if (ours >= 0) {
            pass("Wi-Fi radio", String("\"") + LUMPI_WIFI_SSID + "\" seen at " + WiFi.RSSI(ours) + " dBm (better than -75 is good)");
        } else {
            fail("Wi-Fi radio", String(count) + " networks seen but NOT \"" + LUMPI_WIFI_SSID + "\": is it 2.4 GHz and switched on?");
        }
    }
    WiFi.scanDelete();
    WiFi.mode(WIFI_OFF);
}

// One I2C device we expect on a bus. required=false means "optional part".
struct Device {
    uint8_t address;
    const char* name;
    bool required;
};

inline int i2c_scan(uint8_t* found, int capacity) {
    // Valid 7-bit addresses are 0x08..0x77. Anything else in a scan list is noise.
    int count = 0;
    for (uint8_t address = 0x08; address <= 0x77; ++address) {
        Wire.beginTransmission(address);
        if (Wire.endTransmission() == 0 && count < capacity) {
            found[count++] = address;
        }
    }
    return count;
}

// Full I2C bus check: wires first, then 3 scans, then each expected device.
template <size_t N>
void check_i2c_bus(int sda, int scl, const Device (&devices)[N]) {
    Serial.printf("\n== I2C bus (SDA GPIO %d, SCL GPIO %d) ==\n", sda, scl);

    // 1. Before I2C starts. Each module has pull-up resistors that hold SDA and SCL
    //    HIGH. We switch on the ESP32's WEAK pull-down: a real pull-up still wins, so
    //    the pins read HIGH. LOW means no powered module is pulling up, or a short to GND.
    pinMode(sda, INPUT_PULLDOWN);
    pinMode(scl, INPUT_PULLDOWN);
    delay(5);
    const bool sda_high = digitalRead(sda) == HIGH, scl_high = digitalRead(scl) == HIGH;
    pinMode(sda, INPUT);
    pinMode(scl, INPUT);
    if (sda_high && scl_high) {
        pass("I2C wires", "SDA and SCL rest HIGH: pull-ups present");
    } else {
        fail("I2C wires", String(sda_high ? "" : "SDA ") + (scl_high ? "" : "SCL ") +
                              "rests LOW: modules not powered (measure 3.3 V at each module's VCC-GND), wire missing, or shorted to GND");
    }

    // 2. Scan 3 times. Real wiring gives the same list every time. A list that
    //    changes is a loose wire or a bad breadboard contact.
    Wire.begin(sda, scl);
    uint8_t first[16], again[16];
    const int count = i2c_scan(first, 16);
    bool steady = true;
    for (int round = 0; round < 2; ++round) {
        delay(100);
        const int count_again = i2c_scan(again, 16);
        steady = steady && count_again == count && memcmp(first, again, count) == 0;
    }
    Serial.print("   Answered:");
    for (int i = 0; i < count; ++i) Serial.printf(" 0x%02X", first[i]);
    Serial.println(count ? "" : " nothing");
    if (!steady) {
        fail("I2C stability", "the list changed between 3 scans: loose wire. Wiggle each I2C wire while re-running");
    }

    // 3. Is every expected device there?
    auto found = [&](uint8_t address) {
        for (int i = 0; i < count; ++i) {
            if (first[i] == address) return true;
        }
        return false;
    };
    for (const Device& device : devices) {
        char address[8];
        snprintf(address, sizeof(address), "0x%02X", device.address);
        if (found(device.address)) {
            pass(device.name, String("answers at ") + address);
        } else if (device.required) {
            fail(device.name, String("no answer at ") + address + ": check its VCC (3.3 V), GND, SDA and SCL wires");
        } else {
            skip(device.name, String("not fitted (optional, would be ") + address + ")");
        }
    }
    for (int i = 0; i < count; ++i) {
        bool known = false;
        for (const Device& device : devices) known = known || device.address == first[i];
        if (!known) {
            char hint[120];
            snprintf(hint, sizeof(hint), "unexpected 0x%02X (0x5C = BH1750 with ADDR not on GND, 0x3D = OLED address jumper moved)", first[i]);
            note("I2C extra device", hint);
        }
    }
}

// The OLED: the bus check proves it answers; only a person can say the picture is right.
#ifdef USE_SH1106
inline Adafruit_SH1106G screen(128, 64, &Wire, -1);
#else
inline Adafruit_SSD1306 screen(128, 64, &Wire, -1);
#endif

inline void check_screen(const char* title) {
    Serial.println("\n== OLED picture ==");
    Wire.beginTransmission(0x3C);
    if (Wire.endTransmission() != 0) {
        fail("OLED picture", "not tested: the OLED did not answer at 0x3C (see the I2C lines above)");
        return;
    }
#ifdef USE_SH1106
    const bool started = screen.begin(0x3C, false);
#else
    const bool started = screen.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false);  // false: keep our I2C pins
#endif
    if (!started) {
        fail("OLED picture", "driver did not start (not enough memory?)");
        return;
    }
    screen.clearDisplay();
    screen.drawRect(0, 0, 128, 64, SCREEN_WHITE);  // a frame on all 4 edges
    screen.setTextColor(SCREEN_WHITE);
    screen.setTextSize(2);
    screen.setCursor(6, 6);
    screen.println(title);
    screen.setTextSize(1);
    screen.setCursor(6, 32);
    screen.println("Wiring check");
    screen.setCursor(6, 44);
    screen.println("Can you read me?");
    screen.display();
    if (ask_yes("Is the OLED text sharp, with a frame touching all 4 edges?")) {
        pass("OLED picture", "text and frame shown correctly");
    } else {
        fail("OLED picture", "blank: re-seat VCC/GND. Noise or shifted 2 pixels: it is an SH1106, upload the *_check_sh1106 environment");
    }
}

}  // namespace check
