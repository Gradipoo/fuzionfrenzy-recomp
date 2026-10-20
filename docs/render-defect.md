# Render defect: characters drew as spikes — solved

Recorded 2026-10-20 from driven runs of `out/build/linux-release/FuzionFrenzy`.
Superseded the earlier reading of this defect as "every 3-D draw is edges only"; that
description came from character-select frames where the only 3-D on screen was skinned
characters, and those were the broken thing, not the fill mode.

## What was on screen

Characters rendered as a fan of thin spikes radiating from one point: the shapes were
positioned correctly in the frame and the 2-D art, text and HUD around them were right.
`out/shots/before_55s_zoom.png` is the character-select roster in that state;
`out/shots/fx_55s_zoom.png` is the same screen after the fix.

A fan of spikes converging on one point is the signature of a vertex that should have been
placed by a bone transform and instead got the zero matrix: every vertex whose bone is
missing collapses to the origin, and the triangles joining it to the vertices that did
transform stretch out as slivers.

## Two readings ruled out first

- **Fill mode.** `vulkan.cpp` picks `VK_POLYGON_MODE_LINE` only when the pipeline key's
  `fill` is `0x1B01` (`D3DFILL_WIREFRAME`). A probe at present time logged
  `g_state.rs[kRsFillMode]` as `0x1B02` (`D3DFILL_SOLID`), and a probe over the first 24
  pipelines the title built showed `fill=1B02` on every one. Nothing asked for wireframe.
- **Draw topology.** The same pipeline probe showed `topo=2` (triangles) on 20 of 24
  pipelines. The geometry was being drawn as triangles.

## Cause

A vertex-constant upload on the NV2A is

    [0x41EA4, register + 96]  then  [(dwords << 18) | 0xB80, data...]

and the load register the data lands in **advances by one per four dwords written**, across
as many packets as the upload needs. One packet carries at most the 32 dwords of methods
`0xB80..0xBFC`, which is eight registers.

`Walk` in `sdk/runtime/xdk/d3d8.cpp` required the `0x41EA4` header on every constant packet.
A world transform is four registers, fits one packet, and survived. A skinning palette is
tens of registers, so it arrived as several packets of which only the first followed a
`0x41EA4`; every continuation fell through to the generic method path, which logs the method
once and discards the values. Characters therefore kept only the first eight registers of
their palette and every other bone stayed at the zero matrix.

The walker now tracks the load register as device state, exactly as the hardware does.

## Evidence it is fixed

| Measure | Before | After |
| --- | --- | --- |
| Packets `Walk` discarded, Military Base arena | 33 | 1 (method `0x1710`, a single unrelated one-value packet) |
| Character select | spikes from a point | fully skinned, textured figures |

`out/shots/fx_55s_zoom.png` and `out/shots/pass_55s_zoom.png` show Jet, Geena and Samson
rendering correctly; `out/shots/deep_90s.png` shows a solid, textured arena in play.

## Still open: a white arena floor

Separately, one arena's floor shades to flat `(255,255,255)` while every object standing on
it textures and shades correctly — `out/shots/speed_125s.png`, where 313,545 of 320,000
pixels sampled in the middle of the frame are pure white. It is **not** caused by the
renderer's per-draw reuse caches: scanning eight gameplay frames with every cache disabled
gave a worst case of 2.5% pure white against 2.1% with them all enabled. Stage-0 textures
bind cleanly in that scene (`undescribable 0` over a 200 s run), so it is not a missing
texture falling back to white. Not yet root-caused.
