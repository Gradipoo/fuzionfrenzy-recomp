# Profiling

Tracy is off in every preset except `macos-tracy`. A normal `./build.sh` does
not fetch it, link it, or define `XBE_TRACY`. The call sites are still
compiled: `tools/apply-tracy.py` copies `tools/tracy/trace.h` into the sdk
tree and inserts the zones, and without the flag they are empty functions.

## Tracy

[Tracy](https://github.com/wolfpld/tracy) 0.14.1 (tag `v0.14.1`,
`30997d5ca6bb632cc10807a1da8a6d3de0aeeb3c`, BSD-3).

```
./build.sh macos-tracy
```

That preset is RelWithDebInfo with `XBE_TRACY=ON`. CMake fetches the tag into
the build tree. The client is on-demand and listens on localhost only, with
broadcast, crash handling, system tracing, and frame images off. The game
runs normally until a Tracy server connects, and a capture starts when it
does. Use a 0.14 server. An older server will not speak this protocol.

Zones, on by name in the capture:

| Zone | Where |
| --- | --- |
| `frame` | `PresentFrame`, from the guest present call through the buffer rotate. `FrameMark` is the present. |
| `game tick` | The presenting thread, from the end of that present's bookkeeping until the next present. This is the recompiled sim between frames. |
| `gpu submit` | `vkQueueSubmit` / `ExecuteCommandLists`. |
| `gpu present` | The Vulkan or D3D12 present, including the submit inside it. On macOS this runs on the render thread, one present behind the guest. |
| `audio` | The mixer (`RenderClocked`, or `Render` on the upstream tree). |

`XBE_PERF=1` is the cheap log when a capture is more than the question. One
`perf:` line a second. It does not need Tracy.

## Metal and Instruments

GPU time is not in the Tracy zones above. Those are CPU time around submit
and present. On this Mac the frame itself is Metal, through MoltenVK.

Xcode, with `RingHost` as the scheme (or Attach to Process):

- The camera button in the debug bar, or Debug → Capture GPU Frame, grabs one
  frame: draws, render passes, and the dependency graph.
- Product → Perform Action → Capture Metal System Trace records a timeline of
  command buffers, the GPU, and the display. Use that for a stretch of
  Twisted System rather than a single frame.

Instruments, attach to `RingHost`:

- Metal System Trace: GPU duration per frame, shader and encoder time.
- Game Performance: frame interval next to GPU and CPU.
- CPU Profiler: the guest tick and the render thread, the same split Tracy names.
- Energy Log: CPU, GPU, and wakeups. That is the one for the later battery
  and heat pass. A minimized window is not that measurement. Leave it on the
  display, vsync on, and note the charger.

## Baseline, Twisted System

`macos-release`, Tracy off, muted, minimized, vsync on, 1280×720 windowed,
`--instance 7`, `XBE_PERF=1`. Started 4:17 AM Pacific Time on 26 Oct 2026
and stopped at 110 s. The script is the same autostart that reaches character
select and then the first arena. Release drops the `playing` info line, so
the in-round is the settled stretch rather than that message: 93–110 s,
17 one-second samples, about 6160 draws a frame at about 52 fps. That is the
same shape as the sanitizer session's in-round (about 50 fps and about 6300
draws, after character select). On a new game that arena is Twisted System.
This run did not keep a picture of it.

| | ms/frame |
| --- | --- |
| Wall clock | 19.4 (51.7 fps). Not sitting on the 16.7 ms vsync cap. |
| Vulkan backend, render thread | 18.5. Draw recording 17.2, clear 0.2, present 1.1. |
| Title thread inside XDK draws | 0.7 |
| Title thread recording | 1.6 |
| Title thread waiting | 12.3, of which 11.6 is the `resource` bucket and 0.7 is the next frame |

The frame is about as long as the CPU spends recording draws. The guest is
blocked in that resource wait while the render thread records, so the sim is
not the long pole. These are CPU times. A Metal timestamp was not taken, and
the window was minimized, so this is not a GPU-energy number. Metal System
Trace on a visible window is the measurement that pairs with it.
