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
    audio.dma_buf_count = 8;
    audio.dma_buf_len = 512;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = 23;      // SCK
    pins.ws_io_num = 22;       // WS
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = 19;     // SD

    esp_err_t result = i2s_driver_install(I2S_NUM_0, &audio, 0, nullptr);
    if (result == ESP_OK) {
        result = i2s_set_pin(I2S_NUM_0, &pins);
        gpio_set_pull_mode(GPIO_NUM_19, GPIO_PULLDOWN_ONLY);
        REG_SET_BIT(I2S_TIMING_REG(I2S_NUM_0), BIT(9));
        REG_SET_BIT(I2S_CONF_REG(I2S_NUM_0), I2S_RX_MSB_SHIFT);
    }

    microphoneReady = result == ESP_OK;
    Serial.println(microphoneReady ? "Microphone ready" : "Microphone setup failed");
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
    if (millis() - last > 250) {               // print 4 times a second
        Serial.printf("min %ld  max %ld\n", lo, hi);
        lo = INT32_MAX; hi = INT32_MIN; last = millis();
    }
}