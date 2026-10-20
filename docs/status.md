# Bring-up status

Recorded 2026-10-20. Every number below was observed on this machine or read from a file in this
repository. Where a claim has not been exercised at runtime it says so; that is the authoritative
distinction in this document.

## Where the port stands

| Stage | Result |
| --- | --- |
| Disc image read | 148 entries, 136 files, 2,083,933,828 bytes of file data, partition base `0x00000000` (`docs/xbe-analysis.md`) |
| XBE identified | `assets/default.xbe`, 2,834,432 bytes, sha1 `c6fb6082900532cde5fa8dbb7de8dfa202f0fdad`, title `0x4D530002`, retail, base `0x10000`, entry `0x30F7F`, built 2001-09-25, **XDK 3925** (D3D8, DSOUND) / **3911** (XAPILIB, XBOXKRNL, LIBC, XGRAPHC) |
| XDK signature set | the SDK shipped 5849 and 5558 only; `sdk/xdk/3925/` was seeded by `tools/sig_bootstrap.py` and names 14 of 240 by constants alone, because this XBE has **no `.XTLID` section** |
| Kernel imports | 111 imported, 111 covered, 0 uncovered (`docs/kernel-coverage.md`) |
| Recompilation | 10,589 functions, 633,913 instructions, 2,165,441 of 2,368,328 code bytes (91.4%), 384 jump tables |
| Native Linux build (Vulkan + SDL3) | builds, links and plays, `out/build/linux-release/FuzionFrenzy` |
| Windows build (D3D12 + XAudio2), cross-compiled on Linux | builds and links, `out/build/windows-cross/FuzionFrenzy.exe`, 15,951,360 bytes — **never run on Windows**; under Wine 11.20 it reaches a minigame in play with zero shader failures (`/tmp/wine_play.png`) |
| Boot | starts, reads settings, finds and verifies the disc, runs the title to its Direct3D device creation |
| Picture on screen | yes — intro movie, menus, character select and arenas in play |
| Gameplay | reaches minigames and plays them under the scripted pad (`FUZION_AUTOSTART=1`) |
| Characters | render skinned and textured (`docs/render-defect.md`) |
| Frame rate, Linux/Vulkan | mean 59.9 fps over 199 s of play, worst second 29.9, against 41.7 / 17.0 before the renderer work |
| In-game clock | real time: 1:30 to 1:16 over 15 s of wall clock |
| Game speed vs frame rate | **not frame-step quantised**: at `fps_cap` 30 vs 120 the gameplay frame rate differs 2.38x (30.0 vs 71.4 fps) and the title reaches the same scene 0.4 s apart (53.1 s vs 53.5 s). The intro movie takes 11.3 s under both caps. Nothing to fix for this title. |
| Audio | **never verified** |
| F1 / F2 panels in play | **never seen running** |
| Known defect | one arena's floor shades flat white (`docs/render-defect.md`) |

## Symbol coverage

The scanner's 240 names include libraries this image does not link. Counting by the 5849 set's own
`sec` tag and dropping the sections the XBE has no code for:

| `sec` | Names | Reachable | Named |
| --- | --- | --- | --- |
| `D3D` | 100 | yes | see below |
| `DSOUND` | 63 | yes | 23 |
| (none) | 17 | yes | — |
| `.text` | 15 | yes | — |
| `XPP` | 5 | yes | 5 |
| `XNET` | 34 | **no** — no XNET section | 0, expected |
| `BINK` | 4 | **no** — no BINK section | 0, expected |
| `XONLINE` | 2 | **no** — no XONLINE section | 0, expected |

So the real target is **200 names**. `xbe_symbols` found 14. Hand analysis of the image, verified
against properties measured to be stable across two real XDK builds, has raised that; the live count
is in `config/game.toml` and the per-area evidence is in `docs/d3d8-state-symbols.md`,
`docs/d3d8-core-symbols.md`, `docs/dsound-symbols.md`, `docs/xapi-crt-symbols.md`,
`docs/effects-image-xpp.md` and `docs/align-symbols.md`.

Cross-build invariants measured over the 240 names present in both the 5849 set and a re-signed 5558
set (`config/ref/5558-signed.toml`, produced with `xbe_symbols --sign` from the sibling port's
configuration):

| Property | Agreement between 5849 and 5558 |
| --- | --- |
| `ret` (bytes popped by `retn N`) | 171 / 171 |
| `regs` (registers read before written) | 175 / 175 |
| byte `len` identical | 171 / 175 (97.7%) |

`ret` and `regs` are therefore used as disqualifiers and length as corroboration. Constants alone
were measured and found insufficient at this build distance: seeding the 3925 set from 5849
constants yields 14 names, from the intersection of 5849 and 5558 constants 9, from their union 8.

## What this title does NOT have

These are findings, not gaps, and each one removes work rather than adding it:

- **No `.XTLID` section.** The export-id channel that named 65 symbols on the sibling port's first
  scan contributes nothing here.
- **No `D3DDevice_Reset` and no `D3DDevice_InsertCallback`.** Neither exists in XDK 3925 and the game
  calls neither, so the sibling port's boot crash (defect 2 of its `docs/status.md`) and its
  push-buffer deadlock cannot occur in this port.
- **No `D3DDevice_Swap`.** This build presents through `D3DDevice_Present`, four arguments,
  `retn 10h`.
- **No `D3DDevice_GetBackBuffer2` / `GetDepthStencilSurface2`.** It has the older three-argument
  `GetBackBuffer(BackBuffer, Type, ppSurface)` and one-argument `GetDepthStencilSurface(ppSurface)`.
- **No `CDirectSoundStream` class at all.** DSOUND stores 14 vtables and none has the stream layout;
  no constructor, factory or allocation for a stream exists and the game never creates one. All 21
  `CDirectSoundStream_*` names, `IDirectSoundStream_SetEG` and `DirectSoundCreateStream` stay 0 on
  purpose.
- **No network code.** No XNET or XONLINE section, no network kernel imports, no socket strings.
- **No `CDevice` `BackBuffer`, `FrontBuffer`, `BackBufferScale`, `ScreenSpaceOffset` or `Field`
  field.** The back buffers are surface objects embedded in `CDevice` at `+0x2150 + index * 0x18`,
  and the viewport code adds a fixed `0.53125` inline instead of reading a stored screen-space offset.

## Two SDK defects this title exposed

Both are XDK-5849-era assumptions baked into the runtime, and both are **silent corruption** rather
than a clean failure, so they had to be fixed before the corresponding symbols could be merged.

1. **Render-state numbering.** `sdk/runtime/gpu/state.h` stated outright that its `kRs*` constants
   are "D3DRS indices of the XDK 5849 build this title links", and `sdk/runtime/xdk/d3d8.cpp` used
   them as offsets into the **guest's own** `D3D_RenderState` array. XDK 3925 numbers its deferred
   states from 82, its complex states from 116, and has 144 in total; the array ends exactly at
   `D3D_Device` (`0x203B90`). A 5849 index such as `CullMode` 147 would have written `0x203B9C`,
   which is `CDevice_Flags`.
2. **Deferred texture-stage type numbering.** 3925's types 0..21 are `(5849 + 10) mod 22`, so
   `ColorOp` is 0 and `AddressU` is 10; types 22..30 are unchanged.

The fix keeps the 5849 numbering as the host's canonical one and maps the title's numbering onto it
from `sdk/xdk/<build>/layout.toml`. A build whose config carries no index table falls back to the
built-in constants, so XDK 5849 and 5558 are unaffected.

A full audit of the runtime for other assumptions of the same kind found no third class of silent
corruption: everything else is either already key-driven through `layout.toml` and
`XDK_REQUIRE`, or an NV2A hardware identity that 3925 verifiably shares. One latent difference is
recorded and deliberately not fixed, because this title never exercises it: 3925's vertical-blank
handler calls the callback with the blank count **by value**, where `d3d8.cpp` passes 5849's
`{count, swaps, flag}` pointer. The title never sets a callback.

## The boot, as observed

The first boot took signal 11 at 0.083 s inside the title's own D3D8, reading the NV2A register
window at `0xFD000000` — the canonical symptom of an unnamed library entry point letting console code
run. Identifying `Direct3D_CreateDevice = 0x1F7650` replaced that whole path with the host's, and the
run now stops in the host's own `XDK_REQUIRE`:

```
[    0.072] error: d3d: D3D_DirtyFlags is unknown for this image, add it to the config
[    0.072] error: d3d: D3D_DeferredTextureState is unknown for this image, add it to the config
[    0.072] error: d3d: D3D_RenderState is unknown for this image, add it to the config
[    0.072] error: d3d: D3D_Streams is unknown for this image, add it to the config
[    0.072] error: d3d: D3D_SimpleRenderStateEncode is unknown for this image, add it to the config
[    0.072] error: signal 6 on guest thread 14
```

That is the designed failure of `sdk/runtime/xdk/layout.h`, which traps on a value the config lacks
"rather than being guessed". Full account: `docs/boot-crash.md`.

## Not proven

- No frame has ever been presented, so nothing about the renderer, the picture rect
  (`src/common/game_info.cpp` carries the no-crop default `picture_top = 0`, `picture_height = 480`),
  aspect handling or ultrawide is verified for this title.
- No audio has ever been produced.
- The F1 settings panel and F2 frame-time panel are carried over from the sibling port unchanged and
  compile, but have never been seen running in this game.
- No controller has ever been plugged into the test machine, and the keyboard-as-pad mapping has
  never been exercised in this game.
- The Windows binary has only been cross-compiled on Linux; it has never been run.
- The cheat plumbing exists with an empty table. No cheat code or cheat setter address has been
  identified for this title.
