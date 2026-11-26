// FULL WIRING CHECK: THE NEST (GATEWAY). Tests every part with ALL of them wired.
//
// Upload environment: s4_nest_check  (s4_nest_check_sh1106 if your OLED is an SH1106).
// Then open the monitor so you can type answers:
//   pio device monitor -b 115200 --echo --eol LF --filter send_on_enter
//
// It runs top to bottom, about 1 minute. Some steps ask you to act (cover the light
// sensor, hold your hand in front of the distance sensor, press ACK) or to answer y/n.
// Type s + Enter to skip a step. A summary of every part is printed at the end.
// Then LIVE DATA every 0.5 s: light (lux + DARK/bright at room::dark_lux),
// distance in cm, and ACK presses since the last row.
//
// Wiring (guide: https://lumpi-build-guide.vercel.app/reference/pins):
//   BH1750 light + OLED  SDA 21, SCL 22, VCC 3.3 V, BH1750 ADDR to GND
//   HC-SR04 distance     TRIG 26, ECHO -> 1k -> GPIO 27 -> 2k -> GND, VCC 5 V
//   ACK button           GPIO 32 -> button -> GND
// Named here so PlatformIO links them; check_tools.h (src/drivers/) uses them.
#include <WiFi.h>
#include <Wire.h>
#ifdef USE_SH1106
#include <Adafruit_SH110X.h>
#else
#include <Adafruit_SSD1306.h>
#endif
#include "check_tools.h"
#include "monitor.h"

using namespace check;

// ---------------------------------------------------------------------------
// BH1750 light sensor (I2C address 0x23)
// ---------------------------------------------------------------------------
bool read_lux(float& lux) {
    if (Wire.requestFrom(uint8_t(0x23), uint8_t(2)) != 2) return false;
    const int high = Wire.read(), low = Wire.read();  // high byte first
    lux = ((high << 8) | low) / 1.2f;                  // datasheet: 1.2 counts per lux
    return true;
}

void check_light() {
    Serial.println("\n== BH1750 light sensor ==");
    Wire.beginTransmission(0x23);
    Wire.write(0x10);  // command: measure continuously, high resolution
    if (Wire.endTransmission() != 0) {
        fail("BH1750 reading", "not tested: no answer at 0x23 (see the I2C lines above)");
        return;
    }
    delay(200);  // the first measurement takes up to 180 ms
    float bright = 0;
    if (!read_lux(bright)) {
        fail("BH1750 reading", "answers its address but sends no reading: loose SDA/SCL contact?");
        return;
    }
    Serial.printf("   Light now: %.0f lux\n", bright);
    if (bright < 10) {
        fail("BH1750 reading", "under 10 lux: turn on the room light (or the sensor is covered) and run again");
        return;
    }
    if (!ready("Cover the light sensor completely with your hand")) {
        skip("BH1750 reading", String(bright, 0) + " lux, cover test skipped");
        return;
    }
    float covered = bright;
    if (wait_until([&] { return read_lux(covered) && covered < bright * 0.3f; }, 10)) {
        pass("BH1750 reading", String(bright, 0) + " lux open, " + String(covered, 0) + " lux covered");
    } else {
        fail("BH1750 reading", String("still ") + String(covered, 0) + " lux when covered: is the sensor window facing out, not taped?");
    }
}

// ---------------------------------------------------------------------------
// HC-SR04 distance sensor
// ---------------------------------------------------------------------------
float distance_cm() {  // -1 = no echo within 30 ms (nothing closer than about 5 m)
    digitalWrite(gateway_pins::trig, LOW);
    delayMicroseconds(2);
    digitalWrite(gateway_pins::trig, HIGH);  // a 10 us pulse starts one measurement
    delayMicroseconds(10);
    digitalWrite(gateway_pins::trig, LOW);
    const unsigned long echo_us = pulseIn(gateway_pins::echo, HIGH, 30000);
    return echo_us ? echo_us * 0.0343f / 2 : -1;  // sound: 0.0343 cm/us, there and back
}

void check_distance() {
    Serial.println("\n== HC-SR04 distance sensor (TRIG 26, ECHO 27) ==");
    pinMode(gateway_pins::trig, OUTPUT);
    digitalWrite(gateway_pins::trig, LOW);
    pinMode(gateway_pins::echo, INPUT);
    delay(60);
    if (digitalRead(gateway_pins::echo) == HIGH) {
        fail("HC-SR04 echo", "ECHO is HIGH while idle: check the 1k/2k divider (2k must go to GND) and the GND wire");
        return;
    }
    int echoes = 0;
    for (int i = 0; i < 5; ++i) {
        const float d = distance_cm();
        Serial.printf("   try %d: %s\n", i + 1, d < 0 ? "no echo" : (String(d, 1) + " cm").c_str());
        echoes += d > 0;
        delay(60);  // let the last echo die away
    }
    if (echoes == 0) {
        fail("HC-SR04 echo", "no echo in 5 tries: VCC must be 5 V (not 3.3 V), TRIG on 26, ECHO through the divider to 27");
        return;
    }
    pass("HC-SR04 echo", String(echoes) + " of 5 tries got an echo");

    if (!ready("Hold your hand flat about 20 cm in front of the sensor")) {
        skip("HC-SR04 distance", "hand test skipped");
        return;
    }
    float last = -1;
    if (wait_until([&] { last = distance_cm(); delay(60); return last > 10 && last < 35; }, 10)) {
        pass("HC-SR04 distance", String(last, 1) + " cm with a hand at about 20 cm");
    } else {
        fail("HC-SR04 distance", String("last reading ") + (last < 0 ? String("no echo") : String(last, 1) + " cm") +
                                     ", expected 10-35 cm: is the sensor pointing at your hand, eyes uncovered?");
    }
}

// ---------------------------------------------------------------------------
// ACK button (teacher's "I'm coming" button)
// ---------------------------------------------------------------------------
void check_ack() {
    Serial.println("\n== ACK button (GPIO 32) ==");
    pinMode(gateway_pins::ack, INPUT_PULLUP);  // released = HIGH, pressed = LOW
    delay(5);
    if (digitalRead(gateway_pins::ack) == LOW) {
        fail("ACK button", "reads PRESSED while untouched: shorted to GND, or wired across 2 legs that are always joined (use diagonal legs)");
        return;
    }
    Serial.println("   Press the ACK button now (10 s).");
    unsigned long low_since = 0;  // debounce: count it only after 30 ms of LOW
    const bool pressed = wait_until([&] {
        if (digitalRead(gateway_pins::ack) == HIGH) {
            low_since = 0;
            return false;
        }
        if (low_since == 0) low_since = millis();
        return millis() - low_since >= 30;
    }, 10);
    if (pressed) {
        pass("ACK button", "press seen on GPIO 32");
    } else {
        fail("ACK button", "no press seen: check the wire to GPIO 32 and that the other leg goes to GND");
    }
}

void setup() {
    Serial.begin(115200);
    delay(1500);  // time to open the monitor
    monitor::title("STAGE 4: FULL NEST CHECK", "every nest part, with all of them wired at once.",
                   "the SUMMARY at the end has no FAIL lines.");
    monitor::section("WIRING (config/settings.h, gateway_pins)");
    monitor::item("I2C (light, OLED)", "SDA GPIO%d, SCL GPIO%d, VCC 3.3V", gateway_pins::sda, gateway_pins::scl);
    monitor::item("BH1750 ADDR", "GND (address 0x23)");
    monitor::item("HC-SR04 TRIG", "GPIO%d, VCC 5V", gateway_pins::trig);
    monitor::item("HC-SR04 ECHO", "1k -> GPIO%d -> 2k -> GND", gateway_pins::echo);
    monitor::item("ACK button", "GPIO%d -> button -> GND", gateway_pins::ack);
    check_boot_power();
    check_wifi_radio();
    const Device devices[] = {
        {0x23, "BH1750 light", true},
        {0x3C, "OLED", true},
        {0x76, "BME280 (optional)", false},
    };
    check_i2c_bus(gateway_pins::sda, gateway_pins::scl, devices);
    check_light();
    check_screen("NEST");
    check_distance();
    check_ack();
    print_summary("NEST");
    // From here on: live data, one line every 0.5 s. "--" = no reading.
    // Save it to a file with:  pio device monitor -b 115200 --filter log2file
    monitor::section("LIVE DATA (a row every 0.5 s; write down values you need for calibration)");
    Serial.printf("  light: DARK below %.0f lux (room::dark_lux). distance: no echo = nothing in range.\n",
                  room::dark_lux);
    Serial.println("  ACK: presses since the last row. -- = no reading.");
}

// The checks ran once in setup(). loop() keeps gathering sensor data.
void loop() {
    static unsigned long last_line = 0, low_since = 0;
    static int presses = 0;  // ACK presses since the last line
    static bool held = false;

    // ACK is polled every 10 ms so a short press between two lines is not missed.
    if (digitalRead(gateway_pins::ack) == LOW) {
        if (low_since == 0) low_since = millis();
        if (!held && millis() - low_since >= 30) {  // 30 ms debounce
            held = true;
            presses++;
        }
    } else {
        low_since = 0;
        held = false;
    }

    if (millis() - last_line >= 500) {
        static int line_number = 0;
        last_line = millis();
        if (line_number++ % 20 == 0) {  // repeat the column titles every 10 s
            Serial.printf("\n%8s | %-24s | %-13s | %s\n", "time", "light (lux)", "distance (cm)", "ACK");
            Serial.println("--------" "-+-" "------------------------" "-+-" "-------------" "-+-" "---------");
        }
        float lux = 0;
        const bool lit = read_lux(lux);
        const String light = lit ? String(lux, 0) : String("--");
        const char* verdict = !lit ? "      " : lux < room::dark_lux ? "DARK  " : "bright";
        const float cm = distance_cm();
        const String distance = cm < 0 ? String("no echo") : String(cm, 1);
        const String ack = presses ? "PRESSED x" + String(presses) : String("-");
        Serial.printf("%7.1fs | %6s %s %s | %13s | %s\n", last_line / 1000.0f, light.c_str(),
                      bar(int(lux), 1000).c_str(), verdict, distance.c_str(), ack.c_str());
        presses = 0;
    }
    delay(10);
}
