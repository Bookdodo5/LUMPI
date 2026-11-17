// FULL WIRING CHECK: ONE DOLL. Tests every part with ALL of them wired.
//
// Upload environment: doll_check  (doll_check_sh1106 if your OLED is an SH1106).
// Then open the monitor so you can type answers:
//   pio device monitor -b 115200 --echo --eol LF --filter send_on_enter
//
// It runs top to bottom, about 2 minutes. Steps ask you to act (press each button,
// put the book down, show a tag, clap, wave) or to answer y/n about what you see
// and hear. Type s + Enter to skip a step. A summary is printed at the end.
// The PIR is tested last because it needs 60 s to warm up after power-on.
//
// Wiring (guide: https://lumpi-build-guide.vercel.app/reference/pins), all in
// config/settings.h, namespace doll_pins:
//   mic SCK 26, WS 25, SD 34, L/R GND   FSR 35 (+10k to GND)   RC522 18/19/23, SS 27
//   RECORD 13, STOP 14, PREVIOUS 36, NEXT 39 (36/39 need 10k pull-ups)   OLED SDA 32, SCL 33
//   PIR OUT 15   speaker BCLK 16, LRC 17, DIN 5   arm 21, tongue 22   D4184 power 4
#include <SPI.h>
#include <MFRC522.h>
#include <driver/i2s.h>
#include "check_tools.h"

using namespace check;

// ---------------------------------------------------------------------------
// FIRST, before anything else: keep the servos switched off.
// ---------------------------------------------------------------------------
bool servo_pull_down_found = false;

void servos_off_first() {
    // Test the 10k pull-down on GPIO 4 before we drive the pin. Switch on the ESP32's
    // WEAK internal pull-UP: a real 10k pull-down is stronger, so the pin still reads
    // LOW. HIGH means the pull-down is missing, so the servos may twitch at boot.
    pinMode(doll_pins::servo_power, INPUT_PULLUP);
    delay(2);
    servo_pull_down_found = digitalRead(doll_pins::servo_power) == LOW;
    pinMode(doll_pins::servo_power, OUTPUT);
    digitalWrite(doll_pins::servo_power, LOW);  // servo power OFF
    pinMode(doll_pins::arm, OUTPUT);
    digitalWrite(doll_pins::arm, LOW);           // no servo pulses
    pinMode(doll_pins::tongue, OUTPUT);
    digitalWrite(doll_pins::tongue, LOW);
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------
struct Button {
    int pin;
    const char* name;
    bool external_pull_up;  // GPIO 36/39 have no internal pull-up
};
const Button buttons[] = {
    {doll_pins::record, "RECORD button", false},
    {doll_pins::stop, "STOP button", false},
    {doll_pins::previous, "PREVIOUS button", true},
    {doll_pins::next, "NEXT button", true},
};

// Wait until any button is held LOW for 30 ms, then released.
// Returns its index in buttons[], or -1 if nobody pressed within the time.
int wait_any_button(int seconds) {
    unsigned long low_since[4] = {0, 0, 0, 0};
    int pressed = -1;
    wait_until([&] {
        for (int i = 0; i < 4; ++i) {
            if (digitalRead(buttons[i].pin) == HIGH) {
                low_since[i] = 0;
            } else if (low_since[i] == 0) {
                low_since[i] = millis();
            } else if (millis() - low_since[i] >= 30) {
                pressed = i;
                return true;
            }
        }
        return false;
    }, seconds);
    if (pressed >= 0) {
        wait_until([&] { return digitalRead(buttons[pressed].pin) == HIGH; }, 5);  // wait for release
    }
    return pressed;
}

void check_buttons() {
    Serial.println("\n== Buttons ==");
    bool idle_ok[4];
    for (int i = 0; i < 4; ++i) {
        pinMode(buttons[i].pin, buttons[i].external_pull_up ? INPUT : INPUT_PULLUP);
    }
    delay(5);
    for (int i = 0; i < 4; ++i) {
        idle_ok[i] = digitalRead(buttons[i].pin) == HIGH;  // released = HIGH
        if (!idle_ok[i]) {
            fail(buttons[i].name, buttons[i].external_pull_up
                                      ? "reads PRESSED while untouched: the 10k pull-up (GPIO to 3.3 V) is missing, or the button is shorted"
                                      : "reads PRESSED while untouched: shorted to GND, or wired across 2 always-joined legs (use diagonal legs)");
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (!idle_ok[i]) continue;
        Serial.printf("   Press %s now (10 s).\n", buttons[i].name);
        const int got = wait_any_button(10);
        if (got == i) {
            pass(buttons[i].name, String("works on GPIO ") + buttons[i].pin);
        } else if (got >= 0) {
            fail(buttons[i].name, String("GPIO ") + buttons[got].pin + " reacted, not GPIO " + buttons[i].pin +
                                      ": this button's wire is on the wrong pin (swapped?)");
        } else {
            fail(buttons[i].name, String("no press seen on GPIO ") + buttons[i].pin + ": check the wire, and that the other leg goes to GND");
        }
    }
}

// ---------------------------------------------------------------------------
// FSR pressure mat (analog, 0..4095)
// ---------------------------------------------------------------------------
int read_fsr() {
    long sum = 0;
    for (int i = 0; i < 16; ++i) sum += analogRead(doll_pins::fsr);  // average away noise
    return sum / 16;
}

void check_fsr() {
    Serial.println("\n== FSR pressure mat (GPIO 35) ==");
    analogReadResolution(12);
    analogSetPinAttenuation(doll_pins::fsr, ADC_11db);  // measure the full 0..3.3 V
    if (!ready("Take everything off the mat")) {
        skip("FSR mat", "skipped");
        return;
    }
    const int empty = read_fsr();
    Serial.printf("   Empty mat: %d\n", empty);
    if (empty > 3000) {
        fail("FSR mat", String("reads ") + empty + " with nothing on it: the 10k from GPIO 35 to GND is missing, or the FSR is shorted");
        return;
    }
    Serial.println("   Now put the book on the mat (15 s).");
    int with_book = empty;
    if (wait_until([&] { with_book = read_fsr(); return with_book > empty + 800; }, 15)) {
        pass("FSR mat", String("empty ") + empty + ", with book " + with_book + " (write both down for calibration)");
    } else {
        fail("FSR mat", String("only reached ") + with_book + ": is the FSR under the book's weight? Is GPIO 35 at the FSR/10k junction?");
    }
}

// ---------------------------------------------------------------------------
// RC522 RFID reader (SPI)
// ---------------------------------------------------------------------------
MFRC522 reader(doll_pins::rfid_cs, MFRC522::UNUSED_PIN);  // RST is tied to 3.3 V

void check_rfid() {
    Serial.println("\n== RC522 RFID reader (SCK 18, MISO 19, MOSI 23, SS 27) ==");
    SPI.begin(doll_pins::rfid_sck, doll_pins::rfid_miso, doll_pins::rfid_mosi, doll_pins::rfid_cs);
    reader.PCD_Init();
    const byte version = reader.PCD_ReadRegister(MFRC522::VersionReg);
    char text[8];
    snprintf(text, sizeof(text), "0x%02X", version);
    if (version == 0x00 || version == 0xFF) {
        fail("RC522 chip", String("version ") + text + " = no answer: check 3.3 V (NEVER 5 V), GND, RST to 3.3 V, and the 4 SPI wires");
        return;
    }
    pass("RC522 chip", String("answers, version ") + text + " (0x91/0x92 genuine, others are clones: fine)");
    Serial.println("   Hold a book tag 1-3 cm above the reader (15 s).");
    if (!wait_until([&] { return reader.PICC_IsNewCardPresent() && reader.PICC_ReadCardSerial(); }, 15)) {
        fail("RC522 tag", "chip answers but read no tag: hold it flat and closer; is it a 13.56 MHz (MIFARE) tag?");
        return;
    }
    String uid;
    for (byte i = 0; i < reader.uid.size; ++i) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", reader.uid.uidByte[i]);
        uid += hex;
    }
    reader.PICC_HaltA();
    pass("RC522 tag", "read UID " + uid + " (save it for config/books.json)");
}

// ---------------------------------------------------------------------------
// INMP441 microphone (I2S port 0, receive)
// ---------------------------------------------------------------------------
struct Level {
    int peak = 0;          // loudest sample, 16-bit scale (0..32767)
    int lowest = 32767;    // smallest and largest sample: equal means "stuck"
    int highest = -32768;
};

Level listen() {  // about 100 ms of sound
    Level level;
    int32_t raw[256];
    for (int block = 0; block < 6; ++block) {
        size_t bytes = 0;
        i2s_read(I2S_NUM_0, raw, sizeof(raw), &bytes, pdMS_TO_TICKS(200));
        for (size_t i = 0; i < bytes / 4; ++i) {
            const int sample = raw[i] >> 16;  // the sound is in the top 16 of 32 bits
            level.peak = max(level.peak, abs(sample));
            level.lowest = min(level.lowest, sample);
            level.highest = max(level.highest, sample);
        }
    }
    return level;
}

bool microphone_ready = false;  // stays installed after the check, for the live data

void check_microphone() {
    Serial.println("\n== INMP441 microphone (SCK 26, WS 25, SD 34) ==");
    i2s_config_t config{};
    config.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX);
    config.sample_rate = 16000;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;  // L/R tied to GND = left slot
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
        fail("Microphone data", "the I2S driver did not start");
        return;
    }
    microphone_ready = true;
    listen();  // throw away the first 100 ms (start-up)
    if (!ready("Stay quiet for a moment")) {
        skip("Microphone data", "skipped");
        return;
    }
    const Level quiet = listen();
    Serial.printf("   Quiet level: %d\n", quiet.peak);
    if (quiet.lowest == quiet.highest) {
        fail("Microphone data", String("every sample is ") + quiet.lowest + ": the mic sends nothing. Check VDD 3.3 V, GND, L/R to GND, SD on 34, SCK 26, WS 25");
    } else if (quiet.peak > 20000) {
        fail("Microphone data", "random full-scale data in a quiet room: SD wire loose, or L/R not tied to GND");
    } else {
        pass("Microphone data", String("quiet level ") + quiet.peak);
        const int needed = max(quiet.peak * 4, 2000);
        Serial.println("   Now clap once near the doll (10 s).");
        Level loud;
        if (wait_until([&] { loud = listen(); return loud.peak > needed; }, 10)) {
            pass("Microphone clap", String("quiet ") + quiet.peak + ", clap " + loud.peak);
        } else {
            fail("Microphone clap", String("never above ") + needed + ": is the mic's sound hole blocked? Check WS 25 and SCK 26");
        }
    }
}

// ---------------------------------------------------------------------------
// MAX98357A amplifier + speaker (I2S port 1, transmit)
// ---------------------------------------------------------------------------
void check_speaker() {
    Serial.println("\n== Speaker (BCLK 16, LRC 17, DIN 5) ==");
    i2s_config_t config{};
    config.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX);
    config.sample_rate = 16000;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 4;
    config.dma_buf_len = 256;
    config.tx_desc_auto_clear = true;  // silence, not noise, when we stop sending
    i2s_pin_config_t pins{};
    pins.mck_io_num = I2S_PIN_NO_CHANGE;
    pins.bck_io_num = doll_pins::speaker_bclk;
    pins.ws_io_num = doll_pins::speaker_lrc;
    pins.data_out_num = doll_pins::speaker_din;
    pins.data_in_num = I2S_PIN_NO_CHANGE;
    if (i2s_driver_install(I2S_NUM_1, &config, 0, nullptr) != ESP_OK || i2s_set_pin(I2S_NUM_1, &pins) != ESP_OK) {
        fail("Speaker", "the I2S driver did not start");
        return;
    }
    if (!ready("Listen to the doll: it will beep for 1 second")) {
        skip("Speaker", "skipped");
        i2s_driver_uninstall(I2S_NUM_1);
        return;
    }
    int16_t samples[512];  // 256 stereo frames: left, right, left, right ...
    for (int start = 0; start < 16000; start += 256) {
        for (int i = 0; i < 256; ++i) {
            const int16_t value = 3000 * sinf(2 * PI * 440 * (start + i) / 16000.0f);  // 440 Hz
            samples[i * 2] = value;
            samples[i * 2 + 1] = value;
        }
        size_t sent = 0;
        i2s_write(I2S_NUM_1, samples, sizeof(samples), &sent, pdMS_TO_TICKS(500));
    }
    i2s_zero_dma_buffer(I2S_NUM_1);
    if (ask_yes("Did you hear a clear 1-second beep?")) {
        pass("Speaker", "beep heard");
    } else {
        fail("Speaker", "silent: VIN 5 V and GND (ESP32 GND, not servo LOAD), BCLK 16, LRC 17, DIN 5, both speaker wires on SPK+/SPK-");
    }
    i2s_driver_uninstall(I2S_NUM_1);
}

// ---------------------------------------------------------------------------
// Servos + D4184 power switch
// ---------------------------------------------------------------------------
void set_pulse(int channel, int microseconds) {
    // 50 Hz = one pulse every 20000 us. 16-bit PWM: 65535 = always on.
    ledcWrite(channel, microseconds * 65535L / 20000);
}

void check_servos() {
    Serial.println("\n== Servos + D4184 power switch (arm 21, tongue 22, power 4) ==");
    if (servo_pull_down_found) {
        pass("Servo pull-down", "GPIO 4 is held LOW at boot");
    } else {
        fail("Servo pull-down", "GPIO 4 floats at boot: add the 10k from GPIO 4 to GND, or servos may twitch at power-on");
    }
    if (!ready("First test? Take the horns off. Switch the servo charger ON")) {
        skip("Servos", "skipped");
        return;
    }
    pinMode(doll_pins::stop, INPUT_PULLUP);
    ledcSetup(0, 50, 16);  // channel 0 = arm: 50 Hz, 16-bit
    ledcSetup(1, 50, 16);  // channel 1 = tongue
    ledcAttachPin(doll_pins::arm, 0);
    ledcAttachPin(doll_pins::tongue, 1);
    set_pulse(0, 1500);  // send "middle" BEFORE power, so they don't jump somewhere random
    set_pulse(1, 1500);
    digitalWrite(doll_pins::servo_power, HIGH);
    // 6 moves x 400 ms = 2.4 s, under LUMPI's 3 s servo power limit. STOP cuts power.
    const int moves[][2] = {{0, 1350}, {0, 1650}, {0, 1500}, {1, 1380}, {1, 1620}, {1, 1500}};
    bool stopped = false;
    for (const auto& move : moves) {
        set_pulse(move[0], move[1]);
        Serial.printf("   %s -> %d us\n", move[0] == 0 ? "arm" : "tongue", move[1]);
        for (int t = 0; t < 40 && !stopped; ++t) {
            stopped = digitalRead(doll_pins::stop) == LOW;
            delay(10);
        }
        if (stopped) break;
    }
    digitalWrite(doll_pins::servo_power, LOW);  // power off, then stop the pulses
    ledcWrite(0, 0);
    ledcWrite(1, 0);
    if (stopped) {
        skip("Servos", "STOP pressed: power was cut at once (the safety stop works). Reset to test again");
        return;
    }
    const char* power_hint = "charger switch ON? 5 V between D4184 + and LOAD when on? red to +, brown to LOAD, all GNDs joined. Board reset = power dip";
    if (ask_yes("Did the ARM move one way, the other way, then back to the middle?")) {
        pass("Arm servo", "moved");
    } else {
        fail("Arm servo", String("did not move. Signal: 1k into GPIO 21. Power: ") + power_hint);
    }
    if (ask_yes("Did the TONGUE move the same way?")) {
        pass("Tongue servo", "moved");
    } else {
        fail("Tongue servo", String("did not move. Signal: 1k into GPIO 22. Power: ") + power_hint);
    }
}

// ---------------------------------------------------------------------------
// HC-SR501 PIR motion sensor
// ---------------------------------------------------------------------------
void check_pir() {
    Serial.println("\n== PIR motion sensor (OUT on GPIO 15) ==");
    pinMode(doll_pins::pir, INPUT);
    while (millis() < 60000) {  // the PIR's output means nothing for about 60 s after power-on
        Serial.printf("   PIR warming up: %lu s left\n", (60000 - millis()) / 1000);
        delay(min(10000UL, 60000 - millis()));
    }
    Serial.println("   Stay still, out of its view, until OUT goes LOW (30 s).");
    if (!wait_until([] { return digitalRead(doll_pins::pir) == LOW; }, 30)) {
        fail("PIR", "OUT stays HIGH: turn the time knob fully anticlockwise (shortest hold); is anyone moving in view?");
        return;
    }
    if (!ready("Now wave your hand in front of the PIR")) {
        skip("PIR", "wave test skipped");
        return;
    }
    if (wait_until([] { return digitalRead(doll_pins::pir) == HIGH; }, 15)) {
        pass("PIR", "saw your hand (OUT went HIGH)");
    } else {
        fail("PIR", "OUT never went HIGH: VCC must be 5 V (not 3.3 V), GND, OUT on GPIO 15; try the sensitivity knob");
    }
}

void setup() {
    servos_off_first();  // before the 1.5 s wait: the motors must never move by accident
    Serial.begin(115200);
    delay(1500);  // time to open the monitor
    Serial.println("\n\n########## LUMPI DOLL: FULL WIRING CHECK ##########");
    check_boot_power();
    check_wifi_radio();
    check_buttons();
    check_fsr();
    check_rfid();
    check_microphone();
    const Device devices[] = {{0x3C, "OLED", true}};
    check_i2c_bus(doll_pins::oled_sda, doll_pins::oled_scl, devices);
    check_screen("DOLL");
    check_speaker();
    check_servos();
    check_pir();
    print_summary("DOLL");
    // From here on: live data, one line every 0.5 s. "--" = no reading.
    // Save it to a file with:  pio device monitor -b 115200 --filter log2file
    Serial.println("\nLIVE DATA, every 0.5 s. Write down the values you need for calibration.");
}

// The checks ran once in setup(). loop() keeps gathering sensor data.
void loop() {
    static unsigned long last_line = 0;
    if (millis() - last_line < 500) {
        delay(10);
        return;
    }
    last_line = millis();
    static int line_number = 0;
    if (line_number++ % 20 == 0) {  // repeat the column titles every 10 s
        Serial.printf("\n%8s | %-15s | %-16s | %-6s | %s\n", "time", "mat (0-4095)", "sound (peak)", "PIR", "buttons held");
        Serial.println("--------" "-+-" "---------------" "-+-" "----------------" "-+-" "------" "-+-" "------------");
    }

    // A tag only shows up once per placement (the reader puts it to sleep after reading).
    if (reader.PICC_IsNewCardPresent() && reader.PICC_ReadCardSerial()) {
        Serial.print("         >> TAG READ: ");
        for (byte i = 0; i < reader.uid.size; ++i) Serial.printf("%02X", reader.uid.uidByte[i]);
        Serial.println();
        reader.PICC_HaltA();
    }

    const int fsr = read_fsr();
    const int mic = microphone_ready ? listen().peak : 0;  // loudest sample in about 100 ms
    const String sound = microphone_ready ? String(mic) : String("--");
    // Buttons connect their pin to GND, so LOW = held down.
    String held;
    for (const Button& button : buttons) {
        if (digitalRead(button.pin) == LOW) {
            String name = button.name;
            held += name.substring(0, name.indexOf(' ')) + " ";  // "RECORD button" -> "RECORD"
        }
    }
    Serial.printf("%7.1fs | %4d %s | %5s %s | %-6s | %s\n", last_line / 1000.0f, fsr, bar(fsr, 4095).c_str(),
                  sound.c_str(), bar(mic, 10000).c_str(), digitalRead(doll_pins::pir) ? "MOTION" : "still",
                  held.length() ? held.c_str() : "-");
}
