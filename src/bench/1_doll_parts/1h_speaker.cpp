// MAX98357A amplifier and speaker, on the doll: plays a WAV file on repeat.
// VIN -> ESP32 5V (VIN/USB pin), GND -> ESP32 GND.
// BCLK -> GPIO16, LRC -> GPIO17, DIN -> GPIO5. Leave SD and GAIN unconnected.
// Speaker wires -> amplifier SPK+ and SPK-. Neither wire goes to GND.
//
// The file: data/audio/song.wav, 8000 Hz, mono, 16-bit (about 70 s fits).
// Make it from any audio/video file with ffmpeg:
//   ffmpeg -i input.mp4 -t 20 -ac 1 -ar 8000 -sample_fmt s16 data/audio/song.wav
// Upload order (PlatformIO > s1h_speaker): 1. Upload Filesystem Image  2. Upload.
// Output: wiring, startup checks (file system, I2S), then two rows per play:
// time | data bytes | length | what you should hear.
// Guide: https://lumpi-build-guide.vercel.app/station/speaker
#include <Arduino.h>
#include <LittleFS.h>
#include <driver/i2s.h>
#include "settings.h"  // all pins live in config/settings.h
#include "monitor.h"

const char* FILE_PATH = "/audio/song.wav";
const int RATE = 8000;  // Must match the WAV file. Low quality: fine for a test.
const int FRAMES = 256;  // Samples per chunk (16 ms).
const float VOLUME = 1.0;  // 1.0 = as recorded. Lower to 0.5 if the speaker buzzes.

bool speakerReady = false;
int rows = 0;  // table rows printed; the header repeats every 20

void printRow(uint32_t bytes, float seconds, const char* meaning) {
    if (rows % 20 == 0) {
        Serial.println("\n  time s | data bytes | length  | what you should hear");
        Serial.println("  -------+------------+---------+------------------------------------");
    }
    rows++;
    Serial.printf("  %6.1f | %10lu | %5.1f s | %s\n", millis() / 1000.0, (unsigned long)bytes, seconds, meaning);
}

bool startI2S() {
    i2s_config_t audio = {};
    audio.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX);
    audio.sample_rate = RATE;
    audio.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    audio.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    audio.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    audio.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    audio.dma_buf_count = 8;  // More buffers ride out slow file reads.
    audio.dma_buf_len = FRAMES;
    audio.tx_desc_auto_clear = true;

    i2s_pin_config_t pins = {};
    pins.bck_io_num = doll_pins::speaker_bclk;
    pins.ws_io_num = doll_pins::speaker_lrc;
    pins.data_out_num = doll_pins::speaker_din;
    pins.data_in_num = I2S_PIN_NO_CHANGE;

    esp_err_t result = i2s_driver_install(I2S_NUM_1, &audio, 0, nullptr);
    if (result == ESP_OK) result = i2s_set_pin(I2S_NUM_1, &pins);
    monitor::item("I2S port", "%s", result == ESP_OK ? "started, OK" : esp_err_to_name(result));
    if (result != ESP_OK) {
        monitor::fail("the ESP32 could not start its I2S (digital audio) port",
                      "a software/pin setup problem, not wiring; re-upload and reset");
    }
    return result == ESP_OK;
}

// A WAV file is a list of chunks: "fmt " (format) then "data" (the samples).
// Moves the file position to the start of the samples; returns their size, or 0.
uint32_t findSamples(File& file) {
    file.seek(12);  // Skip "RIFF", file size, "WAVE".
    char id[4];
    uint32_t size;
    while (file.read((uint8_t*)id, 4) == 4 && file.read((uint8_t*)&size, 4) == 4) {
        if (memcmp(id, "fmt ", 4) == 0) {
            uint16_t format, channels, bits;
            uint32_t rate;
            file.read((uint8_t*)&format, 2);
            file.read((uint8_t*)&channels, 2);
            file.read((uint8_t*)&rate, 4);
            file.seek(file.position() + 6);  // Skip byte rate and block align.
            file.read((uint8_t*)&bits, 2);
            if (format != 1 || channels != 1 || rate != RATE || bits != 16) {
                Serial.printf("  FAIL  wrong WAV format: %u channel(s), %u Hz, %u bit\n", channels,
                              (unsigned)rate, bits);
                Serial.printf("        means: remake it as 1 channel, %d Hz, 16 bit (ffmpeg line at top of file)\n", RATE);
                return 0;
            }
            file.seek(file.position() + size - 16);  // Skip any extra fmt bytes.
        } else if (memcmp(id, "data", 4) == 0) {
            return size;
        } else {
            file.seek(file.position() + size + (size & 1));  // Skip other chunks.
        }
    }
    monitor::fail("no audio data found in the WAV file",
                  "the file is not a normal WAV; remake it with the ffmpeg line at the top of the file");
    return 0;
}

void playFile() {
    File file = LittleFS.open(FILE_PATH, "r");
    if (!file) {
        Serial.printf("  FAIL  %s is not on the board\n", FILE_PATH);
        Serial.println("        means: run Upload Filesystem Image (s1h_speaker), then Upload again");
        delay(5000);
        return;
    }
    uint32_t left = findSamples(file);
    if (left == 0) {
        file.close();
        delay(5000);
        return;
    }
    float seconds = left / 2.0 / RATE;  // 2 bytes per sample, RATE samples per second
    uint32_t total = left;
    printRow(total, seconds, "PLAYING: music now; silence = check amp wires");

    int16_t mono[FRAMES];
    int16_t stereo[FRAMES * 2];  // The amp takes left + right; send the same to both.
    while (left > 0) {
        size_t got = file.read((uint8_t*)mono, min<uint32_t>(sizeof(mono), left));
        if (got == 0) break;
        left -= got;
        int samples = got / 2;
        for (int i = 0; i < samples; i++) {
            int16_t value = mono[i] * VOLUME;
            stereo[i * 2] = value;
            stereo[i * 2 + 1] = value;
        }
        size_t sent = 0;
        i2s_write(I2S_NUM_1, stereo, samples * 4, &sent, portMAX_DELAY);
    }
    file.close();
    i2s_zero_dma_buffer(I2S_NUM_1);
    printRow(total, seconds, "done: 1 s of silence, then it repeats");
}

void setup() {
    Serial.begin(115200);
    delay(500);
    monitor::title("STAGE 1h: SPEAKER", "the amplifier plays song.wav from the board's memory.",
                   "you hear the song, then 1 s of silence, then it repeats.");
    monitor::section("WIRING (MAX98357A -> ESP32)");
    monitor::item("VIN", "ESP32 5V (VIN/USB pin)");
    monitor::item("GND", "ESP32 GND");
    monitor::item("BCLK", "GPIO%d", doll_pins::speaker_bclk);
    monitor::item("LRC", "GPIO%d", doll_pins::speaker_lrc);
    monitor::item("DIN", "GPIO%d", doll_pins::speaker_din);
    monitor::item("SD, GAIN", "not connected");
    monitor::item("Speaker", "SPK+ and SPK- (neither to GND)");

    monitor::section("STARTUP CHECK");
    bool filesOk = LittleFS.begin();
    monitor::item("File system", "%s", filesOk ? "mounted, OK" : "failed");
    if (!filesOk) {
        monitor::fail("the board's file system could not be read",
                      "run Upload Filesystem Image (s1h_speaker) first, then Upload");
        return;
    }
    monitor::item("Sound file", "%s %s", FILE_PATH, LittleFS.exists(FILE_PATH) ? "found" : "MISSING");
    speakerReady = startI2S();
    monitor::section("LIVE");
}

// Plays the file forever, with a 1 second pause between repeats.
void loop() {
    if (speakerReady) playFile();
    delay(1000);
}
