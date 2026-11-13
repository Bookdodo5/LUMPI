// MAX98357A amplifier and speaker, on the doll.
// VIN -> ESP32 USB 5V, GND -> ESP32 GND (not servo LOAD).
// BCLK -> GPIO16, LRC -> GPIO17, DIN -> GPIO5. Leave SD unconnected.
// Speaker wires -> amplifier SPK+ and SPK-. Neither wire goes to GND.
// Solder the amplifier headers/terminal block and speaker wires if loose.
// Upload environment: speaker. Monitor: 115200.
// Send 1 to hear a quiet 440 Hz tone for one second. No audio files needed.
// This tests the speaker, not the final voice clips.
// Guide: https://lumpi-build-guide.vercel.app/station/speaker
#include <Arduino.h>
#include <driver/i2s.h>
#include <cmath>

bool speakerReady = false;

void setup() {
    Serial.begin(115200);
    delay(500);

    // Send 16-bit audio at 16000 samples/second on I2S port 1.
    i2s_config_t audio = {};
    audio.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX);
    audio.sample_rate = 16000;
    audio.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    audio.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    audio.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    audio.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    // Four audio buffers let the hardware transfer samples in the background.
    audio.dma_buf_count = 4;
    audio.dma_buf_len = 256;
    audio.tx_desc_auto_clear = true;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = 16;
    pins.ws_io_num = 17;
    pins.data_out_num = 5;
    pins.data_in_num = I2S_PIN_NO_CHANGE;

    esp_err_t result = i2s_driver_install(I2S_NUM_1, &audio, 0, nullptr);
    if (result == ESP_OK) {
        result = i2s_set_pin(I2S_NUM_1, &pins);
    }
    speakerReady = result == ESP_OK;
    Serial.println(speakerReady ? "Send 1 to play a tone" : "Speaker setup failed");
}

void playTone() {
    int16_t samples[512];  // 256 stereo frames: left, right, left, right...

    for (int start = 0; start < 16000; start += 256) {
        int frames = min(256, 16000 - start);

        for (int i = 0; i < frames; i++) {
            float angle = 2 * PI * 440 * (start + i) / 16000.0f;
            int16_t value = 1500 * sinf(angle);  // Low volume.
            samples[i * 2] = value;      // Left.
            samples[i * 2 + 1] = value;  // Right.
        }

        size_t bytesToSend = frames * 2 * sizeof(int16_t);
        size_t bytesSent = 0;
        esp_err_t result = i2s_write(
            I2S_NUM_1, samples, bytesToSend, &bytesSent, pdMS_TO_TICKS(500)
        );
        if (result != ESP_OK || bytesSent != bytesToSend) {
            Serial.println("Audio write failed");
            break;
        }
    }
    i2s_zero_dma_buffer(I2S_NUM_1);
}

void loop() {
    if (Serial.available()) {
        char command = Serial.read();
        if (command == '1' && speakerReady) {
            playTone();
            Serial.println("Tone sent. Did you hear it?");
        }
    }
    delay(5);
}
