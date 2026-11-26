// SH1106 128x64 I2C OLED, on the gateway.
// VCC -> 3.3V, GND -> GND, SDA -> GPIO21, SCL -> GPIO22.
// Upload environment: s2d_screen_sh1106. Monitor: 115200.
// Expect "LUMPI" and an increasing seconds count on the screen.
// If blank, run s2a_i2c_scan. Expect address 0x3C.
// For the SH1106 display. Use the original oled environment for SSD1306.
// Garbled or shifted picture = wrong chip type: try env s2d_screen.
// Output: wiring, a startup check (does 0x3C answer?), then a row every 5 s
// saying what the screen should show right now.
// Guide: https://lumpi-build-guide.vercel.app/gateway/screen
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SH110X.h>
#include "settings.h"  // all pins live in config/settings.h
#include "monitor.h"

Adafruit_SH1106G screen(128, 64, &Wire, -1);  // No reset GPIO.
bool screenReady = false;
int rows = 0;
unsigned long lastRowAt = 0;

void setup() {
    Serial.begin(115200);
    delay(500);
    monitor::title("STAGE 2d: SCREEN (SH1106)", "the SH1106 OLED answers at 0x3C and draws text.",
                   "the screen shows LUMPI and a seconds count that matches the rows below.");
    monitor::section("WIRING (OLED -> ESP32)");
    monitor::item("VCC", "3.3V");
    monitor::item("GND", "GND");
    monitor::item("SDA", "GPIO%d", gateway_pins::sda);
    monitor::item("SCL", "GPIO%d", gateway_pins::scl);
    Wire.begin(gateway_pins::sda, gateway_pins::scl);

    Wire.beginTransmission(0x3C);
    bool screenFound = Wire.endTransmission() == 0;
    if (screenFound) {
        // No reset GPIO; initialize the SH1106 at the scanned address.
        screenReady = screen.begin(0x3C, false);
    }
    monitor::section("STARTUP CHECK");
    monitor::item("Address 0x3C", "%s", screenFound ? "answered" : "no answer");
    monitor::item("Screen setup", "%s", screenReady ? "OK" : "failed");
    if (!screenFound) {
        monitor::fail("no screen answers at 0x3C",
                      "check VCC, GND, SDA, SCL; then run s2a_i2c_scan to see what answers");
    } else if (!screenReady) {
        monitor::fail("the screen answered but setup failed",
                      "maybe not enough memory, or the wrong chip type");
    }
    monitor::section("LIVE (a row every 5 s)");
}

void loop() {
    if (screenReady) {
        screen.clearDisplay();
        screen.setTextColor(SH110X_WHITE);
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
                      screenReady ? "drawing (garbled? try env s2d_screen)"
                                  : "NOT drawing: see STARTUP CHECK");
    }
    delay(200);
}
