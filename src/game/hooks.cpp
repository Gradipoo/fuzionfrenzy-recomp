// Hooks for default.xbe. A strong definition replaces the recompiled function at that address.
//
// There are none yet: no guest function of Fuzion Frenzy has been identified for one, so every
// function in the image runs exactly as it was recompiled. What the sibling port this one is built
// from carries in its copy of this file is the work this port is expected to need. Each piece has to
// be found again in this image, by address, and confirmed against its disassembly before anything
// goes below; nothing carries over by analogy.
//
// - The frame step. That engine measured real time correctly and then quantised it to whole 60ths
//   of a second with a floor of one tick, so a frame shorter than 1/60 s still advanced the world by
//   1/60 s: at 144 fps the game ran 2.4 times too fast, and uncapped without bound. Its fix replaced
//   the quantiser (found among the callers of the title's time source: a clamp of
//   elapsed / tick + 0.5 to 1..N ticks) with 1 ms ticks, kept the engine's own maximum step, wrote
//   the adjusted seconds-per-tick back into the guest's tick-size float so the caller's
//   ticks * tick size is exactly the time that really passed, and made the frame tick take that
//   product path. Whether Fuzion Frenzy steps its frame like that has not been looked at; until it
//   has, the F1 panel's frame cap note ("the game still steps by the time that really passed") is
//   the sibling's finding, not this title's. The check is simulated seconds per real second at 60,
//   144 and uncapped, with a FUZION_NO_STEP run beside it as the control.
// - D3DDevice_InsertCallback, dispatched at once. On the console the callback goes into the push
//   buffer and runs when the GPU reaches it; the SDK walks its push buffer for draws only, so the
//   callback never runs, and an engine thread waiting on what it releases idles forever: a silent
//   deadlock some frames in, every guest thread waiting. The fix there was a host
//   HLE_STDCALL(D3DDevice_InsertCallback, 3) that calls pCallback(Context) straight away through
//   kernel::CallGuest - the host renderer has no GPU timeline for the guest to be behind - plus the
//   function's address in config/game.toml's [symbols], added by hand: the name was not in the
//   XDK signature set then, so it was located by neighbour order (between D3DDevice_IsFencePending
//   and CDevice_KickOff) and confirmed by its body (it writes the callback token and pops three
//   stack arguments). sdk/xdk/3925/signatures.toml, the set for this title's XDK build, has no entry
//   for it either, so if this title has the function it is found the same way; whether it calls it
//   at all is not known yet.
// - Linux only: the aspect getter, for Hor+ ultrawide. The Vulkan backend builds the frame target
//   wider than 16:9 at app::HostAspect() and relies on the title's projection being widened to the
//   same aspect from this file (sdk/runtime/gpu/vulkan.cpp, WidenedGuestWidth). The sibling
//   overrides the one getter its projection builders take the aspect from, for the proven
//   projection call sites only. Until this title has the same, its ultrawide setting widens the
//   frame on Linux but not the 3-D view.
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>

#include "cpu/x86_ops.h"
#include "xdk/hle.h"
#if !defined(_WIN32)
#include <cmath>

#include "app/platform.h"  // app::HostAspect
#include "core/log.h"
#endif

namespace {

// Each wrapper this file gains can be switched off for the run, so a defect can be bisected against
// the stock behaviour without a rebuild: FUZION_NO_STEP for the frame step, the first one expected.
// Off means the recompiled function runs exactly as the image wrote it. Nothing calls this until
// that first wrapper exists.
[[maybe_unused]] bool HookOff(const char* name) {
    const char* v = std::getenv(name);
    return v && v[0] && std::strcmp(v, "0") != 0;
}

}  // namespace
