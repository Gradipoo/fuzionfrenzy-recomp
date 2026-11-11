#pragma once
// Shared fixed-step clock. The harness and the unit test both use these.
// A present is one sim tick. Wall-clock Pace() is separate: it still sleeps
// one frame at fps_cap so a normal launch stays at 60. FUZION_NO_PACE skips
// that sleep for snapshot replays. It does not change the sim tick.
#include <cstdint>

namespace det {

constexpr uint64_t kEpoch100ns = 134116992000000000ull;
constexpr uint64_t kPerfHz = 3375000ull;
constexpr uint64_t kPerfPerFrame = kPerfHz / 60ull;             // 56250
constexpr uint64_t kInterruptHz = 10000000ull;
constexpr uint64_t kInterruptPerFrame = kInterruptHz / 60ull;   // 166666
constexpr uint64_t kRdtscHz = 733333333ull;
constexpr uint32_t kAudioFrames = 48000 / 60;                   // 800

inline uint64_t PerfAt(uint64_t tick) { return tick * kPerfPerFrame; }
inline uint64_t InterruptAt(uint64_t tick) { return tick * kInterruptPerFrame; }
inline uint64_t SystemAt(uint64_t tick) { return kEpoch100ns + InterruptAt(tick); }
inline uint64_t RdtscAt(uint64_t tick) { return tick * (kRdtscHz / 60ull); }
inline uint32_t TickMsAt(uint64_t tick) { return uint32_t((tick * 1000ull) / 60ull); }
inline uint64_t WholeTicksOf(uint64_t hundred_ns) { return hundred_ns / kInterruptPerFrame; }

// Nanoseconds Pace() adds to its deadline when fps_cap is set. 0 disables pacing.
inline uint64_t PacePeriodNs(uint32_t fps_cap) {
    return fps_cap ? 1000000000ull / uint64_t(fps_cap) : 0;
}

}  // namespace det
