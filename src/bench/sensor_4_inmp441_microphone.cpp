// INMP441 microphone, on the doll.
// VDD -> 3.3V, GND -> GND, L/R -> GND.
// SCK -> GPIO26, WS -> GPIO25, SD -> GPIO34.
// Upload environment: microphone. Monitor: 115200.
// Speak, clap, then stay quiet. "Peak" should rise and fall with sound.
// Always zero? Check SD and L/R. This number is not calibrated decibels.
// I2S setup is longer because the microphone sends digital audio, not analog.
// Guide: https://lumpi-build-guide.vercel.app/station/microphone
#include <Arduino.h>
#include <driver/i2s.h>

bool microphoneReady = false;

void setup() {
    Serial.begin(115200);
    delay(500);

    // Receive 16000 samples/second, using the microphone's left channel.
    i2s_config_t audio = {};
    audio.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX);
    audio.sample_rate = 16000;
    audio.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    audio.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    audio.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    audio.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    // Four audio buffers let the hardware transfer samples in the background.
    audio.dma_buf_count = 4;
    audio.dma_buf_len = 256;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = 26;
    pins.ws_io_num = 25;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = 34;

    esp_err_t result = i2s_driver_install(I2S_NUM_0, &audio, 0, nullptr);
    if (result == ESP_OK) {
        result = i2s_set_pin(I2S_NUM_0, &pins);
    }

    microphoneReady = result == ESP_OK;
    Serial.println(microphoneReady ? "Microphone ready" : "Microphone setup failed");
}

void loop() {
    if (!microphoneReady) {
        delay(1000);
        return;
    }

    int32_t samples[512];
    size_t bytesRead = 0;
    esp_err_t result = i2s_read(
        I2S_NUM_0, samples, sizeof(samples), &bytesRead, pdMS_TO_TICKS(200)
    );

    if (result != ESP_OK || bytesRead == 0) {
        Serial.println("No audio samples received");
        delay(200);
        return;
    }

    int peak = 0;
    int sampleCount = bytesRead / sizeof(int32_t);

    for (int i = 0; i < sampleCount; i++) {
        int sample = samples[i] >> 16;  // Keep the upper 16 bits.
        int level = abs(sample);
        if (level > peak) {
            peak = level;
        }
    }

    Serial.print("Peak: ");
    Serial.println(peak);
    delay(100);
}
