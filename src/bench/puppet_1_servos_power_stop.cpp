// Doll servos. Take horns off for the first test.
// Arm signal -> 1k resistor -> GPIO21.
// Tongue signal -> 1k resistor -> GPIO22.
// D4184 PWM -> GPIO4, with 10k pull-down from GPIO4 to GND.
// STOP button -> GPIO14 and GND.
// Separate 5V 3A charger -> USB-C breakout -> toggle switch -> D4184 +.
// Servo red -> D4184 +. Servo brown -> switched LOAD, NOT ESP32 GND.
// Charger GND, D4184 supply -, control GND and ESP32 GND join together.
// 1000uF capacitor: + to servo red, striped - to servo brown/LOAD.
// Do NOT connect charger's +5V to ESP32 5V or power servos from the ESP32.
// Check the D4184 terminal labels and measure its operation at 5V first.
// Full wiring: https://lumpi-build-guide.vercel.app/station/servo
//
// Upload environment: servos. Monitor: 115200, send commands with Enter.
// p1 = power on, a1500 = arm centre, t1500 = tongue centre, x = off.
// Try a1400/a1600 for small moves. Start with no mechanical load.
// Power starts OFF. STOP or a 3-second timeout turns it off again.
// Use p1 again before another move. Repeating p1 while on does not extend time.
#include <Arduino.h>

const int armPin = 21;
const int tonguePin = 22;
const int powerPin = 4;
const int stopPin = 14;

bool powerOn = false;
unsigned long poweredAt = 0;
String command;

void turnOff() {
    digitalWrite(powerPin, LOW);
    ledcWrite(0, 0);  // Stop arm pulses.
    ledcWrite(1, 0);  // Stop tongue pulses.
    powerOn = false;
}

void handleCommand(String text) {
    text.trim();

    if (text == "x" || text == "p0") {
        turnOff();
        Serial.println("Power OFF");
        return;
    }

    if (text == "p1") {
        if (digitalRead(stopPin) == LOW) {
            Serial.println("Release STOP first");
            return;
        }
        if (!powerOn) {
            digitalWrite(powerPin, HIGH);
            poweredAt = millis();
            powerOn = true;
            Serial.println("Power ON for up to 3 seconds");
        }
        return;
    }

    if (text.startsWith("a") || text.startsWith("t")) {
        if (!powerOn) {
            Serial.println("Power is OFF. Send p1 first.");
            return;
        }

        int pulseUs = text.substring(1).toInt();
        if (pulseUs < 1000 || pulseUs > 2000) {
            Serial.println("Use 1000..2000 microseconds; start near 1500.");
            return;
        }

        int channel = 0;  // Arm.
        if (text.startsWith("t")) {
            channel = 1;  // Tongue.
        }

        // 50 Hz = 20000 us per cycle. 16-bit PWM ranges from 0 to 65535.
        int duty = pulseUs * 65535L / 20000;
        ledcWrite(channel, duty);
        Serial.printf("Channel %d: %d us\n", channel, pulseUs);
        return;
    }

    Serial.println("Use p1, a1500, t1500, x");
}

void setup() {
    pinMode(powerPin, OUTPUT);
    digitalWrite(powerPin, LOW);
    pinMode(stopPin, INPUT_PULLUP);

    ledcSetup(0, 50, 16);  // Arm: channel 0, 50 Hz, 16-bit resolution.
    ledcSetup(1, 50, 16);  // Tongue: channel 1.
    ledcAttachPin(armPin, 0);
    ledcAttachPin(tonguePin, 1);
    turnOff();

    Serial.begin(115200);
    Serial.println("Servos OFF. Commands: p1, a1500, t1500, x");
}

void loop() {
    if (powerOn) {
        bool stopPressed = digitalRead(stopPin) == LOW;
        bool timedOut = millis() - poweredAt >= 3000;
        if (stopPressed || timedOut) {
            turnOff();
            Serial.println("STOP or timeout: power OFF");
        }
    }

    // Read one character at a time so waiting for Enter cannot block STOP.
    if (Serial.available()) {
        char letter = Serial.read();
        if (letter == '\n' || letter == '\r') {
            if (command.length() > 0) {
                handleCommand(command);
            }
            command = "";
        } else if (command.length() < 24) {
            command += letter;
        }
    }
    delay(1);
}
