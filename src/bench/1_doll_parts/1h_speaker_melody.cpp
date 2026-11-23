// SPEAKER MELODY, on the doll: plays a melody on repeat, made by the ESP32 itself.
// No sound file, no "Upload Filesystem Image": if this is silent, the problem is the amp,
// its wiring, its power, or the speaker, never the file.
// Wiring (config/settings.h, doll_pins), MAX98357A amp:
//   VIN -> ESP32 5V, GND -> GND, BCLK -> 16, LRC -> 17, DIN -> 5, SD and GAIN unconnected,
//   speaker on SPK+ and SPK- (neither to GND).
// Upload environment: s1h_speaker_melody. Monitor: 115200.
// To change the tune, edit `melody` below: NOTE+OCTAVE[:beats], e.g. "F#5:0.5". No ":" = 1 beat.
// "|" only separates bars for reading; it is skipped.
#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include "monitor.h"
#include "settings.h"  // all pins live in config/settings.h

// The speaker pins. Env s1h_speaker_melody_alt moves them to 4 / 15 / 2 to test whether
// the usual pins (16 / 17 / 5) are the problem. On an ESP32-WROVER, 16 and 17 belong to its memory chip.
#ifdef ALT_SPEAKER_PINS
constexpr int pin_bclk = 4, pin_lrc = 15, pin_din = 2;
#else
constexpr int pin_bclk = doll_pins::speaker_bclk, pin_lrc = doll_pins::speaker_lrc, pin_din = doll_pins::speaker_din;
#endif

const char* melody =
    "F5:0.5 E5:0.5 F5:0.5 G5:0.5 | A5:1.5 F5:0.5 G5:0.5 A5 F5:0.5 | G5:0.5 A5 C6 G5 G5:2 E5:0.5 F5:0.5 G5 B4:0.5 | "
    "G5:0.5 F5:0.5 E5:0.5 F5 D5:0.5 F5:0.5 G5:0.5 | A5:1.5 F5:0.5 G5:0.5 A5 F5:0.5 | "
    "G5:0.5 A5 C6 G5 G5:3 F#5:0.5 G5:0.5 A5:0.5";

constexpr int sample_rate = 16000;
constexpr float beat_seconds = 0.4;   // tempo: 0.4 s per beat = 150 beats per minute
constexpr float volume = 0.25;        // 0..1 of full scale. Raise for louder; lower if it crackles or cuts out.
constexpr float gap_seconds = 0.02;   // a short silence between notes, so repeated notes are heard separately

// "F#5" -> frequency in Hz, or 0 when the text is not a note.
float note_hz(const String& name) {
    const int semitones[] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G, counted from C
    if (name.length() < 2 || name[0] < 'A' || name[0] > 'G') {
        return 0;
    }
    int semitone = semitones[name[0] - 'A'];
    int at = 1;
    if (name[at] == '#') { semitone += 1; at += 1; }
    else if (name[at] == 'b') { semitone -= 1; at += 1; }
    const int octave = name.substring(at).toInt();
    const int midi = (octave + 1) * 12 + semitone;     // MIDI note number: A4 = 69
    return 440.0f * powf(2.0f, (midi - 69) / 12.0f);   // A4 = 440 Hz; each semitone x 2^(1/12)
}

// Sends one tone (or silence when hz = 0) to the amp. Fades in and out over 5 ms so it does not click.
void play_tone(float hz, float seconds) {
    static float phase = 0;
    const int total = seconds * sample_rate;
    const int fade = sample_rate / 200;  // 5 ms
    int16_t frames[512];                 // 256 stereo frames: the same sample left and right
    for (int start = 0; start < total; start += 256) {
        const int n = min(256, total - start);
        for (int i = 0; i < n; ++i) {
            const int k = start + i;
            const float envelope = min(1.0f, min(k, total - k) / float(fade));
            const int16_t value = hz > 0 ? 32767 * volume * envelope * sinf(phase) : 0;
            phase += 2 * PI * hz / sample_rate;
            if (phase > 2 * PI) phase -= 2 * PI;
            frames[i * 2] = frames[i * 2 + 1] = value;
        }
        size_t sent = 0;
        i2s_write(I2S_NUM_1, frames, n * 4, &sent, portMAX_DELAY);
    }
}

bool start_speaker() {  // I2S port 1, transmit (same settings as the full doll check)
    i2s_config_t config{};
    config.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX);
    config.sample_rate = sample_rate;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 8;
    config.dma_buf_len = 256;
    config.tx_desc_auto_clear = true;  // silence, not noise, if we stop sending
    i2s_pin_config_t pins{};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = pin_bclk;
    pins.ws_io_num = pin_lrc;
    pins.data_out_num = pin_din;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    return i2s_driver_install(I2S_NUM_1, &config, 0, nullptr) == ESP_OK && i2s_set_pin(I2S_NUM_1, &pins) == ESP_OK;
}

void setup() {
    Serial.begin(115200);
    delay(500);
    monitor::title("STAGE 1h: SPEAKER MELODY",
                   "the amp and speaker play a melody made by the ESP32 (no sound file needed)",
                   "the melody plays clearly and repeats without cutting out");
    monitor::section("WIRING (MAX98357A amp)");
    monitor::item("VIN / GND", "ESP32 5V / ESP32 GND");
    monitor::item("BCLK / LRC / DIN", "GPIO%d / GPIO%d / GPIO%d%s", pin_bclk, pin_lrc, pin_din,
                  pin_bclk == 4 ? "  (ALTERNATIVE pins for this test)" : "");
    monitor::item("SD, GAIN", "not connected");
    monitor::item("Speaker", "SPK+ and SPK- (neither to GND)");
    monitor::section("SETTINGS (top of this file)");
    monitor::item("Tempo", "%.2f s per beat", beat_seconds);
    monitor::item("Volume", "%.0f%% of full scale", volume * 100);
    monitor::section("PARTS");
    monitor::item("ESP32 chip", "%s, %d MB flash, PSRAM %s", ESP.getChipModel(), int(ESP.getFlashChipSize() >> 20),
                  psramFound() ? "found (a WROVER: GPIO16/17 are taken!)"
                                : "not seen (to be sure, read the label on the metal cover)");
    if (!start_speaker()) {
        monitor::fail("the ESP32 could not start its I2S (digital audio) port", "reset the board; if it repeats, paste the output");
        while (true) delay(1000);
    }
    monitor::item("I2S port", "started, OK");
    Serial.println("\n  The ESP32 is sending sound now. Silent? Measure VIN to GND while it plays (about 5 V),");
    Serial.println("  and check the speaker wires. The sending side is proven by the lines below.");
}

void loop() {
    static int round = 0;
    round += 1;
    Serial.printf("\n  round %d  note  Hz       beats\n", round);
    const String tune = melody;
    int from = 0, notes = 0;
    while (from < int(tune.length())) {
        int to = tune.indexOf(' ', from);
        if (to < 0) to = tune.length();
        const String token = tune.substring(from, to);  // "F#5:0.5"
        from = to + 1;
        if (token.isEmpty() || token == "|") {
            continue;
        }
        const int colon = token.indexOf(':');
        const String name = colon < 0 ? token : token.substring(0, colon);
        const float beats = colon < 0 ? 1.0f : token.substring(colon + 1).toFloat();
        const float hz = note_hz(name);
        if (hz == 0 || beats <= 0) {
            Serial.printf("          %-5s  not a note: skipped (write it like F#5:0.5)\n", token.c_str());
            continue;
        }
        notes += 1;
        Serial.printf("          %-4s  %7.1f  %.1f\n", name.c_str(), hz, beats);
        play_tone(hz, beats * beat_seconds - gap_seconds);
        play_tone(0, gap_seconds);
    }
    Serial.printf("  round %d done: %d notes. 1 s of silence, then again.\n", round, notes);
    play_tone(0, 1.0);
}
