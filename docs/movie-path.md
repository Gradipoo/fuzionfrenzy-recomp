# The intro movie: Bink, the texture lock, and the 0.770 s fault

Recorded 2026-10-20 against `out/ff.exe` (the flat PE of `assets/default.xbe`, XDK 3925,
guest addresses 1:1) and the shared IDA database. Every address below is this image's.
The run itself is the orchestrator's: `[    0.770] error: signal 11 on guest thread 14`
with `eax 00000000 ecx FFFFF010 edx 00000000 ebx 00000000 esp D005EBF8 ebp 00000000
esi 00000000 edi 002E9430` and the only two code values on the guest stack at
`[esp+04C] 0004692B` and `[esp+25C] 00013AD8`. I did not build or run anything; the
fix below is for the orchestrator to build and verify.

## The call graph of the intro movie

The caller at `0x13AD3` (return `0x13AD8`) calls the game's own movie player
`bdVideoPlayStream` at `0x463C0` with a filename, flags `0`, no callback and no
context. The `.bik` files on the disc (`Packages/Video/*.bik`) are all 640x480,
25 or 30 fps, `BIKi` with no alpha (`flags 0`), so the texture below is always
640x480, one level, one surface.

```
0x13AD3  caller
  -> 0x463C0  bdVideoPlayStream(filename, 0, NULL, 0)
       0x5A090  register Bink IO callbacks (game's sub_46390/sub_463B0)
       0x591C0  BinkOpen(filename, 0)                       -> HBINK or NULL
       0x41B10  create movie texture (640, 480, 1, 1, 0, fmt 0)
       0x416C0  SetTexture(0, texture)
       0x41810  (stage setup)
       per frame, while frames remain:
         0x585C0  BinkDoFrame(bink)
         0x41A40  lock texture -> (pitch, height, pBits)   [see below]
         0x57FB0  BinkCopyToBuffer(bink, pBits, pitch, height, 0, 0, 3)
         0x41AC0  unlock / write-combined flush (sub_4E900)
         0x3F360  draw the quad (D3DDevice_DrawVerticesUP)
         0x59E20  BinkNextFrame(bink)
         0x41EE0  present (D3DDevice_Present 0x202150), Clear
         0x58B60  BinkWait(bink), spun on with sub_3096F(0) until it returns 0
       0x58E00  BinkGetSummary (only when flags & 2; not taken here)
       0x59F80  BinkClose(bink)
```

`0x4692B` is the instruction after `call 0x57FB0` at `0x46926`, so the fault is at
or below the first `BinkCopyToBuffer` call (the stack is nearly empty — two code
values in 256 words — so it is a shallow frame under it, not a game call site).

## The lock chain, and what pBits actually is

```
0x41A40 (game) -> 0x1F7810 D3DTexture_LockRect (XDK thunk, retn 14h)
               -> 0x1FD160 Lock2DSurface (named, HLE_WRAP in d3d8.cpp:725)
```

- `sub_41B14` (the real body behind the 4-byte `sub_41B10`) allocates the game
  texture object with the game's own allocator, stores width/height/format-code
  at `+46/+48/+44`, puts the surface-header array at `texture+128` (`+36`), and
  for the one surface calls XG's header writer `sub_2285C1 -> sub_228573`, then
  `sub_1FC8E0` to fix the header's Data field up to an absolute address.
  The default format code 0 becomes 12, i.e. XG format 18: linear, 32 bpp
  (format-table byte `0xA2` at `0x22FFE0+18`: not swizzled, `0x20` in the
  `0x3C` bpp field). For 640x480 the Size dword at header `+16` therefore
  encodes pitch 2560, height 480, width 640.
- The header `sub_228573` writes is exactly the shape the host expects:
  `Common = 0x40001` at `+0`, `Data` at `+4`, `0` at `+8`, `Format` at `+12`
  (from `sub_2283C9`), `Size` at `+16`. That is `SurfaceOf()` in
  `sdk/runtime/xdk/d3d8.cpp:304` (`data` at `+4`, `format` at `+12`) and the
  layout `gpu/texture.cpp`'s `Describe` reads. There is no 3925 surface-header
  difference to fix, and no missing `D3DDevice_CreateTexture`: this build has
  none on the host side and the guest's own XG creation code above is what runs,
  allocating guest memory — which is the intended design (`d3d8.cpp:2`:
  "Resource creation, locking and the XG helpers stay recompiled").
- Guest `Lock2DSurface` calls `D3D_BlockOnResource` (host, fine for this
  header), then `sub_1FCF30`, which returns `Data | 0x80000000` from
  `sub_1FC690` as `pBits` and the Size-derived 2560 as pitch. `sub_41A40`
  returns `pBits` in `eax`, pitch through its fifth argument, height through
  its fourth; `bdVideoPlayStream` passes all three straight to
  `BinkCopyToBuffer`.

So the hypothesis in the brief — "`pBits` comes back NULL (or wrong) and
`BinkCopyToBuffer` writes through it", with `ecx = 0 - 0xFF0` a negative
displacement off that null base — is **wrong**, in two independent ways:

1. `pBits` cannot be NULL here. The header's Data field is the texture
   allocation's address plus a small offset, fixed up by `sub_1FC8E0`; and even
   if it were 0, `BinkCopyToBuffer` itself refuses it: at `0x57FB9-0x57FC5` it
   tests both the Bink handle and the destination and returns 0 with no access
   if either is 0 (`cmp esi,ebx / jz`, `cmp [esp+14],ebx / jnz`). A null
   destination never reaches a write.
2. Nor is the `0x80000000` bit a defect. On the console a low virtual address
   *is* the physical address, so `Data | 0x80000000` is the uncached alias of
   the texture's own pages. On the host the same value lands in the fully
   mapped 64 MB physical window (`sdk/runtime/kernel/memory.cpp:26-30`), and
   every host reader of this header translates Data the same way
   (`mem::VirtualFromPhysical(R32(header + 4))` in `gpu/texture.cpp:277`), so
   what Bink writes through `pBits` is exactly what the renderer later uploads.
   It is silent-aliasing-safe for this path, not a wild pointer.

## What faulted

The faulting instruction is **`mov [edi*4+0x2E6BC8], edx` at guest `0x5C886`**,
in `sub_5C635` — the second, per-value loop of `sub_5C170`, the colour-table
setup `BinkCopyToBuffer` calls first (`0x57FF3`). Its operand is a write to
guest address **`0xE8BC88`**, which is uncommitted. Every field of the crash
record points at this one instruction and no other:

- `ecx = 0xFFFFF010` is materialised by exactly one instruction in the image,
  `mov ecx, 0FFFFF010h` at `0x5C859` in the same function — the loop's initial
  brightness value, reloaded from its stack slot at the loop head (`0x5C878`).
  A search of the whole image for that immediate (and for a `-0xFF0`
  displacement in the Bink region) finds no other source.
- `eax = ebx = edx = ebp = esi = 0` is the register state that function's
  type-3 default switch produces: at `0x5C7AF` it zeroes `ecx/edx/ebx/esi/ebp`
  for any type outside 7..12 (3 included), and the mask arithmetic that
  follows leaves `eax` at 0. Destruction of the destination is not in play:
  the loop has not reached a converter yet.
- `edi = 0x002E9430` is the smoking gun, because in that loop `edi` is the
  table index and should be 0 on the first iteration. `0x2E9430` is not an
  index at all: it is the address one past the end of Bink's colour-table
  arena (`0x2E7430 + 0x2000`), and it is exactly the value the preceding
  `rep movsd` at `0x5C617` leaves in `esi` (source `0x2E7430`, `0x800` dwords).
- `esp` puts the `BinkCopyToBuffer` return at `esp+0x4C`, which is the depth
  of `sub_5C170`'s frame *including* the 4-byte error described next.

### Why edi held 0x2E9430: an esp imbalance in the recompiler

Between the `rep movsd` and the loop, `sub_5C170` calls `sub_6D9E0` at
`0x5C625` — Bink's CPUID/MMX probe. That guest function is:

```
6D9E0 push ebx / esi / edi / ds / es,  pushfd/popfd ID-bit toggle, cpuid
6DA0C pop es / ds / edi / esi / ebx,  ret
```

On hardware `push ds/es` and `pop es/ds` are the same size, so the function
preserves every register and its stack. The recompiler's code generation does
not: `sdk/tools/recomp/codegen_int.cpp` emits a 16-bit `PUSH` (segment
registers included) as `ctx.esp -= 2` with a 16-bit store (line 418-420), but
a segment-register `POP` as `ctx.esp += 4` with the value discarded
(line 425-428). Each pair therefore leaves guest `esp` 2 bytes high — 4 for
the pair — and, worse, the pops read the wrong slots: `pop edi` loads the
saved `esi`, `pop esi` the saved `ebx`, `pop ebx` the return address.

So on the first `BinkCopyToBuffer` call: the `rep movsd` leaves
`esi = 0x2E9430`; `sub_6D9E0` is called to probe MMX (the host's `x86_cpuid`
 duly reports MMX+SSE, as the console's Pentium III would); its broken
`pop edi` returns `0x2E9430` in `edi`; guest `esp` is now also 4 bytes high,
which is why the return address sits at `esp+0x4C` rather than the uncorrupted
frame depth. `sub_5C635`'s switch and loop-head then set
`eax/ebx/edx/ebp/esi` to 0 and `ecx` to `0xFFFFF010` exactly as recorded, and
the loop's first indexed write, `mov [edi*4+0x2E6BC8], edx` with
`edi = 0x2E9430`, targets `0x2E9430*4 + 0x2E6BC8 = 0xE8BC88` — an uncommitted
guest address in the user range, well past the image (committed to
`0x330000`) and the heap allocations of the first second. `signal 11`.

This also explains why the destination hypothesis could never have produced
this record: a write through a bad `pBits` would fault in a converter
(`sub_5B4A0` and below) with the Bink handle in `esi`, the pitch in `ebp` and
several converter return addresses on the stack. None of that is present;
the fault is two calls *earlier*, in table setup, before any pixel moves.

### The fix

The defect is in the recompiler, not in this title and not in any file this
slice owns, so there is deliberately **no change to
`sdk/runtime/xdk/d3d8.cpp`, `sdk/runtime/xdk/bink.cpp` or
`sdk/xdk/3925/layout.toml`** — each was checked and is correct for this path
(header shape above; the Bink wrappers' handle offsets `+0x134/+0x13C` match
this build's `BinkGetSummary` body). Working around it from `bink.cpp` is not
possible: any host wrapper still has to call the guest original, and the
corruption happens inside that call, in generated code.

The fix, for the orchestrator (who owns the recompiler and the one build
tree), is one case in `sdk/tools/recomp/codegen_int.cpp`: make a
segment-register `PUSH` symmetric with the existing segment-register `POP`.
In 32-bit mode a segment-register push is a 4-byte push (selector in the low
word), so the 16-bit `PUSH` case should special-case segment registers to
`ctx.esp -= 4` with a 32-bit store, matching `POP`'s `ctx.esp += 4`. Other
16-bit pushes (a `66h`-prefixed `push ax`) keep their existing `esp -= 2`;
only the segment-register form is wrong. This is a strict correctness fix,
not a behaviour change: any guest code in any build (5849 and 5558 included)
that pushes and pops a segment register corrupts its stack the same way
today, and code that never does is unaffected. The generated code must be
recompiled afterwards (`xbe_recomp`), which is also the orchestrator's step.
Prediction for that rerun, and for the siginfo instrument the orchestrator
added: the fault address should read guest `0xE8BC88` (write) before the fix,
and after it the movie should play through `BinkCopyToBuffer` into the draw.
It is also why the failure arrives at 0.770 s and not earlier: `sub_6D9E0` is
called only from `sub_5C170`, which is called only from `BinkCopyToBuffer`
(image-wide call search), so the corrupted stack cannot occur until the first
movie frame — after a clean boot, disc verification, device creation and
DirectSound initialisation, exactly as the log shows.

## The Bink names, and the evidence for each

`config/found-movie.toml` carries seven names. All seven are called only from
`bdVideoPlayStream` in this image (image-wide call search), which is itself the
proof of the group: it is the title's one movie player, it carries the strings
`"bdVideoPlayStream: *** ERROR - BinkOpen failed on file '%s' ***\n"` and
`"Bink playback summary for '%s'\n"`, and the calls sit in exactly the RAD
per-frame order DoFrame -> CopyToBuffer -> NextFrame -> Wait.

| Name | Address | `retn` | Length | Evidence |
| --- | --- | --- | --- | --- |
| `BinkOpen` | `0x591C0` | 8 | `0xC56` | Called at `0x46406` with `(filename, 0)`; NULL return is the game's "BinkOpen failed" path. Checks the `BIKb/f/g/h/i` magics (`1716209986`…), strings "not a bink file" / "error opening file", allocates the 1028-byte handle and its frame buffers. |
| `BinkDoFrame` | `0x585C0` | 4 | `0x53D` | Called at `0x468E8`, first in each frame iteration, before the lock/copy. Decodes the frame (YUV decode `sub_64710`, audio feed `sub_57BA0`) and advances the decode counters at handle `+560/+684`. 5849: `ret 4`, len `0x5D9` — same `ret`, length corroborates at this build distance. |
| `BinkCopyToBuffer` | `0x57FB0` | `0x1C` (28) | `0x5BD` | Called at `0x46926` with `(bink, pBits, pitch, height, 0, 0, 3)`: seven arguments, the RAD destination-format in the last (3 = 32-bit). Guards a NULL handle/destination, builds colour tables (`sub_5C170`), then dispatches on the format's low nibble to the linear (`sub_5D780`) and masked (`sub_61080`) YUV->RGB converters. |
| `BinkNextFrame` | `0x59E20` | 4 | `0x158` | Called at `0x46974`, after the draw/present of the copied frame. Advances to the next frame (frame-index checks against handle `+12/+924`, `sub_58FD0`). 5849: `ret 4`, len `0x19D`. |
| `BinkWait` | `0x58B60` | 4 | `0x1FD` | Called at `0x4698D` and spun on (`while (sub_58B60(bink)) sub_3096F(0);`): it returns 1 while the frame is not yet due and 0 when it is, from the handle's frame-rate fields (`+20/+24/+560/+580/+584`). 5849: `ret 4`, len `0x1F3` — 10 bytes apart. |
| `BinkGetSummary` | `0x58E00` | 8 | `0x1C9` | Called at `0x46ABE` with `(bink, summary)` under the "Bink playback summary" prints. Memsets 0x7C bytes and fills it from the handle (dimensions at `[0]/[4]`, frames, times). **Length is byte-identical to 5849's `0x1C9`** and `ret 8` matches; its `1000 * [handle+0x134] / ([handle+0x13C] + 1)` at summary `+19` is the division `sdk/runtime/xdk/bink.cpp`'s wrapper guards, at the same handle offsets this build uses. |
| `BinkClose` | `0x59F80` | 4 | `0x108` | Called at `0x46BEC` at the end of the player and at `0x464AE` when texture creation fails after a successful open. Tears down sound/IO, frees every registered allocation and memsets the 1028-byte handle to 0. |

The 5849 set tags the four names it has (`BinkDoFrame`, `BinkGetSummary`,
`BinkNextFrame`, `BinkWait`) `sec = "BINK"`, and this image has no BINK section
— Bink is linked into `.text`, which is why those rows could never match here.
`tools/merge_symbols.py` already skips a section check whose expected section
the image does not have, so no merge change is needed for that; it is recorded
here because the config alone does not show it. `BinkOpen`, `BinkClose` and
`BinkCopyToBuffer` are not in the 5849/5558 sets at all, so they had no row to
merge into: `sdk/xdk/3925/signatures.toml` now carries them in the hand-added
form (`sec = ".text"`, `ret`, `regs`, no `len`/`k`/`xtlid`, so the scan never
matches them and the config gives their addresses by hand), following the
`D3DDevice_Present` precedent in the same file. A rescan gives
`config/game.toml` rows for them; until then a merge will (correctly) refuse
them as names the scanner does not know yet.

Naming the four scanner-known names is not cosmetic: `sdk/runtime/xdk/bink.cpp`
wraps the guest's own Bink, and the wrappers only bind when the names have
addresses. `BinkGetSummary`'s wrapper raises the handle's read-time field
(`+0x13C`) before the guest body divides bytes read (`+0x134`) by it — offsets
verified against this build's body above. `BinkDoFrame`'s wrapper consults
`BinkWait` so a movie decoded above the console's 30 Hz present is not advanced
early, and `BinkNextFrame`'s wrapper mirrors that hold. All three wrappers call
the guest original (`GUEST_ORIG`), so naming them changes pacing and one
division guard, not decoding.

## What could not be established

- The fault address itself was not in the run record this slice was given
  (registers and two stack slots only; the Linux handler did not log `siginfo`).
  The instruction, operand and address above are derived statically, and they
  account for every recorded field — both registers that carry information
  (`ecx`, `edi`), the five zeroed ones, the stack depth, and the absence of
  any converter return address — but they have not been exercised at runtime
  here. The orchestrator's siginfo run is the check: it should report a write
  to guest `0xE8BC88` before the codegen fix, and no fault after it. If it
  reports a different address, this section, not the run, is wrong.
- Which `.bik` plays first, and therefore the movie's exact frame rate
  (25 vs 30 fps), was not established from the caller at `0x13AD3`; it does
  not affect anything above, since all four movies are 640x480 with the same
  header shape.
- Whether any other guest code in this image executes a segment-register
  push/pop pair (and so has been silently corrupting its stack the same way)
  was not audited image-wide. `push ds/es` is rare in this code — the Bink
  CPUID probe is the only instance found in the movie path — but a full audit
  belongs with the codegen fix, not with this title.
