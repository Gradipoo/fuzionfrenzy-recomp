# Render defect: every 3-D draw is edges only

Recorded 2026-10-20 from driven runs of `out/build/linux-release/FuzionFrenzy`.

## What is on screen

Everything the title draws in 2-D is right, and everything it draws in 3-D is an outline.

| Frame | What renders | What is wrong |
| --- | --- | --- |
| `out/shots/boot1_25s.png` (character select) | text, portrait discs, names | all six portraits except one are black hollow silhouettes on live coloured discs |
| `out/shots/gameplay1_40s.png` (roster) | text, discs | character silhouettes are black hollow outlines |
| `out/shots/gameplay1_55s.png` (player confirm) | text, portraits, player names | the 3-D weapons above the names are stretched line-fan outlines in wrong shapes, with no fill |
| `out/shots/gameplay1_70s.png` (Stage I, Military Base, round one) | banner art | the arena is a full wireframe around one flat-shaded sand platform; the four player characters are not visible |
| `out/shots/front_00049.png` (guest front buffer) | Microsoft logo | correct |

So: vertex positions, matrices and indices are right (the silhouettes are the right shapes in the
right places), 2-D and texture art are right, and **triangle fill is missing**. The defect is not in
the guest - its own front buffer (the Microsoft logo frame above) is complete - so it is in the host
path between the guest's draws and the swapchain.

## Classification

Two known SDK defect classes were ruled out with code evidence by a static audit
(`docs/d3d8-rest-symbols.md` covers the surrounding names):

- Vertex attribute locations numbered by guest register: fixed in this SDK
  (`sdk/runtime/gpu/vulkan.cpp:1187-1194`, `attrs[na].location = na`).
- The 2-D sprite UV-rect clamp acting on 3-D draws: it acts on 2-D quads only.

The audit's leading suspect for the **characters specifically** is the one D3D entry that is
deliberately not replaced: the 3925 `D3DDevice_SetVertexShaderConstant` (`0x1F84A0`, stdcall,
`(Register+300)*16` mirror, +96 hardware bias), through the chain

```
guest SetVertexShaderConstant body -> ScratchPush/MakeSpace
  -> Walk() -> StoreVsConstants -> g_state.vsc[192]
```

Skinning palettes are uploaded there (`SetVertexShaderConstant(-96, …)` -> c0-c56, and `(-36, …)` ->
c60+; the audit found the palette upload at `sub_3CED0`). The host touches only c58/c59 (viewport
pair), c97/c98 (fold) and c121 (2-D centre), so nothing else in the SDK competes for those slots.
`Walk`'s generic path does `break` on `(head & 0xA0030003)`, which can silently drop everything after
one unexpected dword; its "Walk drops method" diagnostic is `LOGI` and therefore release-silent.

## Measurements taken, and what they exclude

- A verbose intro run logs **no** "Walk drops" lines, and only 8 texture-stage, 8 Present and 3
  BlockOnResource `d3d:` lines. That excludes the drop hypothesis for the intro, but the intro is not
  where the defect shows: it is visible on the character select and in the arena, which have not been
  run under the verbose build yet.
- The intro and menu are **one draw and one clear per frame**, plus a 1.17 MB texture upload per
  frame, which is the Bink movie frame being uploaded each present.
- Perf on the same run: 25-30 fps at 40 ms per frame, of which the host accounts for about
  1.5 ms (draw 1.2, present 0.23). **The renderer is not the frame-rate problem** at these draw
  counts. Three unsynchronised ~60 Hz clocks pace this port (host `fps_cap`, the simulated vertical
  blank, and the compositor's FIFO present), so part of "low fps" is the title running at its own
  capped rate.

## The next measurement, before any fix

`sdk/runtime/gpu/vulkan.cpp:1621` reads

```cpp
k.fill = s.rs[kRsFillMode];
```

`kRsFillMode` is the **canonical (XDK 5849) index 139**; in XDK 3925 the same state is index **119**
(`RenderState_FillMode = 119` in `sdk/xdk/3925/layout.toml`). The remap in
`sdk/runtime/xdk/d3d8.cpp` translates the writers it knows about. If anything still writes the guest
or host render-state array at a 3925-derived index, the host reads the wrong slot and the Vulkan
polygon mode becomes `VK_POLYGON_MODE_LINE` - which is exactly "correct vertices, edges only, every
3-D draw, 2-D unaffected", and would also explain why one platform is flat-shaded while everything
around it is not.

That is a hypothesis. It must be measured, not edited:

1. Run the verbose build far enough to reach the arena
   (`FUZION_AUTOSTART=1 tools/shot_run.sh arena1 out/build/linux-verbose 70`) and read the `perf:`
   line for the in-arena draw count.
2. Log `s.rs[kRsFillMode]` and the resolved `k.fill` once per present from `vulkan.cpp` and compare
   with the guest's own `D3D_RenderState[119]`.
3. If they disagree, find the writer. If they agree, the fill hypothesis is dead and the next
   candidate is the draw-topology interpretation of the push stream on Vulkan.

No code has been changed on this evidence yet.
