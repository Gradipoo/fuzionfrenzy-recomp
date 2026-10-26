#pragma once

// Copied to sdk/runtime/core/trace.h by tools/apply-tracy.py.
// XBE_TRACY_ZONES
//
// Empty unless the translation unit is compiled with XBE_TRACY and Tracy's
// own TRACY_ENABLE. The default build does not define either, and does not
// fetch Tracy.

#if defined(XBE_TRACY) && defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#include <tracy/TracyC.h>

namespace xbe_trace {

struct Span {
    TracyCZoneCtx ctx{};
    bool open = false;

    void Open(const ___tracy_source_location_data* loc) {
        if (open) ___tracy_emit_zone_end(ctx);
        ctx = ___tracy_emit_zone_begin(loc, 1);
        open = true;
    }
    void Close() {
        if (!open) return;
        ___tracy_emit_zone_end(ctx);
        open = false;
    }
};

inline const ___tracy_source_location_data& FrameLoc() {
    static const ___tracy_source_location_data loc{"frame", "PresentFrame", "d3d8.cpp", 0, 0};
    return loc;
}
inline const ___tracy_source_location_data& TickLoc() {
    static const ___tracy_source_location_data loc{"game tick", "PresentFrame", "d3d8.cpp", 0, 0};
    return loc;
}
inline Span& FrameState() {
    static thread_local Span s;
    return s;
}
inline Span& TickState() {
    static thread_local Span s;
    return s;
}

inline void BeginFrame() { FrameState().Open(&FrameLoc()); }
inline void EndFrame() { FrameState().Close(); }
inline void BeginGameTick() { TickState().Open(&TickLoc()); }
inline void EndGameTick() { TickState().Close(); }
inline void MarkFrame() { FrameMark; }

}  // namespace xbe_trace

#define XBE_ZONE(name) ZoneScopedN(name)

#else

namespace xbe_trace {
inline void BeginFrame() {}
inline void EndFrame() {}
inline void BeginGameTick() {}
inline void EndGameTick() {}
inline void MarkFrame() {}
}  // namespace xbe_trace

#define XBE_ZONE(name) do { (void)sizeof(name); } while (0)

#endif
