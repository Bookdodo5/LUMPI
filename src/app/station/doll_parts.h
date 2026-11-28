#pragma once
// PLUMBING FOR THE DOLL (src/app/station/doll.cpp). Read doll.cpp first to understand LUMPI.
//
// Hardware setup and helpers: servos (<= 3 s cut), CHANGE button ISRs (E2), FSR/knob/MFRC522,
// INMP441 mic (I2S0), MAX98357A speaker (I2S1 + LittleFS WAV + HTTP clip stream),
// OLED + British English IPA glyphs, nest HTTP retries, Wi-Fi/OTA, and CSV loggers.
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <MFRC522.h>
#include <SPI.h>
#include <WiFi.h>
#include <Wire.h>
#include <driver/i2s.h>
#include <esp_system.h>
#include <esp_task_wdt.h>

#ifdef USE_SH1106
#include <Adafruit_SH110X.h>
#define DOLL_WHITE SH110X_WHITE
#else
#include <Adafruit_SSD1306.h>
#define DOLL_WHITE SSD1306_WHITE
#endif

#include "doll_logic.h"
#include "lumpi_net.h"
#include "messages.h"
#include "session.h"
#include "settings.h"

namespace doll_parts {

inline bool mic_ok = false, speaker_ok = false, fs_ok = false, oled_ok = false, rfid_ok = false;
inline MFRC522 rfid(doll_pins::rfid_cs, MFRC522::UNUSED_PIN);

#ifdef USE_SH1106
inline Adafruit_SH1106G oled(128, 64, &Wire, -1);
#else
inline Adafruit_SSD1306 oled(128, 64, &Wire, -1);
#endif

struct AudioBlock { int slot; size_t bytes; bool final_block; };
struct ClipSlice { uint16_t bytes; int16_t samples[256]; };
struct ReadingStats {
    uint32_t attempt = 0, held_ms = 0, chunks = 0, chunk_tries = 0;
    uint32_t finish_ms = 0, grade_ms = 0, release_ms = 0, wrong_words = 0;
};

// ---- 1. Servos (LEDC 50 Hz, 16-bit; hard 3 s cut, toggle switch cuts 5 V) ----------------

inline bool servo_active = false;
inline uint32_t servo_started_ms = 0, first_servo_pulse_ms = 0;

inline void servos_off() {
    ledcWrite(0, 0);
    ledcWrite(1, 0);
    servo_active = false;
}

inline void servos_init() {
    pinMode(doll_pins::arm, OUTPUT);
    digitalWrite(doll_pins::arm, LOW);
    pinMode(doll_pins::tongue, OUTPUT);
    digitalWrite(doll_pins::tongue, LOW);
    ledcSetup(0, 50, 16);
    ledcSetup(1, 50, 16);
    ledcAttachPin(doll_pins::arm, 0);
    ledcAttachPin(doll_pins::tongue, 1);
    servos_off();
}

inline void servos_check_timeout() {
    if (servo_active && millis() - servo_started_ms >= doll_tuning::servo_timeout_ms) {
        servos_off();
    }
}

inline void wait_ms(uint32_t ms) {
    const uint32_t t0 = millis();
    while (millis() - t0 < ms) {
        servos_check_timeout();
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

inline void servo_pulse(int ch, int us) {
    if (!servo_active) {
        servo_started_ms = millis();
        servo_active = true;
    }
    if (first_servo_pulse_ms == 0) {
        first_servo_pulse_ms = millis();
    }
    ledcWrite(ch, static_cast<uint32_t>(constrain(us, 1000, 2000)) * 65535UL / 20000UL);
}

// Each step is {arm_us, tongue_us, hold_ms}; 0 leaves that servo unchanged. Always ends limp.
inline void run_gesture(uint32_t cmd) {
    using namespace doll_tuning;
    struct Move { int arm, tongue; uint32_t ms; };
    const Move praise[] = {{arm_up_us, 0, 700}, {arm_centre_us, 0, 300}};
    const Move attend[] = {{arm_up_us, tongue_a_us, 350}, {0, tongue_b_us, 350}, {arm_centre_us, tongue_centre_us, 300}};
    const Move shush[]  = {{arm_wave_a_us, tongue_a_us, 350}, {arm_wave_b_us, tongue_b_us, 350}, {arm_centre_us, tongue_centre_us, 300}};
    const Move wave[]   = {{arm_wave_a_us, 0, 350}, {arm_wave_b_us, 0, 350}, {arm_wave_a_us, 0, 350}, {arm_centre_us, 0, 300}};

    const Move* seq = nullptr;
    size_t n = 0;
    if (cmd == lumpi::accepted)            { seq = praise; n = 2; }
    else if (cmd == lumpi::retry_word)     { seq = attend; n = 3; }
    else if (cmd == lumpi::too_noisy)      { seq = shush;  n = 3; }
    else if (cmd == lumpi::teacher_coming) { seq = wave;   n = 4; }
    for (size_t i = 0; i < n; ++i) {
        if (seq[i].arm) {
            servo_pulse(0, seq[i].arm);
        }
        if (seq[i].tongue) {
            servo_pulse(1, seq[i].tongue);
        }
        wait_ms(seq[i].ms);
    }
    servos_off();
}

// ---- 2. Buttons by CHANGE interrupt (E2) + 35 ms debouncer -------------------------------

struct ButtonEdge { uint8_t pin, level; uint32_t at_ms; };

struct Debouncer {
    uint8_t stable = HIGH, pending = HIGH;
    bool changing = false;
    uint32_t edge_ms = 0;

    void on_edge(uint8_t lv, uint32_t ms) { pending = lv; edge_ms = ms; changing = true; }

    bool ready(uint32_t now, uint8_t& out) {
        if (!changing || now - edge_ms < 35) {
            return false;
        }
        changing = false;
        if (pending == stable) {
            return false;
        }
        stable = out = pending;
        return true;
    }
};

inline QueueHandle_t button_queue = nullptr;

// static, not inline: an inline function's constants are not placed in IRAM, and the link fails
// ("dangerous relocation: l32r: literal placed after use").
static void IRAM_ATTR push_edge(int pin) {
    const ButtonEdge e = {static_cast<uint8_t>(pin),
                          static_cast<uint8_t>(gpio_get_level(static_cast<gpio_num_t>(pin))),
                          static_cast<uint32_t>(xTaskGetTickCountFromISR() * portTICK_PERIOD_MS)};
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(button_queue, &e, &woken);
    portYIELD_FROM_ISR(woken);
}

static void IRAM_ATTR record_isr() { push_edge(doll_pins::record); }
static void IRAM_ATTR stop_isr() { push_edge(doll_pins::stop); }

inline void buttons_init() {
    pinMode(doll_pins::record, INPUT_PULLUP);
    pinMode(doll_pins::stop, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(doll_pins::record), record_isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(doll_pins::stop), stop_isr, CHANGE);
}

// ---- 3. FSR mat, page knob, and MFRC522 RFID reader --------------------------------------

inline void sensors_init() {
    analogReadResolution(12);
    analogSetPinAttenuation(doll_pins::fsr, ADC_11db);
    analogSetPinAttenuation(doll_pins::page_knob, ADC_11db);
    SPI.begin(doll_pins::rfid_sck, doll_pins::rfid_miso, doll_pins::rfid_mosi, doll_pins::rfid_cs);
    rfid.PCD_Init();
    const byte ver = rfid.PCD_ReadRegister(MFRC522::VersionReg);
    rfid_ok = ver != 0x00 && ver != 0xFF;
}

inline int read_adc(int pin) {
    long sum = 0;
    for (int i = 0; i < 8; ++i) {
        sum += analogRead(pin);
    }
    return static_cast<int>(sum / 8);
}

inline bool read_rfid(char* uid_out, size_t cap) {
    if (!rfid_ok || !rfid.PICC_IsNewCardPresent() || !rfid.PICC_ReadCardSerial()) {
        return false;
    }
    size_t pos = 0;
    for (byte i = 0; i < rfid.uid.size && pos + 2 < cap; ++i) {
        snprintf(uid_out + pos, 3, "%02X", rfid.uid.uidByte[i]);
        pos += 2;
    }
    uid_out[pos] = '\0';
    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
    return pos >= 8;
}

// ---- 4. Mic (INMP441, I2S0) & Speaker (MAX98357A, I2S1 + LittleFS WAV) -------------------

inline void audio_init() {
    i2s_config_t rx = {};
    rx.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX);
    rx.sample_rate = lumpi::sample_rate_hz;
    rx.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    rx.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT;  // L/R on GND (core swaps left/right)
    rx.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    rx.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    rx.dma_buf_count = 8;
    rx.dma_buf_len = 512;
    i2s_pin_config_t rx_pins = {I2S_PIN_NO_CHANGE, doll_pins::mic_sck, doll_pins::mic_ws,
                                I2S_PIN_NO_CHANGE, doll_pins::mic_sd};
    mic_ok = i2s_driver_install(I2S_NUM_0, &rx, 0, nullptr) == ESP_OK &&
             i2s_set_pin(I2S_NUM_0, &rx_pins) == ESP_OK;
    if (mic_ok) {
        REG_SET_BIT(I2S_TIMING_REG(I2S_NUM_0), BIT(9));
        REG_SET_BIT(I2S_CONF_REG(I2S_NUM_0), I2S_RX_MSB_SHIFT);
    }

    fs_ok = LittleFS.begin(false);
    i2s_config_t tx = {};
    tx.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX);
    tx.sample_rate = lumpi::sample_rate_hz;
    tx.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    tx.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    tx.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    tx.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    tx.dma_buf_count = 6;
    tx.dma_buf_len = 256;
    tx.tx_desc_auto_clear = true;
    i2s_pin_config_t tx_pins = {I2S_PIN_NO_CHANGE, doll_pins::speaker_bclk, doll_pins::speaker_lrc,
                                doll_pins::speaker_din, I2S_PIN_NO_CHANGE};
    speaker_ok = i2s_driver_install(I2S_NUM_1, &tx, 0, nullptr) == ESP_OK &&
                 i2s_set_pin(I2S_NUM_1, &tx_pins) == ESP_OK;
}

inline size_t mic_read_pcm16(int16_t* dst, size_t count) {
    if (!mic_ok) {
        memset(dst, 0, count * sizeof(int16_t));
        vTaskDelay(pdMS_TO_TICKS(max<uint32_t>(1, count * 1000UL / lumpi::sample_rate_hz)));
        return count;
    }
    static int32_t raw[256];
    size_t got = 0;
    i2s_read(I2S_NUM_0, raw, min<size_t>(count, 256) * 4, &got, pdMS_TO_TICKS(100));
    const size_t n = got / 4;
    for (size_t i = 0; i < n; ++i) {
        dst[i] = static_cast<int16_t>(constrain(raw[i] >> doll_tuning::mic_shift, -32768, 32767));
    }
    return n;
}

// 250 ms peak using (raw >> 8).
inline uint32_t mic_peak_250ms() {
    if (!mic_ok) {
        vTaskDelay(pdMS_TO_TICKS(250));
        return 0;
    }
    static int32_t raw[256];
    uint32_t peak = 0;
    const uint32_t t0 = millis();
    while (millis() - t0 < 250) {
        size_t got = 0;
        i2s_read(I2S_NUM_0, raw, sizeof(raw), &got, pdMS_TO_TICKS(50));
        for (size_t i = 0; i < got / 4; ++i) {
            const int32_t s = raw[i] >> 8;
            peak = max<uint32_t>(peak, s < 0 ? -s : s);
        }
    }
    return peak;
}

inline void speaker_write_mono(const int16_t* mono, size_t samples) {
    if (!speaker_ok || samples == 0) {
        return;
    }
    int16_t stereo[512];
    for (size_t off = 0; off < samples; off += 256) {
        const size_t n = min<size_t>(256, samples - off);
        for (size_t i = 0; i < n; ++i) {
            stereo[i * 2] = stereo[i * 2 + 1] = mono[off + i];
        }
        size_t sent = 0;
        i2s_write(I2S_NUM_1, stereo, n * 4, &sent, pdMS_TO_TICKS(200));
    }
}

inline void play_wav_file(const char* path) {
    if (!speaker_ok || !fs_ok) {
        return;
    }
    File f = LittleFS.open(path, "r");
    if (!f) {
        return;
    }
    f.seek(12);
    char id[4] = {};
    uint32_t left = 0;
    while (f.read(reinterpret_cast<uint8_t*>(id), 4) == 4 && f.read(reinterpret_cast<uint8_t*>(&left), 4) == 4) {
        if (memcmp(id, "data", 4) == 0) {
            break;
        }
        f.seek(f.position() + left + (left & 1));
        left = 0;
    }
    int16_t buf[256];
    while (left > 0) {
        const size_t got = f.read(reinterpret_cast<uint8_t*>(buf), min<uint32_t>(sizeof(buf), left));
        if (got < 2) {
            break;
        }
        speaker_write_mono(buf, got / 2);
        left -= got;
        esp_task_wdt_reset();
    }
    f.close();
    i2s_zero_dma_buffer(I2S_NUM_1);
}

// ---- 5. OLED (SSD1306 / SH1106) + 6x8 British English IPA glyphs -------------------------

struct IpaGlyph { uint16_t cp; uint8_t rows[8]; };

inline const IpaGlyph ipa_table[] = {
    {0x00E6, {0x00, 0x00, 0xD8, 0x24, 0x7C, 0xA0, 0x6C, 0x00}},  // æ
    {0x00F0, {0x50, 0x20, 0x50, 0x10, 0x70, 0x90, 0x60, 0x00}},  // ð
    {0x014B, {0x00, 0x00, 0xF0, 0x90, 0x90, 0x90, 0x10, 0x20}},  // ŋ
    {0x0250, {0x00, 0x00, 0xB0, 0x88, 0x78, 0x08, 0x70, 0x00}},  // ɐ
    {0x0251, {0x00, 0x00, 0x78, 0x88, 0x88, 0x98, 0x68, 0x00}},  // ɑ
    {0x0252, {0x00, 0x00, 0xB0, 0xC8, 0x88, 0x88, 0x78, 0x00}},  // ɒ
    {0x0254, {0x00, 0x00, 0x70, 0x88, 0x08, 0x88, 0x70, 0x00}},  // ɔ
    {0x0259, {0x00, 0x00, 0x70, 0x08, 0x78, 0x88, 0x70, 0x00}},  // ə
    {0x025A, {0x00, 0x00, 0x70, 0x08, 0x7C, 0x8A, 0x70, 0x00}},  // ɚ
    {0x025B, {0x00, 0x00, 0x70, 0x80, 0x70, 0x80, 0x70, 0x00}},  // ɛ
    {0x025C, {0x00, 0x00, 0x70, 0x08, 0x70, 0x08, 0x70, 0x00}},  // ɜ
    {0x025D, {0x00, 0x00, 0x70, 0x08, 0x74, 0x0A, 0x70, 0x00}},  // ɝ
    {0x0261, {0x00, 0x00, 0x78, 0x88, 0x88, 0x78, 0x08, 0x70}},  // ɡ
    {0x026A, {0x00, 0x00, 0x70, 0x20, 0x20, 0x20, 0x70, 0x00}},  // ɪ
    {0x026B, {0x20, 0x20, 0x20, 0x50, 0x20, 0x20, 0x30, 0x00}},  // ɫ
    {0x0279, {0x00, 0x00, 0x10, 0x10, 0x10, 0x50, 0x20, 0x00}},  // ɹ
    {0x027E, {0x00, 0x00, 0x30, 0x48, 0x40, 0x40, 0x40, 0x00}},  // ɾ
    {0x0283, {0x18, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x60}},  // ʃ
    {0x028A, {0x00, 0x00, 0xD8, 0x88, 0x88, 0x88, 0x70, 0x00}},  // ʊ
    {0x028C, {0x00, 0x00, 0x20, 0x50, 0x50, 0x88, 0x88, 0x00}},  // ʌ
    {0x0292, {0x00, 0x00, 0xF8, 0x10, 0x30, 0x08, 0x88, 0x70}},  // ʒ
    {0x0294, {0x70, 0x88, 0x08, 0x30, 0x20, 0x20, 0x20, 0x00}},  // ʔ
    {0x02C8, {0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00}},  // ˈ
    {0x02CC, {0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x20, 0x20}},  // ˌ
    {0x02D0, {0x00, 0x20, 0x20, 0x00, 0x20, 0x20, 0x00, 0x00}},  // ː
    {0x03B8, {0x70, 0x88, 0x88, 0xF8, 0x88, 0x88, 0x70, 0x00}},  // θ
    {0x1D7B, {0x00, 0x00, 0x70, 0x20, 0xF8, 0x20, 0x70, 0x00}},  // ᵻ
};

inline void oled_init() {
    Wire.begin(doll_pins::oled_sda, doll_pins::oled_scl);
    Wire.beginTransmission(0x3C);
    if (Wire.endTransmission() != 0) {
        return;
    }
#ifdef USE_SH1106
    oled_ok = oled.begin(0x3C, false);
#else
    oled_ok = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false);
#endif
    if (oled_ok) {
        oled.setTextWrap(false);
        oled.setTextColor(DOLL_WHITE);
    }
}

inline uint16_t next_utf8(const char*& p) {
    const uint8_t b0 = static_cast<uint8_t>(*p++);
    if (b0 < 0x80) {
        return b0;
    }
    if ((b0 & 0xE0) == 0xC0 && p[0]) {
        return static_cast<uint16_t>(((b0 & 0x1F) << 6) | (*p++ & 0x3F));
    }
    if ((b0 & 0xF0) == 0xE0 && p[0] && p[1]) {
        const uint8_t b1 = static_cast<uint8_t>(*p++);
        return static_cast<uint16_t>(((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (*p++ & 0x3F));
    }
    return 0;
}

inline void oled_draw_utf8(int x, int y, const char* text) {
    if (!oled_ok || !text) {
        return;
    }
    for (const char* p = text; *p && y <= 56; ) {
        const uint16_t cp = next_utf8(p);
        if (cp == 0) {
            continue;
        }
        if (x > 122) {
            x = 0;
            y += 10;
            if (y > 56) {
                break;
            }
        }
        if (cp >= 0x20 && cp <= 0x7E) {
            oled.drawChar(x, y, static_cast<unsigned char>(cp), DOLL_WHITE, 0, 1);
        } else {
            for (const IpaGlyph& g : ipa_table) {
                if (g.cp == cp) {
                    oled.drawBitmap(x, y, g.rows, 6, 8, DOLL_WHITE);
                    break;
                }
            }
        }
        x += 6;
    }
}

inline void oled_show(const char* l1, const char* l2 = "", const char* l3 = "") {
    if (!oled_ok) {
        return;
    }
    oled.clearDisplay();
    oled_draw_utf8(0, 4, l1);
    oled_draw_utf8(0, 24, l2);
    oled_draw_utf8(0, 44, l3);
    oled.display();
}

inline void oled_show_word(const char* word, const char* ipa, const char* heard) {
    if (!oled_ok) {
        return;
    }
    oled.clearDisplay();
    oled.setTextSize(strlen(word) <= 10 ? 2 : 1);
    oled.setCursor(0, 0);
    oled.print(word);
    oled.setTextSize(1);
    oled_draw_utf8(0, 22, "IPA:");
    oled_draw_utf8(26, 22, ipa);
    oled_draw_utf8(0, 44, "You:");
    oled_draw_utf8(26, 44, heard);
    oled.display();
}

// ---- 6. Nest HTTP helpers, clip stream, Wi-Fi/OTA, CSV -----------------------------------

inline int tell_nest(const char* method, const char* route, const String& extra, String& answer,
                     const uint8_t* body = nullptr, size_t len = 0, uint32_t wait_ms = 5000) {
    const String url = String(LUMPI_GATEWAY_URL) + route + "?node=" + String(LUMPI_NODE_ID) + extra;
    return lumpi_net::request(method, url, body, len, body ? "application/octet-stream" : nullptr,
                              answer, wait_ms);
}

// Returns 1 = "dark", 0 = "ok", -1 = no answer.
inline int send_presence(bool present, uint32_t fsr, bool quiet) {
    String ans;
    const String q = "&present=" + String(present ? 1 : 0) + "&fsr=" + String(fsr) +
                     "&mode=" + String(quiet ? "quiet" : "read");
    return tell_nest("POST", lumpi::route::presence, q, ans, nullptr, 0, 1800) == 200
               ? (ans == "dark" ? 1 : 0)
               : -1;
}

// Sends one 8192-byte chunk with up to 5 tries (1500 ms ACK timeout). 409 aborts immediately.
inline bool send_chunk(uint32_t attempt, uint32_t index, const uint8_t* data, size_t len, uint32_t& tries) {
    String ans;
    const String q = "&attempt=" + String(attempt) + "&index=" + String(index);
    for (int t = 0; t < 5; ++t) {
        tries += 1;
        const int code = tell_nest("PUT", lumpi::route::chunk, q, ans, data, len, 1500);
        if (code == 200 && ans == String(index)) {
            return true;
        }
        if (code == 409) {
            return false;
        }
        esp_task_wdt_reset();
    }
    return false;
}

// Polls GET /v1/attempt every 300 ms (5 s/ask, 50 s cap). Leaves reply = uncertain on 409/timeout.
inline void poll_attempt(uint32_t attempt, doll_logic::AttemptReply& reply) {
    const uint32_t t0 = millis();
    String ans;
    while (millis() - t0 < 50000) {
        esp_task_wdt_reset();
        const int code = tell_nest("GET", lumpi::route::attempt, "&attempt=" + String(attempt), ans);
        if (code == 409) {
            return;
        }
        if (code == 200) {
            const auto parsed = doll_logic::parse_attempt(ans.c_str());
            if (parsed.valid && !parsed.pending) {
                reply = parsed;
                return;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}

// Streams GET /v1/word_clip into clip_queue (512 B slices, no whole-file RAM buffer).
inline void stream_word_clip(uint32_t attempt, uint32_t word_index, QueueHandle_t clip_queue) {
    static WiFiClient plain;
    static WiFiClientSecure secure;  // unused: the nest is plain http
    HTTPClient http;
    const String url = String(LUMPI_GATEWAY_URL) + lumpi::route::word_clip +
                       "?node=" + String(LUMPI_NODE_ID) + "&attempt=" + String(attempt) +
                       "&word=" + String(word_index);
    if (lumpi_net::open(http, plain, secure, url, nullptr, 5000)) {
        if (http.GET() == 200) {
            WiFiClient* s = http.getStreamPtr();
            int left = http.getSize();
            uint8_t hdr[44];
            if (s->readBytes(hdr, sizeof(hdr)) == sizeof(hdr)) {
                if (left >= 44) {
                    left -= 44;
                }
                while (left != 0 && (s->connected() || s->available() > 0)) {
                    ClipSlice slice = {};
                    const size_t want = (left > 0 && left < 512) ? static_cast<size_t>(left) : 512;
                    const size_t got = s->readBytes(reinterpret_cast<uint8_t*>(slice.samples), want);
                    if (got < 2) {
                        break;
                    }
                    slice.bytes = static_cast<uint16_t>(got & ~1U);
                    if (left > 0) {
                        left -= static_cast<int>(got);
                    }
                    xQueueSend(clip_queue, &slice, pdMS_TO_TICKS(1000));
                    esp_task_wdt_reset();
                }
            }
        }
        http.end();
    }
    ClipSlice end = {};
    xQueueSend(clip_queue, &end, pdMS_TO_TICKS(1000));
}

inline uint32_t wifi_drops = 0, wifi_down_ms = 0;
inline bool wifi_was_up = false, ota_started = false;

inline void wifi_maintain(bool idle) {
    if (lumpi_net::connected()) {
        if (!wifi_was_up) {
            wifi_was_up = true;
            wifi_down_ms = 0;
            if (!ota_started) {
                char host[24];
                snprintf(host, sizeof(host), "lumpi-doll-%d", LUMPI_NODE_ID);
                ArduinoOTA.setHostname(host);
                if (LUMPI_LAN_KEY[0] != '\0') {
                    ArduinoOTA.setPassword(LUMPI_LAN_KEY);
                }
                ArduinoOTA.begin();
                ota_started = true;
            }
            Serial.printf("[wifi] connected %s (%d dBm)\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
        }
        if (idle && ota_started) {
            ArduinoOTA.handle();
        }
        return;
    }
    if (wifi_was_up) {
        wifi_was_up = false;
        wifi_drops += 1;
        wifi_down_ms = millis();
        WiFi.reconnect();
    } else if (wifi_down_ms == 0) {
        wifi_down_ms = millis();
    }
    if (idle && millis() - wifi_down_ms >= 60000) {
        ESP.restart();
    }
}

inline void csv_boot() {
    Serial.printf("CSV,boot,%d,%s,%lu\n", LUMPI_NODE_ID, lumpi_net::reset_reason(),
                  static_cast<unsigned long>(ESP.getFreeHeap()));
}

inline void csv_beat() {
    Serial.printf("CSV,beat,%d,%lu,%d,%lu,%lu,%lu\n", LUMPI_NODE_ID,
                  static_cast<unsigned long>(millis() / 1000), lumpi_net::connected() ? WiFi.RSSI() : 0,
                  static_cast<unsigned long>(wifi_drops), static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getMinFreeHeap()));
}

inline void csv_reading(const ReadingStats& s, uint32_t code, uint32_t servo_ms) {
    Serial.printf("CSV,reading,%d,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu\n", LUMPI_NODE_ID,
                  static_cast<unsigned long>(s.attempt), static_cast<unsigned long>(s.held_ms),
                  static_cast<unsigned long>(s.chunks), static_cast<unsigned long>(s.chunk_tries),
                  static_cast<unsigned long>(s.finish_ms), static_cast<unsigned long>(s.grade_ms),
                  static_cast<unsigned long>(code), static_cast<unsigned long>(s.wrong_words),
                  static_cast<unsigned long>(servo_ms));
}

inline void csv_count(const char* key, uint8_t n) {
    Serial.printf("CSV,count,%d,%s,%u\n", LUMPI_NODE_ID, key, static_cast<unsigned>(n));
}

inline void csv_dark(bool dark) {
    Serial.printf("CSV,dark,%d,%d\n", LUMPI_NODE_ID, dark ? 1 : 0);
}

}  // namespace doll_parts
