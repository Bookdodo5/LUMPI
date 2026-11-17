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
constexpr int trig = 18;
constexpr int echo = 19;
constexpr int ack = 23;
}

namespace room {
constexpr float dark_lux = 100.0f;
constexpr float teacher_margin_cm = 20.0f;
}
