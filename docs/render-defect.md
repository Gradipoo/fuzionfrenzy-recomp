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

## The white arena floor: cause found, fix not yet proven on the scene

One arena's floor shaded flat `(255,255,255)` while every object standing on it textured and
shaded correctly — `out/shots/speed_125s.png`, where 313,545 of 320,000 pixels sampled in the
middle of the frame are pure white.

What a frame capture of that scene showed (`XBE_CAPTURE_DIR`, F3 path):

- The title clears two 128x128 off-screen surfaces to opaque white and submits 271 draws into
  them. Those surfaces came out **100.0%** white: not one of the draws painted a pixel.
- The floor samples those surfaces, so it takes their clear colour.
- In healthy scenes the equivalent surfaces are 256x256 and come out 94-96% white, which is
  what a mostly-unshadowed pass should look like.

Ruled out along the way, each by measurement: the renderer's per-draw reuse caches (eight
gameplay frames with every cache disabled gave a worst case of 2.5% pure white against 2.1%
with them all enabled); a missing palette (P8 is used in nearly every gameplay frame and the
count of P8 binds with no palette was 0 over a 240 s run); an unbound texture falling back to
white (`nullImage_` is cleared to black); and the texel decoders for the formats in play
(0x02, 0x06, 0x0B, 0x12).

The cause is an ordering hazard in the ultrawide centring. `FrameCentreShift` decides whether
to shift by looking at the surface bound **at the moment the constant is stored**
(`gpu::SurfaceSize(g_state.rt ...)`), and the shift was baked into the stored constant. A title
that writes its fold and screen constants while the frame is still bound and only then switches
to the off-screen surface they were meant for gets the frame's shift applied to an off-screen
pass, which pushes its geometry off its own target: it draws nothing and the surface keeps its
clear colour. `CentreVsConstant` now keeps the value the title actually wrote, and
`RecentreVsConstants` re-derives the shifted value whenever the bound surface changes.

**What is proven:** the hazard is real and is fixed, and the fix does not regress the widened
frame — `out/shots/rc_215s.png` is a fully textured, correctly centred arena, and a 200 s driven
run reports 0 skipped draws at a mean of 59.7 fps. **What is not proven:** that this was the
cause of the white floor. That scene was only ever reproduced once, before the fix; across
roughly 25 minutes of driven play afterwards it has not recurred, but absence over eleven
sampled frames and two 450 s capture runs is not the same as a demonstrated repair.

## Twisted System characters drew flat white — solved

Recorded 2026-10-25 on the macOS arm64 build, Twisted System, manual play.

In the round the characters are small vertex-program draws (`B01CA761`) with stage 0
`MODULATE2X` on a colored 512×512 A1R5G5B5 texture. Diffuse peaks near 0.74, so the
stage-0 product is the texel. Stage 1 is color op 14, args texture / current, alpha op
`SELECTARG1`, and no texture is bound. This shader reads op 14 as `BLENDTEXTUREALPHA`.
A missing texture is `float4(1,1,1,1)`, the blend weight is that alpha, and the result
is arg1: flat white. Blend and fog are off on these draws, so nothing after the stage
hides it. Character select uses a different program whose stage 1 is disabled, which is
why those figures were already textured.

The stage is skipped when it has no texture, the color op is 14, and color arg1 is the
texture (complement not set). A narrower skip, only when the alpha op was disabled, did
not match: these draws set `SELECTARG1`.

Checked the same night, and not applied: Cxbx-Reloaded `NormalizeValue()` in
`src/core/hle/D3D8/Direct3D9/TextureStates.cpp` remaps D3DTOP 13–16 when
`g_LibVersion_D3D8 <= 3948` (old order verified on 3948, later order on 4039). Fuzion
Frenzy's D3D8 is 3925. Their later-XDK constants, which match this shader's cases, are
13 `BLENDCURRENTALPHA`, 14 `BLENDTEXTUREALPHA`, 15 `BLENDFACTORALPHA`,
16 `BLENDTEXTUREALPHAPM`, 17 `PREMODULATE`. The remap is:

| Raw | XDK 3925 | Later value |
| --- | --- | --- |
| 13 | `BLENDTEXTUREALPHA` | 14 |
| 14 | `BLENDFACTORALPHA` | 15 |
| 15 | `BLENDTEXTUREALPHAPM` | 16 |
| 16 | `BLENDCURRENTALPHA` | 13 |

`PREMODULATE` (17) is the same in both and is not remapped. Raw 14 on these character
draws would become `BLENDFACTORALPHA`. The texture factor on them is `0x60000000`
(alpha 96/255) and arg1 would still be the missing texture, sampled white, so the stage
would mix white back into the texel. That is a different picture from the skip, which
was played through the countdown and the round with the characters textured and with no
other scene in that session looking wrong. The decoder is unchanged so the skip still
sees op 14.

The floor mesh in that round is separate: fixed-function, 8192 vertices, stage 0
`MODULATE2X` on a 64×64 P8 texture, stage 1 disabled. This skip does not run on it.
