// Fixed-step clock and the wall-clock period Pace() uses.
#include "det/clock.h"

#include <cstdio>
#include <cstdlib>

static int g_fails = 0;

static void Expect(bool ok, const char* what) {
    if (ok) return;
    std::fprintf(stderr, "FAIL %s\n", what);
    g_fails++;
}

int main() {
    Expect(det::kPerfPerFrame == 56250, "qpc per frame");
    Expect(30 * det::kPerfPerFrame == det::kPerfHz / 2, "30 reads are half a second");
    Expect(det::PerfAt(30) == 30 * 56250, "perf at tick 30");
    Expect(det::TickMsAt(60) == 1000, "tick ms at frame 60");
    Expect(det::TickMsAt(1) == 16, "tick ms alternates, frame 1");
    Expect(det::TickMsAt(2) == 33, "tick ms frame 2");
    Expect(det::TickMsAt(3) == 50, "tick ms frame 3");
    int alt = 0;
    for (uint64_t t = 1; t <= 60; t++) {
        uint32_t step = det::TickMsAt(t) - det::TickMsAt(t - 1);
        if (step != 16 && step != 17) alt++;
    }
    Expect(alt == 0, "tick ms steps are 16 or 17");
    Expect(det::WholeTicksOf(166666) == 1, "one interrupt quantum is one tick");
    Expect(det::WholeTicksOf(1) == 0, "a short wait drops");
    Expect(det::kInterruptPerFrame * 60 == 9999960, "interrupt step * 60");
    Expect(det::SystemAt(0) == det::kEpoch100ns, "system epoch");
    Expect(det::RdtscAt(1) == 733333333ull / 60ull, "rdtsc step");
    Expect(det::kAudioFrames == 800, "mixer frames per tick");
    Expect(det::PacePeriodNs(0) == 0, "pace off");
    Expect(det::PacePeriodNs(60) == 1000000000ull / 60ull, "pace 60");
    if (g_fails) {
        std::fprintf(stderr, "test_det_clock: %d failed\n", g_fails);
        return 1;
    }
    std::printf("test_det_clock: PASS\n");
    return 0;
}
