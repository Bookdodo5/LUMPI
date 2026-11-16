// SH1106 128x64 I2C OLED, on the doll.
// VCC -> 3.3V, GND -> GND, SDA -> GPIO32, SCL -> GPIO33.
// Upload environment: doll_oled_sh1106. Monitor: 115200.
// Expect "LUMPI" and an increasing seconds count on the screen.
// If blank, run doll_i2c. Expect address 0x3C.
// For the SH1106 display. Use the original oled environment for SSD1306.
// Guide: https://lumpi-build-guide.vercel.app/station/screen
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SH110X.h>

Adafruit_SH1106G screen(128, 64, &Wire, -1);  // No reset GPIO.
bool screenReady = false;

void setup() {
    Serial.begin(115200);
    Wire.begin(32, 33);

    Wire.beginTransmission(0x3C);
    bool screenFound = Wire.endTransmission() == 0;
    if (screenFound) {
        // No reset GPIO; initialize the SH1106 at the scanned address.
        screenReady = screen.begin(0x3C, false);
    }
    Serial.println(screenReady ? "OLED ready" : "OLED not found");
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
    delay(200);
}
