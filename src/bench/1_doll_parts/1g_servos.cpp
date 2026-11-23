// Doll servos + STOP button. Take horns off for the first test.
// Arm signal (orange) -> GPIO21.
// Tongue signal (orange) -> GPIO22.
// STOP button -> GPIO14 and GND.
// Separate 5V 3A charger -> USB-C breakout -> toggle switch -> servo red wires.
// Servo brown wires -> charger GND. Charger GND and ESP32 GND join together.
// 1000uF capacitor: + to servo red, striped - to servo brown.
// Do NOT connect charger's +5V to ESP32 5V or power servos from the ESP32.
// The toggle switch is the manual power cut: flip it OFF to stop the servos for sure.
// Full wiring: https://lumpi-build-guide.vercel.app/station/servo
//
// Upload environment: s1g_servos. Monitor: 115200, send commands with Enter.
// a1500 = arm centre, t1500 = tongue centre, x = stop the pulses.
// Try a1400/a1600 for small moves. Start with no mechanical load.
// Pulses start OFF. STOP or a 3-second timeout ends them again (duty 0:
// the servos stop, go limp and stay quiet). Each new move starts a new 3 s.
// Output: a table row per event: time | servo | pulse us | PWM duty | what happens.
#include <Arduino.h>
#include "settings.h"  // all pins live in config/settings.h
#include "monitor.h"

const int armPin = doll_pins::arm;
const int tonguePin = doll_pins::tongue;
const int stopPin = doll_pins::stop;

bool pulsesOn = false;
unsigned long startedAt = 0;
String command;
int rows = 0;  // table rows printed; the header repeats every 20

// One table row. pulse and duty are "-" when no pulse is involved.
void printRow(const char* servo, const char* pulse, const char* duty, const char* meaning) {
    if (rows % 20 == 0) {
        Serial.println("\n  time s | servo  | pulse us | PWM duty     | what happens");
        Serial.println("  -------+--------+----------+--------------+------------------------------");
    }
    rows++;
    Serial.printf("  %6.1f | %-6s | %8s | %-12s | %s\n", millis() / 1000.0, servo, pulse, duty, meaning);
}

void turnOff() {
    ledcWrite(0, 0);  // Stop arm pulses.
    ledcWrite(1, 0);  // Stop tongue pulses.
    pulsesOn = false;
}

void handleCommand(String text) {
    text.trim();

    if (text == "x") {
        turnOff();
        printRow("both", "-", "0", "x typed: pulses OFF, servos limp");
        return;
    }

    if (text.startsWith("a") || text.startsWith("t")) {
        if (digitalRead(stopPin) == LOW) {
            printRow("-", "-", "-", "REFUSED: STOP is held, release it first");
            return;
        }

        int pulseUs = text.substring(1).toInt();
        if (pulseUs < 1000 || pulseUs > 2000) {
            printRow("-", "-", "-", "REFUSED: use 1000..2000 us, start near 1500");
            return;
        }

        int channel = 0;  // Arm.
        if (text.startsWith("t")) {
            channel = 1;  // Tongue.
        }

        // 50 Hz = 20000 us per cycle. 16-bit PWM ranges from 0 to 65535.
        int duty = pulseUs * 65535L / 20000;
        ledcWrite(channel, duty);
        startedAt = millis();  // pulses run for at most 3 seconds from now
        pulsesOn = true;
        char pulse[12], dutyText[16], meaning[48];
        snprintf(pulse, sizeof(pulse), "%d", pulseUs);
        snprintf(dutyText, sizeof(dutyText), "%d / 65535", duty);
        snprintf(meaning, sizeof(meaning), "moving, %+d us from centre 1500; off in 3 s", pulseUs - 1500);
        printRow(channel == 0 ? "ARM" : "TONGUE", pulse, dutyText, meaning);
        return;
    }

    printRow("-", "-", "-", "UNKNOWN command: type a1500, t1500 or x");
}

void setup() {
    pinMode(stopPin, INPUT_PULLUP);

    ledcSetup(0, 50, 16);  // Arm: channel 0, 50 Hz, 16-bit resolution.
    ledcSetup(1, 50, 16);  // Tongue: channel 1.
    ledcAttachPin(armPin, 0);
    ledcAttachPin(tonguePin, 1);
    turnOff();

    Serial.begin(115200);
    delay(500);
    monitor::title("STAGE 1g: SERVOS + STOP",
                   "each servo moves when told, and STOP or 3 s always cuts the pulses.",
                   "a1500 / t1500 centre a servo; it goes limp by itself 3 s later.");
    monitor::section("WIRING");
    monitor::item("Arm signal", "GPIO%d (orange wire)", armPin);
    monitor::item("Tongue signal", "GPIO%d (orange wire)", tonguePin);
    monitor::item("STOP button", "GPIO%d and GND", stopPin);
    monitor::item("Servo red", "5V charger via toggle switch (NOT the ESP32)");
    monitor::item("Servo brown", "charger GND, joined to ESP32 GND");
    monitor::item("1000uF capacitor", "+ to servo red, striped - to servo brown");

    monitor::section("STARTUP CHECK");
    bool stopHeld = digitalRead(stopPin) == LOW;
    monitor::item("STOP button", "%s", stopHeld ? "LOW = pressed" : "HIGH = released, OK");
    monitor::item("Pulses", "OFF (servos limp until you type a command)");
    if (stopHeld) {
        monitor::fail("STOP reads pressed while nobody touches it",
                      "check the STOP wire is on the right pin and not touching GND");
    }

    monitor::section("COMMANDS (type, then Enter)");
    monitor::item("a1500", "arm to centre (try a1400 / a1600 for small moves)");
    monitor::item("t1500", "tongue to centre");
    monitor::item("x", "stop all pulses now");
}

void loop() {
    if (pulsesOn) {
        bool stopPressed = digitalRead(stopPin) == LOW;
        bool timedOut = millis() - startedAt >= 3000;
        if (stopPressed || timedOut) {
            turnOff();
            printRow("both", "-", "0", stopPressed ? "STOP pressed: pulses OFF, servos limp"
                                                   : "3 s timeout: pulses OFF, servos limp");
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
