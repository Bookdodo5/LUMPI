// Quiet Study bench test: noise, PIR and servo warning on one doll.
//
// Wiring: same as doll_full_check (no new wires).
//   Mic:   SCK 26, WS 25, SD 34, L/R GND       (INMP441, I2S port 0)
//   PIR:   OUT 15, VCC 5V, GND                  (HC-SR501)
//   STOP:  GPIO 14 to GND                       (button, internal pull-up)
//   Arm:   signal 21 via 1k                     (servo)
//   Power: GPIO 4 -> D4184 PWM, 10k pull-down   (servo power switch)
//   Servo 5V from separate charger via toggle switch, NOT from ESP32.
//
// Upload environment: quiet_study. Monitor: 115200.
//
// Wait 60s for PIR warm-up. Then:
//   1. Walk up to the doll. PIR triggers -> INVITED -> MONITORING.
//   2. Talk or clap near the mic. If loud for 1s -> WARNING (servo shush).
//   3. After warning, 30s cooldown before the next warning.
//   4. Walk away. PIR goes LOW for 10s -> back to IDLE.
//   5. Press STOP at any time -> servo off, back to IDLE.
//
// Tune NOISE_THRESHOLD below with your room. Quiet ~1000-2000, talking ~5000-10000.
#include <Arduino.h>
#include <driver/i2s.h>
#include "settings.h"

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

// --- Servo helpers ---

void servosOff() {
    digitalWrite(doll_pins::servo_power, LOW);
    ledcWrite(0, 0);
}

void servoArm(int us) {
    ledcWrite(0, us * 65535L / 20000);
}

void shushGesture() {
    // Centre first, then power on, wave, power off. Under 2 seconds total.
    servoArm(1500);
    digitalWrite(doll_pins::servo_power, HIGH);
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
    Serial.printf("[%s]\n", stateNames[state]);
}

void setup() {
    // Servo power off first, before anything else.
    pinMode(doll_pins::servo_power, OUTPUT);
    digitalWrite(doll_pins::servo_power, LOW);
    pinMode(doll_pins::arm, OUTPUT);
    digitalWrite(doll_pins::arm, LOW);

    ledcSetup(0, 50, 16);
    ledcAttachPin(doll_pins::arm, 0);
    servosOff();

    // STOP button.
    pinMode(doll_pins::stop, INPUT_PULLUP);

    // PIR.
    pinMode(doll_pins::pir, INPUT);

    Serial.begin(115200);
    delay(500);
    Serial.println("Quiet Study bench test");
    Serial.printf("Threshold=%d, noisy_count=%d, cooldown=%lus, pir_timeout=%lus\n",
                  NOISE_THRESHOLD, NOISY_COUNT, COOLDOWN_MS / 1000, PIR_TIMEOUT_MS / 1000);

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
        Serial.println("Microphone ready");
    } else {
        Serial.println("Microphone FAILED — check wiring");
    }

    // PIR warm-up.
    Serial.println("PIR warming up for 60 seconds...");
    for (int s = 60; s > 0; s--) {
        // Check STOP during warm-up too.
        if (digitalRead(doll_pins::stop) == LOW) {
            Serial.println("STOP pressed during warm-up");
        }
        if (s % 10 == 0) Serial.printf("  %d s left\n", s);
        delay(1000);
    }
    Serial.println("Ready.");
    enterState(IDLE);
}

void loop() {
    unsigned long now = millis();

    // --- STOP button: always checked ---
    if (digitalRead(doll_pins::stop) == LOW) {
        servosOff();
        if (state != IDLE) {
            Serial.println("[STOP] Servo power off -> IDLE");
            enterState(IDLE);
        }
        delay(200);  // debounce
        return;
    }

    bool pirHigh = digitalRead(doll_pins::pir) == HIGH;

    switch (state) {

    case IDLE:
        if (pirHigh) {
            pirHighSince = now;
            Serial.println("[IDLE] PIR: Motion detected");
            enterState(INVITED);
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
                Serial.println("[INVITED] Presence confirmed -> MONITORING");
                pirLowSince = 0;
                enterState(MONITORING);
            }
        } else if (pirLowSince && (now - pirLowSince >= PIR_TIMEOUT_MS)) {
            Serial.println("[INVITED] PIR lost -> IDLE");
            enterState(IDLE);
        }
        delay(100);
        break;

    case MONITORING:
        if (now - lastPrint >= 250 && micReady) {
            lastPrint = now;
            int peak = readPeak();
            bool loud = peak >= NOISE_THRESHOLD;

            if (loud) {
                loudCount++;
                Serial.printf("[MONITORING] peak=%d (LOUD %d/%d)\n", peak, loudCount, NOISY_COUNT);
            } else {
                if (loudCount > 0) {
                    Serial.printf("[MONITORING] peak=%d (quiet, reset count)\n", peak);
                }
                loudCount = 0;
            }

            if (loudCount >= NOISY_COUNT) {
                Serial.println("[MONITORING] -> WARNING: too noisy!");
                enterState(WARNING);
                // Do the gesture right here.
                Serial.println("[WARNING] Servo shush gesture");
                shushGesture();
                // Move to cooldown.
                cooldownStart = millis();
                Serial.printf("[COOLDOWN] %lu s\n", COOLDOWN_MS / 1000);
                enterState(COOLDOWN);
                break;
            }
        }

        // PIR timeout: child left.
        if (!pirHigh) {
            if (pirLowSince == 0) pirLowSince = now;
            if (now - pirLowSince >= PIR_TIMEOUT_MS) {
                Serial.println("[MONITORING] PIR: no motion for 10s -> IDLE");
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
            Serial.println("[COOLDOWN] Done -> MONITORING");
            enterState(MONITORING);
        } else if (now - lastPrint >= 5000) {
            lastPrint = now;
            Serial.printf("[COOLDOWN] %lu s remaining\n", (COOLDOWN_MS - elapsed) / 1000);
        }

        // PIR timeout during cooldown too.
        if (!pirHigh) {
            if (pirLowSince == 0) pirLowSince = now;
            if (now - pirLowSince >= PIR_TIMEOUT_MS) {
                Serial.println("[COOLDOWN] PIR: no motion -> IDLE");
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
