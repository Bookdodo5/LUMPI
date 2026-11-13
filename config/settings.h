#pragma once

// Shared gateway pin map from the LUMPI build guide.
// Gateway code uses only gateway_pins. Doll pins belong in doll_pins later.
namespace gateway_pins {
constexpr int sda = 21;
constexpr int scl = 22;
constexpr int trig = 18;
constexpr int echo = 19;
constexpr int ack = 13;
}

namespace room {
constexpr float dark_lux = 100.0f;
constexpr float teacher_margin_cm = 20.0f;
}
