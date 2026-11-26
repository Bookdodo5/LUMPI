// Quiet Study bench test: noise warning with the servo, on one doll.
//
// Wiring: same as doll_full_check (no new wires).
//   Mic:   SCK 26, WS 25, SD 34, L/R GND       (INMP441, I2S port 0)
//   FSR:   pressure mat (book on = child present; threshold doll_tuning::book_on_fsr)
//   STOP:  GPIO 14 to GND                       (button, internal pull-up)
//   Arm:   signal 21 via 1k                     (servo)
//   Servo 5V from separate charger via toggle switch, NOT from ESP32.
//
// Upload environment: x_quiet_study. Monitor: 115200.
//
// Pins come from config/settings.h. Then:
//   1. Put the book on the mat. After 3 s -> INVITED -> MONITORING.
//   2. Talk or clap near the mic. If loud for 1s -> WARNING (servo shush).
//   3. After warning, 30s cooldown before the next warning.
//   4. Take the book off for 10 s -> back to IDLE.
//   5. Press STOP at any time -> servo off, back to IDLE.
//
// Tune NOISE_THRESHOLD below with your room. Quiet ~1000-2000, talking ~5000-10000.
// Output: a table: time | state | raw reading | meaning. A row on every state change
// and every loud reading, plus a steady row (mat or mic level) every 2-5 s.
#include <Arduino.h>
#include <driver/i2s.h>
#include "settings.h"
#include "monitor.h"

// --- Tunables ---
const int NOISE_THRESHOLD   = 8000;  // raw mic peak to count as "loud"
const int NOISY_COUNT        = 4;     // consecutive loud readings (x250ms = 1s)
const unsigned long COOLDOWN_MS     = 30000;
const unsigned long PIR_TIMEOUT_MS  = 10000;
const unsigned long INVITE_CONFIRM_MS = 3000;

// --- State ---
enum State { IDLE, INVITED, MONITORING, WARNING, COOLDOWN };
State state = IDLE;
const char* stateNames[] = {"IDLE", "INVITED", "MONITORING", "WARNING", "COOLDOWN"};

int loudCount = 0;
unsigned long pirHighSince = 0;
unsigned long pirLowSince = 0;
unsigned long cooldownStart = 0;
unsigned long lastPrint = 0;
bool micReady = false;
unsigned long lastIdleRow = 0;
int rows = 0;  // table rows printed; the header repeats every 20

// One table row: time | state | raw reading | meaning.
void logRow(const char* raw, const char* meaning) {
    if (rows % 20 == 0) {
        Serial.println("\n  time s | state      | raw reading     | meaning");
        Serial.println("  -------+------------+-----------------+--------------------------------");
    }
    rows++;
    Serial.printf("  %6.1f | %-10s | %-15s | %s\n", millis() / 1000.0, stateNames[state], raw, meaning);
}

// --- Servo helpers ---
// No power switch pin any more (removed 2026-11-20): duty 0 = no pulses, the servo goes limp.
// The manual toggle switch on the charger is the hard power cut.

void servosOff() {
    ledcWrite(0, 0);
}

void servoArm(int us) {
    ledcWrite(0, us * 65535L / 20000);
}

void shushGesture() {
    // Centre, wave, centre, then pulses off. Under 2 seconds total.
    servoArm(1500);
    delay(300);
    servoArm(1350);
    delay(400);
    servoArm(1650);
    delay(400);
    servoArm(1500);
    delay(300);
    servosOff();
}

// --- Mic peak reading ---

int readPeak() {
    int32_t samples[512];
    size_t bytesRead = 0;
    i2s_read(I2S_NUM_0, samples, sizeof(samples), &bytesRead, pdMS_TO_TICKS(100));
    int n = bytesRead / sizeof(int32_t);
    int peak = 0;
    for (int i = 0; i < n; i++) {
        int s = abs(samples[i] >> 8);
        if (s > peak) peak = s;
    }
    return peak;
}

// --- State transitions ---

void enterState(State next) {
    state = next;
    loudCount = 0;
    logRow("-", "now in this state");
}

void setup() {
    // Servo pulses off first, before anything else.
    pinMode(doll_pins::arm, OUTPUT);
    digitalWrite(doll_pins::arm, LOW);

    ledcSetup(0, 50, 16);
    ledcAttachPin(doll_pins::arm, 0);
    servosOff();

    // STOP button.
    pinMode(doll_pins::stop, INPUT_PULLUP);

    // FSR mat is an ADC input: no pinMode needed.

    Serial.begin(115200);
    delay(500);
    monitor::title("EXTRA: QUIET STUDY", "the doll shushes (arm wave) when the room stays loud while a book is on.",
                   "book on 3 s -> MONITORING; loud for 1 s -> WARNING + wave; then 30 s COOLDOWN.");
    monitor::section("WIRING (same as the full doll check)");
    monitor::item("Mic", "SCK %d, WS %d, SD %d, L/R to GND", doll_pins::mic_sck, doll_pins::mic_ws, doll_pins::mic_sd);
    monitor::item("FSR mat", "GPIO%d, 10k to GND", doll_pins::fsr);
    monitor::item("STOP button", "GPIO%d to GND", doll_pins::stop);
    monitor::item("Arm servo signal", "GPIO%d via 1k", doll_pins::arm);
    monitor::item("Servo 5V", "separate charger via toggle switch, NOT the ESP32");
    monitor::section("SETTINGS");
    monitor::item("Book on above", "%d (doll_tuning::book_on_fsr)", doll_tuning::book_on_fsr);
    monitor::item("Loud above", "mic peak %d (NOISE_THRESHOLD)", NOISE_THRESHOLD);
    monitor::item("Loud for", "%d readings in a row (x 250 ms = 1 s)", NOISY_COUNT);
    monitor::item("Cooldown", "%lu s between warnings", COOLDOWN_MS / 1000);
    monitor::item("Book off for", "%lu s -> back to IDLE", PIR_TIMEOUT_MS / 1000);

    // I2S microphone (port 0, receive).
    i2s_config_t audio = {};
    audio.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX);
    audio.sample_rate = 16000;
    audio.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    audio.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    audio.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    audio.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    audio.dma_buf_count = 8;
    audio.dma_buf_len = 512;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = doll_pins::mic_sck;
    pins.ws_io_num = doll_pins::mic_ws;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = doll_pins::mic_sd;

    if (i2s_driver_install(I2S_NUM_0, &audio, 0, nullptr) == ESP_OK &&
        i2s_set_pin(I2S_NUM_0, &pins) == ESP_OK) {
        micReady = true;
    }
    monitor::section("STARTUP CHECK");
    monitor::item("Microphone (I2S)", "%s", micReady ? "started, OK" : "FAILED");
    if (!micReady) {
        monitor::fail("the microphone port did not start",
                      "re-upload and reset; without it MONITORING never hears anything");
    }
    monitor::section("LIVE");
    enterState(IDLE);
}

void loop() {
    unsigned long now = millis();

    // --- STOP button: always checked ---
    if (digitalRead(doll_pins::stop) == LOW) {
        servosOff();
        if (state != IDLE) {
            logRow("STOP LOW", "STOP pressed: servo off, back to IDLE");
            enterState(IDLE);
        }
        delay(200);  // debounce
        return;
    }

    // ponytail: presence = book on the mat (the PIR was removed). Swap in a better signal if one appears.
    int fsr = analogRead(doll_pins::fsr);
    bool pirHigh = fsr >= doll_tuning::book_on_fsr;
    char raw[24];
    snprintf(raw, sizeof(raw), "mat %d", fsr);

    switch (state) {

    case IDLE:
        if (pirHigh) {
            pirHighSince = now;
            logRow(raw, "book ON the mat");
            enterState(INVITED);
        }
        else if (now - lastIdleRow >= 5000) {
            lastIdleRow = now;
            logRow(raw, "waiting: book OFF the mat");
        }
        delay(250);
        break;

    case INVITED:
        if (!pirHigh) {
            pirLowSince = now;
            // If PIR stays low long enough, cancel the invitation.
        }
        if (pirHigh) {
            pirLowSince = 0;
            if (now - pirHighSince >= INVITE_CONFIRM_MS) {
                logRow(raw, "book stayed 3 s: start listening");
                pirLowSince = 0;
                enterState(MONITORING);
            }
        } else if (pirLowSince && (now - pirLowSince >= PIR_TIMEOUT_MS)) {
            logRow(raw, "book gone: back to IDLE");
            enterState(IDLE);
        }
        delay(100);
        break;

    case MONITORING:
        if (now - lastPrint >= 250 && micReady) {
            lastPrint = now;
            int peak = readPeak();
            bool loud = peak >= NOISE_THRESHOLD;
            char micRaw[24], meaning[48];
            snprintf(micRaw, sizeof(micRaw), "mic peak %d", peak);

            if (loud) {
                loudCount++;
                snprintf(meaning, sizeof(meaning), "LOUD %d of %d (above %d)", loudCount, NOISY_COUNT, NOISE_THRESHOLD);
                logRow(micRaw, meaning);
            } else {
                if (loudCount > 0) {
                    logRow(micRaw, "quiet again: loud count back to 0");
                } else if (now - lastIdleRow >= 2000) {
                    lastIdleRow = now;
                    snprintf(meaning, sizeof(meaning), "quiet (loud above %d)", NOISE_THRESHOLD);
                    logRow(micRaw, meaning);
                }
                loudCount = 0;
            }

            if (loudCount >= NOISY_COUNT) {
                enterState(WARNING);
                // Do the gesture right here.
                logRow("-", "too noisy for 1 s: arm waves (shush)");
                shushGesture();
                // Move to cooldown.
                cooldownStart = millis();
                enterState(COOLDOWN);
                break;
            }
        }

        // PIR timeout: child left.
        if (!pirHigh) {
            if (pirLowSince == 0) pirLowSince = now;
            if (now - pirLowSince >= PIR_TIMEOUT_MS) {
                logRow(raw, "book off for 10 s: back to IDLE");
                enterState(IDLE);
            }
        } else {
            pirLowSince = 0;
        }
        delay(10);
        break;

    case WARNING:
        // Normally we transition straight to COOLDOWN inside MONITORING.
        // If we ever land here, just move on.
        cooldownStart = millis();
        enterState(COOLDOWN);
        break;

    case COOLDOWN: {
        unsigned long elapsed = now - cooldownStart;
        if (elapsed >= COOLDOWN_MS) {
            logRow("-", "cooldown over: listening again");
            enterState(MONITORING);
        } else if (now - lastPrint >= 5000) {
            lastPrint = now;
            char meaning[40];
            snprintf(meaning, sizeof(meaning), "%lu s left, no warnings now", (COOLDOWN_MS - elapsed) / 1000);
            logRow(raw, meaning);
        }

        // PIR timeout during cooldown too.
        if (!pirHigh) {
            if (pirLowSince == 0) pirLowSince = now;
            if (now - pirLowSince >= PIR_TIMEOUT_MS) {
                logRow(raw, "book off for 10 s: back to IDLE");
                enterState(IDLE);
            }
        } else {
            pirLowSince = 0;
        }
        delay(100);
        break;
    }
    }
}
