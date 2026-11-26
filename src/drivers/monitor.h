#pragma once
// EASY-TO-READ SERIAL MONITOR: the same layout in every bench sketch.
//
//   ================ STAGE 1b: PRESSURE MAT ================      monitor::title(...)
//   Tests: the mat's reading rises when a book is on it.
//   Pass:  near 0 when empty, above 1500 with a book.
//
//   WIRING                                                     monitor::section("WIRING")
//     FSR signal ......... GPIO35, 10k resistor to GND         monitor::item("FSR signal", "GPIO%d, ...", pin)
//
//   FAIL  no answer from the light sensor                      monitor::fail(what, means)
//         means: check SDA 21 and SCL 22
#include <Arduino.h>
#include <cstdarg>

namespace monitor {

inline void title(const char* name, const char* tests, const char* pass) {
    Serial.printf("\n================ %s ================\n", name);
    Serial.printf("Tests: %s\n", tests);
    Serial.printf("Pass:  %s\n", pass);
}

inline void section(const char* name) {
    Serial.printf("\n%s\n", name);
}

// "  Label ............ value". format works like printf.
inline void item(const char* label, const char* format, ...) __attribute__((format(printf, 2, 3)));
inline void item(const char* label, const char* format, ...) {
    Serial.printf("  %s ", label);
    for (size_t i = strlen(label); i < 19; ++i) {
        Serial.print('.');
    }
    Serial.print(' ');
    char value[160];
    va_list args;
    va_start(args, format);
    vsnprintf(value, sizeof(value), format, args);
    va_end(args);
    Serial.println(value);
}

inline void fail(const char* what, const char* means) {
    Serial.printf("  FAIL  %s\n        means: %s\n", what, means);
}

} // namespace monitor
