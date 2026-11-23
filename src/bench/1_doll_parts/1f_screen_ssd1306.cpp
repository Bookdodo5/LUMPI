// SSD1306 128x64 I2C OLED, on the doll.
// VCC -> 3.3V, GND -> GND, SDA -> GPIO32, SCL -> GPIO33.
// Upload environment: s1f_screen. Monitor: 115200.
// Expect "LUMPI" and an increasing seconds count on the screen.
// If blank, run s1e_i2c_scan. Expect address 0x3C.
// This sample is for SSD1306, not SH1106.
// Garbled or shifted picture = wrong chip type: try env s1f_screen_sh1106.
// Output: wiring, a startup check (does 0x3C answer?), then a row every 5 s
// saying what the screen should show right now.
// Guide: https://lumpi-build-guide.vercel.app/station/screen
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include "settings.h"  // all pins live in config/settings.h
#include "monitor.h"

Adafruit_SSD1306 screen(128, 64, &Wire, -1);  // No reset GPIO.
bool screenReady = false;
int rows = 0;
unsigned long lastRowAt = 0;

void setup() {
    Serial.begin(115200);
    delay(500);
    monitor::title("STAGE 1f: SCREEN (SSD1306)", "the SSD1306 OLED answers at 0x3C and draws text.",
                   "the screen shows LUMPI and a seconds count that matches the rows below.");
    monitor::section("WIRING (OLED -> ESP32)");
    monitor::item("VCC", "3.3V");
    monitor::item("GND", "GND");
    monitor::item("SDA", "GPIO%d", doll_pins::oled_sda);
    monitor::item("SCL", "GPIO%d", doll_pins::oled_scl);
    Wire.begin(doll_pins::oled_sda, doll_pins::oled_scl);

    Wire.beginTransmission(0x3C);
    bool screenFound = Wire.endTransmission() == 0;
    if (screenFound) {
        // false: keep the I2C pins we already selected above.
        screenReady = screen.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false);
    }
    monitor::section("STARTUP CHECK");
    monitor::item("Address 0x3C", "%s", screenFound ? "answered" : "no answer");
    monitor::item("Screen setup", "%s", screenReady ? "OK" : "failed");
    if (!screenFound) {
        monitor::fail("no screen answers at 0x3C",
                      "check VCC, GND, SDA, SCL; then run s1e_i2c_scan to see what answers");
    } else if (!screenReady) {
        monitor::fail("the screen answered but setup failed",
                      "maybe not enough memory, or the wrong chip type");
    }
    monitor::section("LIVE (a row every 5 s)");
}

void loop() {
    if (screenReady) {
        screen.clearDisplay();
        screen.setTextColor(SSD1306_WHITE);
        screen.setTextSize(2);
        screen.setCursor(0, 0);
        screen.println("LUMPI");
        screen.setTextSize(1);
        screen.print("Seconds: ");
        screen.println(millis() / 1000);
        screen.display();
    }
    if (millis() - lastRowAt >= 5000) {
        lastRowAt = millis();
        if (rows % 20 == 0) {
            Serial.println("\n  time s | screen should show    | verdict");
            Serial.println("  -------+-----------------------+---------------------------------------");
        }
        rows++;
        Serial.printf("  %6.1f | LUMPI  Seconds: %-5lu | %s\n", millis() / 1000.0, millis() / 1000,
                      screenReady ? "drawing (garbled? try env s1f_screen_sh1106)"
                                  : "NOT drawing: see STARTUP CHECK");
    }
    delay(200);
}
