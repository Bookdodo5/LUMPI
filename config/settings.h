#pragma once
#include <cstdint>
// THE WIRING SHEET. Every pin of every board is here and only here.
// Every bench sketch includes this file, so moving a wire = changing ONE number below.
// Secrets (Wi-Fi, keys, URLs) live in config/private.h, never here.

#if __has_include("private.h")
#include "private.h"
#endif

#ifndef LUMPI_WIFI_SSID
#define LUMPI_WIFI_SSID ""
#endif

#ifndef LUMPI_WIFI_PASSWORD
#define LUMPI_WIFI_PASSWORD ""
#endif

#ifndef LUMPI_ADAPTER_URL
#define LUMPI_ADAPTER_URL ""
#endif

#ifndef LUMPI_LAN_KEY
#define LUMPI_LAN_KEY ""
#endif

#ifndef LUMPI_GATEWAY_URL
#define LUMPI_GATEWAY_URL "http://10.61.117.68"   // the nest, e.g. "http://192.168.1.50" (dolls send here)
#endif

// Root certificate of the cloud site (https only). Empty = encrypted but NOT checked (bench only).
#ifndef LUMPI_CLOUD_CA
#define LUMPI_CLOUD_CA ""
#endif

// Which doll this board is: 1 or 2. Set per board in platformio.ini (-DLUMPI_NODE_ID=2).
#ifndef LUMPI_NODE_ID
#define LUMPI_NODE_ID 1
#endif

// Shared gateway pin map from the LUMPI build guide.
// Gateway code uses only gateway_pins. Doll pins belong in doll_pins later.
namespace gateway_pins {
constexpr int sda = 21;
constexpr int scl = 22;
constexpr int trig = 26;  // VIN side of the board
constexpr int echo = 27;  // through the 1k/2k divider
constexpr int ack = 32;   // needs the internal pull-up, so not 34-39
}

// Doll pin map from the LUMPI build guide (reference/pins). Doll code uses only doll_pins.
// The same GPIO number means a different wire on the gateway: never mix the two.
namespace doll_pins {
constexpr int mic_sck = 26, mic_ws = 25, mic_sd = 34;  // INMP441, I2S port 0
constexpr int fsr = 35;                                // FSR mat, 10k to GND
constexpr int rfid_sck = 18, rfid_miso = 19, rfid_mosi = 23, rfid_cs = 27;
constexpr int record = 13, stop = 14;                  // INPUT_PULLUP; RECORD is the E2 interrupt
constexpr int page_knob = 36;                          // potentiometer wiper, ADC1; ends to 3.3V and GND
constexpr int oled_sda = 32, oled_scl = 33;            // the doll's own I2C bus
constexpr int speaker_bclk = 16, speaker_lrc = 17, speaker_din = 5;  // MAX98357A, I2S port 1
constexpr int arm = 21, tongue = 22;                   // servo signals (orange wires)
}

// Doll calibration knobs. Measure with the bench tests (s1b_pressure, s1g_servos) and change here.
namespace doll_tuning {
constexpr int book_on_fsr = 1000;          // FSR above this = a book is on the mat. Measured 2026-11-23 (1k resistor): empty 0, book 1800-2400
constexpr int mic_shift = 14;              // NEW: INMP441 32-bit sample >> mic_shift -> 16-bit PCM. Lower = louder.
constexpr int knob_step = 4095 / 20;       // NEW: ADC counts per page step (about 1/20 of a turn)
constexpr uint32_t lift_grace_ms = 30000;  // NEW: lift-offs shorter than 30 s keep the session and mistake counts
constexpr uint32_t proposal_timeout_ms = 30000; // NEW: how long "Call your teacher? Hold STOP" stays shown
constexpr uint32_t stop_hold_ms = 2000;    // NEW: STOP hold length that calls the teacher
constexpr uint32_t min_record_ms = 500;    // NEW: RECORD holds shorter than 0.5 s cancel
constexpr uint32_t max_record_ms = 10000;  // NEW: longest single recording (10 s)
constexpr uint32_t servo_timeout_ms = 3000; // NEW: hard limit; servo pulses cut to duty 0 after at most 3 s
constexpr int arm_centre_us = 1500;        // NEW: calibrate with s1g_servos
constexpr int arm_up_us = 1650;            // NEW: praise / wave peak
constexpr int arm_wave_a_us = 1350;        // NEW: wave left
constexpr int arm_wave_b_us = 1650;        // NEW: wave right
constexpr int tongue_centre_us = 1500;     // NEW: calibrate with s1g_servos
constexpr int tongue_a_us = 1380;          // NEW: tongue move A
constexpr int tongue_b_us = 1620;          // NEW: tongue move B
}

namespace room {
constexpr float dark_lux = 10.0f;          // dark below this for 3 s (5 lx hysteresis below bright_lux)
constexpr float bright_lux = 15.0f;        // NEW: bright again at or above this for 3 s (owner-set 15 lx)
constexpr uint32_t dark_hold_ms = 3000;    // NEW: dark/bright state changes only after holding for 3 s
constexpr float teacher_margin_cm = 40.0f;
}
