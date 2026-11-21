// Desktop test for quiet_study_logic.h.
// Build:  g++ -std=c++17 -I src/common tests/test_quiet_study_logic.cpp -o test_quiet_study && ./test_quiet_study
// Or on Windows:  g++ -std=c++17 -I src/common tests/test_quiet_study_logic.cpp -o test_quiet_study.exe && test_quiet_study.exe

#include "quiet_study_logic.h"
#include <cassert>
#include <cstdio>

static int passed = 0;

static void ok(const char* name) {
    ++passed;
    std::printf("  [PASS] %s\n", name);
}

int main() {
    std::puts("== Quiet Study logic tests ==\n");

    // 1. Noise above threshold, child present, no session -> warn.
    {
        lumpi::NoiseState s;
        s.child_present = true;
        s.session_active = false;
        assert(s.on_noise(10000, 8000, 1000) == true);
        ok("1  loud + present + idle = warn");
    }

    // 2. Noise above threshold, session active -> don't interrupt.
    {
        lumpi::NoiseState s;
        s.child_present = true;
        s.session_active = true;
        assert(s.on_noise(10000, 8000, 1000) == false);
        ok("2  loud + session active = no warn");
    }

    // 3. Noise above threshold, no child present -> no warn.
    {
        lumpi::NoiseState s;
        s.child_present = false;
        s.session_active = false;
        assert(s.on_noise(10000, 8000, 1000) == false);
        ok("3  loud + no child = no warn");
    }

    // 4. Noise below threshold -> no warn.
    {
        lumpi::NoiseState s;
        s.child_present = true;
        s.session_active = false;
        assert(s.on_noise(5000, 8000, 1000) == false);
        ok("4  quiet = no warn");
    }

    // 5. Noise above threshold, within cooldown -> no warn.
    {
        lumpi::NoiseState s;
        s.child_present = true;
        s.session_active = false;
        assert(s.on_noise(10000, 8000, 1000) == true);   // first warning at t=1000
        assert(s.on_noise(10000, 8000, 20000) == false);  // 19 s later: still cooling
        ok("5  loud + within cooldown = no warn");
    }

    // 6. Noise above threshold, after cooldown expires -> warn again.
    {
        lumpi::NoiseState s;
        s.child_present = true;
        s.session_active = false;
        assert(s.on_noise(10000, 8000, 1000) == true);    // first warning at t=1000
        assert(s.on_noise(10000, 8000, 32000) == true);   // 31 s later: cooldown done
        ok("6  loud + after cooldown = warn again");
    }

    std::printf("\n%d / 6 passed.\n", passed);
    return passed == 6 ? 0 : 1;
}
