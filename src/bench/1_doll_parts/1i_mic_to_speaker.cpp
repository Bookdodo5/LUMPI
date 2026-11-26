// MICROPHONE -> SPEAKER, on the doll: record 3 s, then play it back.
// Proves the whole sound path the doll uses: the mic records what the child says,
// and the speaker can play sound back. Do 1d_microphone and 1h_speaker first.
// Wiring (config/settings.h, doll_pins):
//   INMP441 mic:      VDD 3.3V, GND, L/R GND, SCK 26, WS 25, SD 34
//   MAX98357A amp:    VIN 5V, GND, BCLK 16, LRC 17, DIN 5, speaker on SPK+ / SPK-
// Upload environment: s1i_mic_to_speaker. Monitor so keys can be typed:
//   pio device monitor -b 115200 --echo --eol LF --filter send_on_enter
// Press RECORD (or type r + Enter), speak for 3 s, and hear it back.
// Too quiet or distorted? Change mic_shift below (lower = louder).
#include <Arduino.h>
#include <driver/i2s.h>
#include "monitor.h"
#include "settings.h"  // all pins live in config/settings.h

constexpr int sample_rate = 16000;        // samples per second, same as the real doll
constexpr int record_seconds = 3;
constexpr int samples_max = sample_rate * record_seconds;  // 48000 samples = 96 kB
constexpr int mic_shift = 14;             // mic sample (32-bit) -> 16-bit. Lower = louder. Tune by ear.

int16_t* recording = nullptr;

bool start_mic() {  // I2S port 0, receive
    i2s_config_t config{};
    config.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX);
    config.sample_rate = sample_rate;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    config.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;  // L/R on GND. Yes, RIGHT: this ESP32 core swaps left and right
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 8;
    config.dma_buf_len = 256;
    i2s_pin_config_t pins{};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = doll_pins::mic_sck;
    pins.ws_io_num = doll_pins::mic_ws;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = doll_pins::mic_sd;
    if (i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) != ESP_OK || i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
        return false;
    }
    REG_SET_BIT(I2S_TIMING_REG(I2S_NUM_0), BIT(9));  // INMP441 timing fix, same as 1d_microphone
    REG_SET_BIT(I2S_CONF_REG(I2S_NUM_0), I2S_RX_MSB_SHIFT);
    return true;
}

bool start_speaker() {  // I2S port 1, transmit
    i2s_config_t config{};
    config.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX);
    config.sample_rate = sample_rate;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;  // stereo: each sample is sent twice
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 4;
    config.dma_buf_len = 256;
    config.tx_desc_auto_clear = true;  // silence, not noise, after the last sample
    i2s_pin_config_t pins{};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = doll_pins::speaker_bclk;
    pins.ws_io_num = doll_pins::speaker_lrc;
    pins.data_out_num = doll_pins::speaker_din;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    return i2s_driver_install(I2S_NUM_1, &config, 0, nullptr) == ESP_OK && i2s_set_pin(I2S_NUM_1, &pins) == ESP_OK;
}

// Records up to 3 s (STOP ends early). Returns how many samples were recorded.
int record() {
    i2s_zero_dma_buffer(I2S_NUM_0);
    int32_t raw[256];
    int count = 0;
    while (count < samples_max) {
        if (digitalRead(doll_pins::stop) == LOW && count > sample_rate / 2) {
            break;  // STOP pressed, after at least 0.5 s
        }
        size_t bytes = 0;
        i2s_read(I2S_NUM_0, raw, sizeof(raw), &bytes, pdMS_TO_TICKS(100));
        for (size_t i = 0; i < bytes / 4 && count < samples_max; ++i) {
            recording[count++] = constrain(raw[i] >> mic_shift, -32768, 32767);
        }
    }
    return count;
}

void play(int count) {
    int16_t frames[512];  // 256 stereo frames: the same sample left and right
    for (int start = 0; start < count; start += 256) {
        const int n = min(256, count - start);
        for (int i = 0; i < n; ++i) {
            frames[i * 2] = frames[i * 2 + 1] = recording[start + i];
        }
        size_t sent = 0;
        i2s_write(I2S_NUM_1, frames, n * 4, &sent, pdMS_TO_TICKS(500));
    }
    i2s_zero_dma_buffer(I2S_NUM_1);
}

void record_and_play() {
    Serial.println("\n  1/3 RECORDING  3 s ... speak now (STOP ends early)");
    const int count = record();
    int loudest = 0;
    for (int i = 0; i < count; ++i) {
        loudest = max(loudest, abs(int(recording[i])));
    }
    const int percent = loudest * 100 / 32767;
    const char* verdict = loudest == 0       ? "SILENT: the mic sent only zeros (check SD and L/R, run s1d_microphone)"
                        : percent < 3        ? "very quiet: speak closer, or lower mic_shift"
                        : loudest >= 32767   ? "CLIPPING: too loud, raise mic_shift"
                                             : "ok";
    Serial.printf("  2/3 RECORDED   %.1f s, loudest sample %d of 32767 (%d%%): %s\n", count / float(sample_rate),
                  loudest, percent, verdict);
    Serial.println("  3/3 PLAYING    listen to the doll ...");
    play(count);
    Serial.println("      Pass = you hear your own voice, clear and not distorted.");
    Serial.println("      Press RECORD (or type r) to try again.");
}

void setup() {
    Serial.begin(115200);
    delay(500);
    monitor::title("STAGE 1i: MICROPHONE -> SPEAKER",
                   "record 3 s from the mic, then play it back on the speaker",
                   "you hear your own voice back, clear and not distorted");
    monitor::section("WIRING (config/settings.h, doll_pins)");
    monitor::item("Microphone", "SCK %d, WS %d, SD %d, L/R GND, VDD 3.3V", doll_pins::mic_sck, doll_pins::mic_ws,
                  doll_pins::mic_sd);
    monitor::item("Speaker amp", "BCLK %d, LRC %d, DIN %d, VIN 5V", doll_pins::speaker_bclk, doll_pins::speaker_lrc,
                  doll_pins::speaker_din);
    monitor::item("RECORD / STOP", "GPIO%d / GPIO%d, pressed = LOW", doll_pins::record, doll_pins::stop);
    pinMode(doll_pins::record, INPUT_PULLUP);
    pinMode(doll_pins::stop, INPUT_PULLUP);

    monitor::section("PARTS");
    recording = static_cast<int16_t*>(malloc(samples_max * sizeof(int16_t)));
    const bool mic_ok = start_mic();
    const bool speaker_ok = start_speaker();
    monitor::item("Memory", "%s", recording ? "96 kB for 3 s of sound" : "NO ROOM for 96 kB");
    monitor::item("Microphone", "%s", mic_ok ? "started" : "I2S driver did not start");
    monitor::item("Speaker", "%s", speaker_ok ? "started" : "I2S driver did not start");
    if (!recording || !mic_ok || !speaker_ok) {
        monitor::fail("cannot run", "see the line above; reset the board and try again");
        while (true) {
            delay(1000);
        }
    }
    monitor::section("TEST (press RECORD, or type r then Enter)");
}

void loop() {
    static uint32_t low_since_ms = 0;  // RECORD counts after 35 ms of LOW (buttons bounce)
    static bool done = false;
    bool go = false;
    if (digitalRead(doll_pins::record) == HIGH) {
        low_since_ms = 0;
        done = false;
    } else if (low_since_ms == 0) {
        low_since_ms = millis();
    } else if (!done && millis() - low_since_ms >= 35) {
        done = true;
        go = true;
    }
    while (Serial.available()) {
        if (Serial.read() == 'r') {
            go = true;
        }
    }
    if (go) {
        record_and_play();
    }
    delay(10);
}
