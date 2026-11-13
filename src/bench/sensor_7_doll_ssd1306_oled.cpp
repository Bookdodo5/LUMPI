// SSD1306 128x64 I2C OLED, on the doll.
// VCC -> 3.3V, GND -> GND, SDA -> GPIO32, SCL -> GPIO33.
// Upload environment: doll_oled. Monitor: 115200.
// Expect "LUMPI" and an increasing seconds count on the screen.
// If blank, run doll_i2c. Expect address 0x3C.
// This sample is for SSD1306, not SH1106.
// Guide: https://lumpi-build-guide.vercel.app/station/screen
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_SSD1306.h>

Adafruit_SSD1306 screen(128, 64, &Wire, -1);  // No reset GPIO.
bool screenReady = false;

void setup() {
    Serial.begin(115200);
    Wire.begin(32, 33);

    Wire.beginTransmission(0x3C);
    bool screenFound = Wire.endTransmission() == 0;
    if (screenFound) {
        // false: keep the I2C pins we already selected above.
        screenReady = screen.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false);
    }
    Serial.println(screenReady ? "OLED ready" : "OLED not found");
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
    delay(200);
}
