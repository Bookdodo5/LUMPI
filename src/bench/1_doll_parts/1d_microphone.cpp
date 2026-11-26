// INMP441 microphone, on the doll.
// VDD -> 3.3V, GND -> GND, L/R -> GND.
// SCK -> GPIO26, WS -> GPIO25, SD -> GPIO34.
// Upload environment: s1d_microphone. Monitor: 115200.
// Speak, clap, then stay quiet. "Peak" should rise and fall with sound.
// Always zero? Check SD and L/R. This number is not calibrated decibels.
// Output: a row every 500 ms: time | lowest sample | highest sample | swing | verdict.
// Swing = highest - lowest = how loud. The first row (stay quiet) is the "quiet level";
// later rows say SOUND when the swing is more than 3 times that.
// I2S setup is longer because the microphone sends digital audio, not analog.
// Guide: https://lumpi-build-guide.vercel.app/station/microphone
#include <Arduino.h>
#include <driver/i2s.h>
#include "settings.h"  // all pins live in config/settings.h
#include "monitor.h"

bool microphoneReady = false;
int rows = 0;            // table rows printed; the header repeats every 20
int32_t quietSwing = 0;  // swing of the first row, taken while the room is quiet

void setup() {
    Serial.begin(115200);
    delay(500);
    monitor::title("STAGE 1d: MICROPHONE",
                   "the INMP441 sends sound samples that grow when you speak or clap.",
                   "swing changes all the time, and a clap shows SOUND (swing > 3x quiet).");
    monitor::section("WIRING (INMP441 -> ESP32)");
    monitor::item("VDD", "3.3V");
    monitor::item("GND", "GND");
    monitor::item("L/R", "GND  (selects the left channel)");
    monitor::item("SCK", "GPIO%d", doll_pins::mic_sck);
    monitor::item("WS", "GPIO%d", doll_pins::mic_ws);
    monitor::item("SD", "GPIO%d  (the sound data)", doll_pins::mic_sd);

    // Receive 16000 samples/second, using the microphone's left channel.
    i2s_config_t audio = {};
    audio.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX);
    audio.sample_rate = 16000;
    audio.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    audio.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;  // L/R on GND. Yes, RIGHT: this ESP32 core swaps left and right (tested 2026-11-22)
    audio.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    audio.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    // Four audio buffers let the hardware transfer samples in the background.
    audio.dma_buf_count = 8;
    audio.dma_buf_len = 512;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = doll_pins::mic_sck;      // SCK (guide pin map; 23/22/19 belong to RFID and the tongue servo)
    pins.ws_io_num = doll_pins::mic_ws;       // WS
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = doll_pins::mic_sd;     // SD (input-only pin)

    esp_err_t result = i2s_driver_install(I2S_NUM_0, &audio, 0, nullptr);
    if (result == ESP_OK) {
        result = i2s_set_pin(I2S_NUM_0, &pins);
        REG_SET_BIT(I2S_TIMING_REG(I2S_NUM_0), BIT(9));
        REG_SET_BIT(I2S_CONF_REG(I2S_NUM_0), I2S_RX_MSB_SHIFT);
    }

    microphoneReady = result == ESP_OK;
    monitor::section("STARTUP CHECK");
    monitor::item("I2S driver", "%s", microphoneReady ? "started, OK" : esp_err_to_name(result));
    if (!microphoneReady) {
        monitor::fail("the ESP32 could not start its I2S (digital audio) port",
                      "this is a software/pin setup problem, not wiring; re-upload and reset");
    }
    monitor::section("LIVE (stay quiet for the first row, then speak or clap)");
}

void loop() {
    static int32_t samples[512];
    static int32_t lo = INT32_MAX, hi = INT32_MIN;
    static unsigned long last = 0;
    size_t bytesRead = 0;
    i2s_read(I2S_NUM_0, samples, sizeof(samples), &bytesRead, portMAX_DELAY);
    int n = bytesRead / sizeof(int32_t);
    for (int i = 0; i < n; i++) {
        int32_t s = samples[i] >> 8;           // 24-bit value
        if (s < lo) lo = s;
        if (s > hi) hi = s;
    }
    if (millis() - last > 500) {               // one row every 500 ms
        int32_t swing = hi - lo;
        if (rows == 0) quietSwing = swing;     // the first row sets the quiet level
        const char* verdict = "quiet";
        if (swing == 0) {
            verdict = "NO DATA: all samples equal, check SD wire and L/R to GND";
        } else if (rows == 0) {
            verdict = "quiet level (reference)";
        } else if (swing > 3 * quietSwing) {
            verdict = "SOUND";
        }
        if (rows % 20 == 0) {
            Serial.println("\n  time s |    lowest |   highest |     swing | verdict");
            Serial.println("  -------+-----------+-----------+-----------+------------------------");
        }
        rows++;
        Serial.printf("  %6.1f | %9ld | %9ld | %9ld | %s\n", millis() / 1000.0, (long)lo, (long)hi,
                      (long)swing, verdict);
        lo = INT32_MAX; hi = INT32_MIN; last = millis();
    }
}
