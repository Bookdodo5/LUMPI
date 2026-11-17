#pragma once

#if __has_include("private.h")
#include "private.h"
#endif

#ifndef LUMPI_WIFI_SSID
#define LUMPI_WIFI_SSID ""
#endif

#ifndef LUMPI_WIFI_PASSWORD
#define LUMPI_WIFI_PASSWORD ""
#endif

#ifndef LUMPI_MQTT_URI
#define LUMPI_MQTT_URI ""
#endif

#ifndef LUMPI_MQTT_USER
#define LUMPI_MQTT_USER ""
#endif

#ifndef LUMPI_MQTT_PASSWORD
#define LUMPI_MQTT_PASSWORD ""
#endif

#ifndef LUMPI_MQTT_CA
#define LUMPI_MQTT_CA ""
#endif

#ifndef LUMPI_ADAPTER_URL
#define LUMPI_ADAPTER_URL ""
#endif

#ifndef LUMPI_LAN_KEY
#define LUMPI_LAN_KEY ""
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
constexpr int record = 13, stop = 14;                  // INPUT_PULLUP
constexpr int previous = 36, next = 39;                // external 10k pull-ups
constexpr int oled_sda = 32, oled_scl = 33;            // the doll's own I2C bus
constexpr int pir = 15;
constexpr int speaker_bclk = 16, speaker_lrc = 17, speaker_din = 5;  // MAX98357A, I2S port 1
constexpr int arm = 21, tongue = 22;                   // servo signals, through 1k
constexpr int servo_power = 4;                         // D4184 PWM input, 10k pull-down
}

namespace room {
constexpr float dark_lux = 100.0f;
constexpr float teacher_margin_cm = 20.0f;
}
