// Doll controls: RECORD and STOP buttons, and the page knob.
// RECORD    -> GPIO13 and GND. No resistor. Read by INTERRUPT (course item E2).
// STOP      -> GPIO14 and GND. No resistor. Polled.
// Page knob -> potentiometer (10k is fine): outer legs to 3.3V and GND, middle leg to GPIO36.
//              If turning right goes back a page, swap the two outer legs.
// Upload environment: s1a_buttons. Monitor: 115200.
// Output: a title, the WIRING list, a startup check of each input, then a table
// with one row per press or page step: time | input | raw reading | meaning.
// Guide: https://lumpi-build-guide.vercel.app/station/buttons
//
// How RECORD works (interrupt + deferred processing, no polling):
//   1. Pressing RECORD pulls GPIO13 from HIGH to LOW: a FALLING edge.
//   2. The hardware runs recordIsr() at once. It only wakes recordTask and returns.
//   3. recordTask, which was asleep, waits 35 ms for the contacts to stop bouncing,
//      then checks the pin. Still LOW = a real press.
//
// How the knob works (a dial, not a pointer):
//   The knob does not point at a page. Each step of turning (1/20 of its travel,
//   about 13 degrees) moves one page forward or back from wherever the knob is now.
//   So a new book always starts at page 1, whatever the knob position.
#include <Arduino.h>
#include "settings.h"  // all pins live in config/settings.h
#include "monitor.h"

const int RECORD_PIN = doll_pins::record;
const int STOP_PIN = doll_pins::stop;
const int KNOB_PIN = doll_pins::page_knob;      // ADC1: still works while Wi-Fi is on.
const int STEP = 4095 / 20;   // ADC counts per page step. Raise for bigger steps.
const int PAGE_COUNT = 30;    // Pages in the test book.

TaskHandle_t recordTask = nullptr;
int rows = 0;  // table rows printed; the header repeats every 20

// One table row. raw = what the pin read; meaning = the same thing in plain words.
void printRow(const char* input, const char* raw, const char* meaning) {
    if (rows % 20 == 0) {
        Serial.println("\n  time s | input  | raw reading     | meaning");
        Serial.println("  -------+--------+-----------------+------------------------");
    }
    rows++;
    Serial.printf("  %6.1f | %-6s | %-15s | %s\n", millis() / 1000.0, input, raw, meaning);
}

// The ISR. Rules: be short, never print, never delay. IRAM_ATTR keeps it in fast RAM.
void IRAM_ATTR recordIsr() {
    BaseType_t higherPriorityWoken = pdFALSE;
    vTaskNotifyGiveFromISR(recordTask, &higherPriorityWoken);
    portYIELD_FROM_ISR(higherPriorityWoken);  // Switch to recordTask right away if needed.
}

// The deferred processing: all the real work happens here, outside the ISR.
void recordTaskLoop(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Sleep until the ISR wakes us.
        vTaskDelay(pdMS_TO_TICKS(35));            // Let the contact bounce settle.
        ulTaskNotifyTake(pdTRUE, 0);              // Drop the extra wake-ups the bounce caused.

        // Release also bounces and can cause a FALLING edge; then the pin is HIGH: ignore.
        if (digitalRead(RECORD_PIN) == LOW) {
            printRow("RECORD", "GPIO LOW", "pressed (seen by interrupt)");
        }
    }
}

// The ESP32's analog reading jitters by a few counts. Averaging 8 reads smooths it.
int readKnob() {
    long sum = 0;
    for (int i = 0; i < 8; i++) sum += analogRead(KNOB_PIN);
    return sum / 8;
}

int page = 1;
int anchor = 0;  // Knob reading at the last page step.

void checkKnob() {
    int reading = readKnob();
    // Move one page only after a full STEP away from the anchor. Because the anchor
    // moves by exactly one STEP, going back needs a full STEP the other way:
    // jitter near a boundary can never flip the page back and forth.
    while (reading - anchor >= STEP) {
        anchor += STEP;
        if (page < PAGE_COUNT) page++;
        char raw[16], meaning[40];
        snprintf(raw, sizeof(raw), "ADC %d", reading);
        snprintf(meaning, sizeof(meaning), "NEXT -> page %d of %d", page, PAGE_COUNT);
        printRow("KNOB", raw, meaning);
    }
    while (anchor - reading >= STEP) {
        anchor -= STEP;
        if (page > 1) page--;
        char raw[16], meaning[40];
        snprintf(raw, sizeof(raw), "ADC %d", reading);
        snprintf(meaning, sizeof(meaning), "PREVIOUS -> page %d of %d", page, PAGE_COUNT);
        printRow("KNOB", raw, meaning);
    }
}

bool lastStop = HIGH, stableStop = HIGH;
unsigned long stopChangedAt = 0;

void checkStop() {
    bool reading = digitalRead(STOP_PIN);
    if (reading != lastStop) {
        stopChangedAt = millis();
        lastStop = reading;
    }
    // Ignore contact bounce. Accept a change only after 35 ms.
    if (millis() - stopChangedAt >= 35 && reading != stableStop) {
        stableStop = reading;
        if (reading == LOW) printRow("STOP", "GPIO LOW", "pressed");
    }
}

void setup() {
    Serial.begin(115200);
    delay(500);
    monitor::title("STAGE 1a: BUTTONS + PAGE KNOB",
                   "RECORD and STOP give one row per press; the knob turns pages.",
                   "one row per press (no doubles); about 1/20 of a knob turn = 1 page.");
    monitor::section("WIRING");
    monitor::item("RECORD button", "GPIO%d and GND, no resistor", RECORD_PIN);
    monitor::item("STOP button", "GPIO%d and GND, no resistor", STOP_PIN);
    monitor::item("Knob middle leg", "GPIO%d", KNOB_PIN);
    monitor::item("Knob outer legs", "3.3V and GND (swap them if the direction is wrong)");

    pinMode(STOP_PIN, INPUT_PULLUP);
    pinMode(RECORD_PIN, INPUT_PULLUP);
    analogSetPinAttenuation(KNOB_PIN, ADC_11db);  // Read the full 0-3.3 V range.
    anchor = readKnob();  // Wherever the knob is now counts as page 1.

    // Startup check. A released button reads HIGH (the pull-up); LOW means held or shorted.
    monitor::section("STARTUP CHECK (do not touch anything)");
    bool recordUp = digitalRead(RECORD_PIN) == HIGH;
    bool stopUp = digitalRead(STOP_PIN) == HIGH;
    monitor::item("RECORD", "%s", recordUp ? "HIGH = released, OK" : "LOW = pressed?");
    monitor::item("STOP", "%s", stopUp ? "HIGH = released, OK" : "LOW = pressed?");
    monitor::item("Knob", "ADC %d of 4095 = %.2f V", anchor, anchor * 3.3 / 4095);
    if (!recordUp || !stopUp) {
        monitor::fail("a button reads pressed while nobody touches it",
                      "check that the button wire is not touching GND or the wrong pin");
    }
    if (anchor == 0 || anchor == 4095) {
        Serial.println("  NOTE  knob reads an end (0 or 4095). Fine if it is turned fully;");
        Serial.println("        if turning it never changes this, check the middle leg wire.");
    }

    // Create the task before the interrupt, so the ISR always has a task to wake.
    xTaskCreate(recordTaskLoop, "record", 2048, nullptr, 2, &recordTask);
    attachInterrupt(digitalPinToInterrupt(RECORD_PIN), recordIsr, FALLING);
    monitor::section("LIVE (press a button or turn the knob)");
    Serial.printf("  Start: page %d of %d. Nothing printed = nothing pressed.\n", page, PAGE_COUNT);
}

void loop() {
    checkStop();
    checkKnob();
    delay(20);  // Polled every 20 ms: a hand cannot turn or press faster than that.
}
