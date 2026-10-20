# D3D8 core symbols — Fuzion Frenzy (USA), XDK 3925

Device lifecycle, push buffer, fence, draw, resource and shader half of the `D3D` section
(`0x1F5340-0x206970`) of `out/ff.exe`. The render-state / texture-stage / transform / viewport /
light / material / gamma family is `D3DStateFns`' (`docs/d3d8-state-symbols.md`).
Machine-readable result: `config/found-d3d8-core.toml`.

## Result

**30 of 48 names proven and emitted, plus 1 proven but held back. Neither extra name exists here.**

> **`D3DDevice_Reset` and `D3DDevice_InsertCallback` are NOT in this image.** No body of either shape
> exists, and the title never calls one. Neither the sibling's boot crash (guest `Reset`) nor its
> deadlock (guest `InsertCallback`) can therefore occur in this title. Evidence is in "Not emitted"
> below.

> **`D3DDevice_Swap` does not exist in XDK 3925. The title presents through
> `D3DDevice_Present` at `0x202150`.** Its signature is `stdcall`, 4 arguments, `retn 10h`. The host has
> no `Present` body, and the guest body cannot run on a host-created device (see "Present").
> **The host needs an `HLE_STDCALL(D3DDevice_Present, 4)` that does what its `Swap` does.**
> This is the next boot blocker after `Direct3D_CreateDevice`.

> `D3DDevice_GetBackBuffer2` and `D3DDevice_GetDepthStencilSurface2` do not exist either. 3925 has
> the older out-pointer forms with different signatures: `GetBackBuffer(BackBuffer, Type, ppSurface)`
> (`retn 0Ch`, `0x1F58A0`) and `GetDepthStencilSurface(ppSurface)` (`retn 4`, `0x1F5B40`). Neither may
> be wired to the `...2` host bodies.

Other findings the integrator needs:

- **The 3925 render-state and texture-stage numbering differs from 5849's**, so I held back
  `D3DDevice_SetRenderStateNotInline` (see "Held back"). `D3DStateFns` independently mapped the full
  rotation.
- The `CDevice` layout for this build is at the end of this document (as requested). It includes the
  five `[data]` globals that the host `Direct3D_CreateDevice` currently aborts on.
- **The host `Direct3D_CreateDevice` will still trap on `CDevice_BackBuffer` and
  `CDevice_FrontBuffer`.** Its `XDK_REQUIRE` asks for both, but 3925 has no pointer fields for them:
  its frame buffers are surfaces embedded in `CDevice`. **Do not point those keys at `+0x2150`/`+0x2168`.**
  The host writes a pointer into each, which would overwrite the embedded surface's `Common`
  dword. The title stores the surfaces from `GetBackBuffer` and later binds them with
  `SetRenderTarget` (`0x4537F`), so a 3925-ABI `GetBackBuffer` host body is needed as well.
- `Direct3D_CreateDevice = 0x1F7650` was reached independently. It agrees with
  `config/found-boot.toml`; there is no disagreement.

## The name set I adopted

From `docs/symbol-targets.json` section `D3D` (100 rows), I took every row that does **not** start with
one of the prefixes in `D3DStateFns`' brief. Those prefixes are `D3DDevice_SetRenderState_`,
`_SetTextureStageState`, `_SetTransform`, `_GetTransform`, `_SetTexture`, `_SetPalette`, `_SetViewport`,
`_GetViewport`, `_SetScissors`, `_SetScreenSpaceOffset`, `_SetVertexData`, `_SetVertexShaderConstant`,
`_SetStreamSource`, `_SetIndices`, `_SetMaterial`, `_SetLight`, `_LightEnable`, `_SetFlickerFilter`,
`_SetSoftDisplayFilter` and `_SetGammaRamp`. That leaves 48 names:

`CDevice_KickOff`, `D3DDevice_Begin`, `D3DDevice_BlockOnFence`, `D3DDevice_BlockUntilVerticalBlank`,
`D3DDevice_Clear`, `D3DDevice_CopyRects`, `D3DDevice_CreatePixelShader`,
`D3DDevice_CreateVertexShader`, `D3DDevice_DeletePixelShader`, `D3DDevice_DeleteVertexShader`,
`D3DDevice_DrawIndexedVertices`, `D3DDevice_DrawVertices`, `D3DDevice_DrawVerticesUP`,
`D3DDevice_EnableOverlay`, `D3DDevice_End`, `D3DDevice_FlushVertexCache`, `D3DDevice_GetBackBuffer2`,
`D3DDevice_GetDepthStencilSurface2`, `D3DDevice_GetRenderTarget2`,
`D3DDevice_GetVertexShaderFunction`, `D3DDevice_InsertFence`, `D3DDevice_IsFencePending`,
`D3DDevice_LoadVertexShader`, `D3DDevice_MakeSpace`, `D3DDevice_PersistDisplay`, `D3DDevice_Release`,
`D3DDevice_SelectVertexShader`, `D3DDevice_SetPixelShader`, `D3DDevice_SetPixelShaderConstant`,
`D3DDevice_SetRenderStateNotInline` (coordinated with `D3DStateFns`, who left it to me),
`D3DDevice_SetRenderTarget`, `D3DDevice_SetShaderConstantMode`, `D3DDevice_SetTile`,
`D3DDevice_SetVertexShader`, `D3DDevice_Swap`, `D3DDevice_SwitchTexture`,
`D3DResource_BlockUntilNotBusy`, `D3DResource_IsBusy`, `D3D_BlockOnResource`, `D3D_BlockOnTime`,
`D3D_InitDefaultState`, `D3D_InternalAddRef`, `D3D_InternalRelease`, `D3D_KickOffAndWaitForIdle`,
`D3D_MakeRequestedSpace`, `D3D_SetFence`, `Direct3D_CreateDevice`, `Lock2DSurface`.

I also took the two extras from the brief, which are not in the JSON: `D3DDevice_Reset` and
`D3DDevice_InsertCallback`. `D3DDevice_BeginPush`/`EndPush` sit in the JSON's `(none)` section and
belong to `XapiCrtFns`; it found neither, and I agree.

## Method and conventions

- `out/sims5558.exe` (XDK 5558, names applied) was the reference body for every name.
- The candidate in `out/ff.exe` was read in full and compared with it instruction by instruction:
  NV2A method tokens, stack bytes popped, register inputs, call sequence and kernel imports. Kernel
  imports were resolved from the thunk table at `0x253360` by ordinal: `0x9F` KeWaitForSingleObject,
  `0x91` KeSetEvent, `0x77` KeInsertQueueDpc, `0x01/02/03/04` Av{Get,Send,SetDisplay,Set}…,
  `0xA6` MmAllocateContiguousMemoryEx, `0xAB` MmFreeContiguousMemory, `0xB2` MmPersistContiguousMemory,
  `0xB4` MmQueryAllocationSize.
- **Call-site proof from the title.** `bStartDisplay` (`0x42020`) and the black-screen routine
  (`0x42BE0`) carry debug strings and call the device API with literal arguments, for example
  `"CreateDevice failed (%s)"` and `"GetDepthStencilBuffer failed"`. They name several entries
  directly.
- **Call-sequence proof from the library.** The 3925 device init `CDevice_Init` (`0x1FBB00`, tail at
  `0x1FBF44`) runs the same sequence as 5558's (`0x2504F0`): `SetVertexShader`, then
  `SetRenderTarget(back buffer, auto depth)`, then `D3D_InitDefaultState`, then
  `Clear(0, NULL, 3, 0, 1.0f, 0)`, then the kernel-debugger block. That pins four names by position
  alone, independently of their bodies.
- 3925 differs structurally from 5558 in places that matter: `CDevice` is `0x2B90` bytes, the dirty
  flags live at `CDevice+8`, the state flags at `+0xC` and the fence clock at `+0x1C`, and the frame
  buffers are surfaces embedded in the device. Field offsets are therefore never transplanted; each
  one is read from this image.
- I renamed the proven functions in the shared `ff.exe` IDA database for navigation. The 3925-only
  forms carry a `_3925` suffix, for example `D3DDevice_Present_3925`.

## Emitted names: evidence

### Direct3D_CreateDevice = 0x1F7650
`retn 18h` (24, matching 5849/5558).
- The body is identical in shape to 5558 `0x2443C0`. It sets the push-buffer size default `0x80000`
  (`0x206958`) and the kick-off size default `0x8000` (`0x206954`), points `D3D_pDevice` `0x206720`
  (and the alias `0x206724`) at the static device `0x203B90`, and sets the created flag `0x203F98`.
  It ors `BehaviorFlags & 0x10` into `[0x203B9C]`, calls `CDevice_Init` `0x1FBB00(&device, pPP)`, and
  on failure runs `CDevice_UnInit` `0x1FC010`.
- Title call: `0x421F0` in `bStartDisplay`. The next string is `"bStartDisplay: CreateDevice failed (%s)"`.
- **Confidence: certain.** It matches `config/found-boot.toml`.

### D3DDevice_Release = 0x1F54D0
Plain `retn`; 55 bytes, matching 5558/5849.
- If the reference count `[device+0x408]` is 1, it calls `CDevice_UnInit` `0x1FC010` (the cleanup
  `Direct3D_CreateDevice` uses), clears `0x206720`/`0x206724` and zeroes `0xAE4` dwords of the device
  (`0x1F54EB`). Otherwise it decrements the count and returns it. 5558 `0x245370` is identical, with
  `[+0x938]` and `0x928` dwords.
- Title calls: the `while (Release() != 0)` loop at `0x42266`, after `"GetDepthStencilBuffer failed"`,
  and the tail jump at `0x42C67`.
- **Confidence: certain.**

### D3DDevice_BlockUntilVerticalBlank = 0x1F5520
Plain `retn`; 36 bytes, matching 5558/5849.
- It clears the event's `SignalState` (`[device+0x24F4]`), then calls
  `KeWaitForSingleObject(device+0x24F0, 6, 1, 0, 0)` (ordinal `0x9F`). 5558 `0x245490` is the same
  with `0x1DBC`.
- The vertical-blank handler `0x1FD7FC` signals `miniport+0x1E8`, which is the same `device+0x24F0`.
- Called by `D3DDevice_Present` `0x202212` while it waits for a mode change.
- **Confidence: certain.**

### D3DDevice_InsertFence = 0x1F5550
Plain `retn`; 8 bytes, matching 5558/5849.
- The body is `push 0; call D3D_SetFence; retn`. In 5558 the only function of that exact shape is
  `D3DDevice_InsertFence` `0x245790`; I checked the other `D3D_SetFence` callers `0x248C00`,
  `0x24D9C0`, `0x2453E0` and `0x2495B0`.
- Four title calls: `0xD3042`, `0xD3282`, `0x13983B` and `0x1B27BC`. Each comes right after
  `SetStreamSource(0, NULL, …)` (`0x1F8000`); `0x13983B` also follows `SetIndices(NULL, 0)`. Three of
  the four are tail jumps.
- **Confidence: high.**

### D3DDevice_SetRenderTarget = 0x1F5640
`retn 8`; 605 bytes, against 628 in 5849.
- A NULL target keeps `[device+0x40C]` (`0x1F5655`).
- It AddRefs the new target, stamps the old one with the clock, releases it, and stores the new
  target at `0x1F572A`. It does the same for the depth surface at `[device+0x410]` (`0x1F574F`).
- It writes the depth-format maximum into `&[device+0x4F8]` via `0x1FCCB0`: `65535.0f` for D16,
  `16777215.0f` for D24S8, `511.9375f` for F16 and `1e30f` for F24S8.
- It pushes `0xC020C` (SURFACE_PITCH, COLOR_OFFSET, ZETA_OFFSET) and `0x80200` (SURFACE_CLIP_H/V),
  then resets the viewport through `SetViewport` with `{0, 0, W, H, 0, 1.0f}`.
- 5558's has the same structure, with `k` holding `0x948`, `0x1A04` and `0x1A08` as the
  ZScale/target/depth counterparts.
- Callers: `CDevice_Init` at `0x1FBF78`, in the init sequence above; title `sub_45330` (3×); and the
  MultiSampleType state handler `0x1F660D`.
- **Confidence: certain.**

### D3DDevice_Clear = 0x1FC1B0
`retn 18h`; 894 bytes, against 927 in 5849.
- **Title call pattern:** `Clear(0, NULL, 0xF3, 0, 1.0f, 0)` at `0x42C51`, `0x42478` and `0x42494`,
  each followed by `Present`; the string `"Dummy present took %f ms"` follows.
- `CDevice_Init` calls `Clear(0, NULL, 3, 0, 1.0f, 0)` at `0x1FBF91`, exactly where 5558's init does
  (`0x250530`).
- For each rect it pushes `0x81D98` (SET_CLEAR_RECT_HORIZONTAL/VERTICAL) and then `0xC1D8C`
  (ZSTENCIL_CLEAR_VALUE, COLOR_CLEAR_VALUE = arg 4, CLEAR_SURFACE = arg 3), at `0x1FC4E0`/`0x1FC4E6`.
- Rects are scaled by `[device+0x500]`/`+0x504`. With no rects it uses the viewport at `+0xB00`.
- **Confidence: certain.**

### D3DDevice_CopyRects = 0x1F58E0
`retn 14h` (5 arguments).
- `D3DDevice_PersistDisplay` calls it at `0x1F6548` with
  `(front buffer, NULL, 0, persist surface, NULL)`. That is the exact position and argument shape of
  5558 `PersistDisplay`'s `call D3DDevice_CopyRects` (`0x248080`).
- The prologue matches 5558 `0x2467D0`:
  - the format byte `[src+0Dh]` is looked up in the format table and gives bytes per pixel;
  - the sizes of source and destination (argument 4) are taken;
  - `cRects` (default 1) is processed in batches of 16, through the blit pusher `0x1FDE70`
    (image-blit methods `0xC6300`/`0x108300`);
  - both resources are stamped with `[device+0x1C]`.
- **Confidence: high.**

### D3DDevice_PersistDisplay = 0x1F6490
Plain `retn`; 289 bytes, against 302. The sequence is step for step the same as 5558 `0x247FC0`:
1. `AvGetSavedDataAddress`; if set, `MmFreeContiguousMemory` and `AvSetSavedDataAddress(0)`.
2. Return `E_FAIL` (`0x80004005`) if `[device+0x408]` is 0.
3. Spin on the miniport flip queue (`0x1FD630`).
4. Create the persist surface from the front buffer (`0x1F7910`), then `CopyRects`, then
   `D3D_KickOffAndWaitForIdle` (`0x1F654D`).
5. Loop `AvSetDisplayMode` until it returns 0.
6. `MmQueryAllocationSize`, `MmPersistContiguousMemory(…, TRUE)`, `AvSetSavedDataAddress`.

Title calls: `0x184C6` and `0x1CCDD`. **Confidence: certain.**

### CDevice_KickOff = 0x1FAED0
`ecx` = device, plain `retn`, matching 5849's `regs=[ecx]` and `ret=0`. It has the same two paths as
5558 `0x24D120`:
- **Hardware:** take the push pointer from `[device+0x400]` if flag bit 4 is set, otherwise from
  `[device]`. Then `sfence`, set bit `0x10000` of NV2A register `+0x100410` (miniport base
  `[device+0x2308]`) and spin. Write PUT `& 0x3FFFFFF` to `[[device+0x2304]+0x40]` and store the
  pointer at `[device+0x18]`.
- **No-hardware** (global `0x203748`): fake GET and set `[[device+0x3F0]] = clock - 2` and
  `[device+0x2518] = [device+0x2B60]`, as 5558 does with `+0x30`, `+0x1DE4` and `+0x2478`.

Callers: `D3D_SetFence`, `D3DDevice_MakeSpace` (2×), `CDevice_Init`, `CDevice_UnInit` and `Present`.
**Confidence: certain.**

### D3D_SetFence = 0x1FAF80
`retn 4`.
- It pushes `0x41D70` (BACK_END_WRITE_SEMAPHORE_RELEASE) with the clock, then `0x41D90`, 0 twice.
- It records the clock and push position in the 64-entry ring at
  `device + ((t>>1)&0x3F)*12 + 0xE4`; flag 1 also records them in the 16-entry ring indexed by
  `[device+0x20]`.
- Flag 2 skips `CDevice_KickOff`.
- It adds 2 to `[device+0x1C]` (`0x1FB00D`) and returns the old value.
- This is the 5558 `0x24D240` algorithm. 5558 additionally pushes `0x4A310`.
- Flag values at the callers: InsertFence 0, BlockOnTime 0, MakeSpace 1, DrawVerticesUP and
  DrawIndexedVertices 1, Present 2.
- **Confidence: certain.**

### D3D_BlockOnTime = 0x1FB020
`retn 8`. The algorithm is 5558 `0x24D300`'s:
1. Return if the time has already completed (`[device+0x1C]` against `[[device+0x3F0]]`).
2. If the time is the current clock, call `SetFence(0)`.
3. Find the ring entry (`0x1FAE20`) and measure the distance to GET (`0x1FADB0`).
4. If it is `>= 0x8000`, patch the entry with `0x40110` (wait for idle; skipped when argument 2 is
   nonzero) and `0x40100, 0x310`, then poke the GPU (`0x1FAEA0`).
5. Then either busy-wait on the clock, or `KeWaitForSingleObject(device+0x2500)`.

Callers: KickOffAndWaitForIdle, BlockOnResource (3×) and MakeSpace. **Confidence: certain.**

### D3DDevice_MakeSpace = 0x1FB110 — read this note
Plain `retn`, no stack arguments, returns the new push cursor in `eax` on every path
(`0x1FB2D6`, `0x1FB2F6`, `0x1FB13C`).
- **Identity by role.** Every 3925 pusher reaches it when `[device] >= [device+4]`: the helper
  `0x1FB3B0` (74 callers), the reserve helper `0x1FB3D0` (7 callers) and
  `D3DDevice_SetRenderState_Simple` `0x1F6650`. 5558's corresponding callers call
  `D3DDevice_MakeSpace` at the same points, and the 5558 `SetRenderState_Simple` `0x2498E0` is
  byte-for-byte the same function apart from the 6-byte `mov ecx, [D3D_pDevice]` before the call.
- **Not a pure 1:1 match.** 5849 splits the work into `D3DDevice_MakeSpace`, a 15-byte stdcall
  wrapper with `regs=[]`, and `D3D_MakeRequestedSpace(Min, Requested)`. 3925 has a single **thiscall**
  body with `ecx` = device that reads the same two size globals (`0x206954`, `0x206958`).
- **Why it is still safe to wire.** Both forms pop nothing and return the cursor. The host body
  (`HLE_STDCALL(D3DDevice_MakeSpace, 0)` → `ScratchPush()`) ignores `ecx`, and `ecx` is always
  `D3D_pDevice` here. All three call sites save or discard `ecx` themselves.
- **Why it matters.** Without it, the guest body dereferences `[device+0x2304]` (the DMA control
  pointer, 0 on a host-created device) the first time any guest pusher runs out of room. That is the
  same class of NV2A crash as the boot crash.
- **Confidence: high** for identity. The thiscall/stdcall difference is the only exception to
  "`regs` must match" in this file.

### D3D_KickOffAndWaitForIdle = 0x1FB300
Plain `retn`; 17 bytes, matching 5558/5849.
- The body is `BlockOnTime([device+0x1C], 0)`. 5558's pushes flag 2 instead.
- Callers sit at the 5558 positions: `PersistDisplay` `0x1F654D`, `CDevice_UnInit` `0x1FC01A` (5558
  `0x2505BA`) and `Present` `0x2021FD`. There is also the 5-byte thunk `0x1F5510`
  (`jmp D3D_KickOffAndWaitForIdle`, 7 title calls), which matches 5558 `0x2453B0`. [inference] It is
  presumably later XDKs' `D3DDevice_BlockUntilIdle`; it is not in the target set and I did not name it.
- **Confidence: certain.**

### D3D_BlockOnResource = 0x1FB320
`retn 4`; 129 bytes, against 120. The structure is 5558 `0x24D630`'s:
- return if there is no device;
- for a surface `(Common & 0x70000) == 0x50000` with a parent `[+0x14]`: if the in-use test
  `0x1FC740` (5558 `0x244970`) finds the device still holding it, call `BlockOnTime(current clock, 0)`;
  otherwise continue with the parent texture;
- for the resource: if the device still holds it, call `BlockOnTime(current clock, 0)`, otherwise
  `BlockOnTime(fence [+8], 0)`; then clear the fence. 5558 does not clear it.

`Lock2DSurface` calls it at `0x1FD171`, exactly where 5558 `Lock2DSurface` calls
`D3D_BlockOnResource`. **Confidence: certain.**

### D3DResource_BlockUntilNotBusy = 0x1FC680
The 5-byte thunk `jmp D3D_BlockOnResource`, the same as 5558 `0x244BF0`; 5849 records `ret=None` and
`len=5`. Title call: `0x3F6B4`. **Confidence: certain.**

### D3D_InitDefaultState = 0x1FB580
Plain `retn`, no register inputs. It is called by `CDevice_Init` at `0x1FBF7D`, between
`SetRenderTarget` and `Clear`, exactly as in 5558 (`0x25051C`). The body is 5558 `0x24F940`'s:
1. `SetTransform(i, identity)` for 10 transform types.
2. A sweep of render states from `0x39` with one skip, through `SetRenderStateNotInline` `0x1F66A0`.
3. `ZEnable := [device+0x2198] != 0`. That is render state `0x7B` here and `0x8F` in 5558.
4. For each of 4 stages, 32 texture-stage states from a byte table, plus `(stage, 0x1C, stage)`.
5. The stage-0 colour/alpha op defaults.

Note: every state number it passes is 3925-numbered. **Confidence: certain.**

### D3D_InternalAddRef = 0x1FC780, D3D_InternalRelease = 0x1FC910, D3DResource_IsBusy = 0x1FC850
These were already in `config/game.toml`; I re-verified them and confirm all three.
- `InternalAddRef` adds `0x80000` to `Common`, and to the surface's parent when its internal count
  was 0.
- `InternalRelease` is 98 bytes, the same as 5558 `0x244DE0`, and calls the resource destroyer
  `0x1FC6A0`.
- `IsBusy` is identical to 5558 `0x244B10` apart from the clock fields, which are `+0x1C`/`+0x3F0`
  here and `+0x2C`/`+0x30` in 5558.

**Confidence: certain.**

### Lock2DSurface = 0x1FD160
`retn 18h`; **155 bytes, matching 5558 `0x250FC0` instruction for instruction.**
- `Flags & 0x20` skips `D3D_BlockOnResource`; `Flags & 0x40` ors `0xF0000000`.
- It uses the same level-info helper call and the same pitch arithmetic over the format table.
- Callers: the `D3DTexture_LockRect` wrapper `0x1F7810` and the `D3DSurface_LockRect` wrapper
  `0x1F7860`.

**Confidence: certain.**

### D3DDevice_CreateVertexShader = 0x1F7EC0
`retn 10h`; **318 bytes, matching 5849/5558.** It parallels 5558 `0x24B8B0` throughout:
- the declaration size is taken via `0x1F7DC0`;
- it allocates, returning `0x8007000E` on failure, and clears the object;
- it sets `[obj] = 1` and `[obj+4] = Usage`, plus the function-header checks `0x2078`/`0x7378`;
- it copies the program to `obj+0x168` (5558 uses `+0x114`) and stores its length at `obj+0x14`;
- the handle is `obj | 1`.

Title call: `0x3E038`. **Confidence: certain.**

### D3DDevice_DeleteVertexShader = 0x1F82A0
`retn 4`; 25 bytes, against 27. The body is `obj = handle - 1; if (--obj->ref == 0) free(obj)`, the
same as 5558 `0x24C2E0`; 3925 tail-jumps into the allocator's free with no tag. Title calls: `0x3DE59`
and `0x3DEE9`. **Confidence: high.**

### D3DDevice_LoadVertexShader = 0x1F8090
`retn 8`; **84 bytes, matching 5558.** It is the same as 5558 `0x24BFC0`:
- it tests the device flag `0x10` (`[device+0xC]` here, `+8` in 5558);
- it copies to `0x41E9C` (SET_TRANSFORM_PROGRAM_LOAD) = Address, then `[obj+0x14]` program dwords from
  `obj+0x168`.

It is reached from `SetVertexShader` `0x1F841C`, just as 5558's `SetVertexShader` calls
`LoadVertexShader`. **Confidence: certain.**

### D3DDevice_SelectVertexShader = 0x1F80F0
`retn 8`. It has the 5558 `0x24C090` shape:
1. If there is a handle, store the object/handle at `[device+0x470]`/`+0x474`, push `0x81E94, 6`
   and the viewport.
2. Push `0x41EA0` (SET_TRANSFORM_PROGRAM_START) = Address.

`SetVertexShader` calls it right after `LoadVertexShader` (`0x1F8424`), as 5558's does.
**Confidence: high.**

### D3DDevice_SetVertexShader = 0x1F83C0
`retn 4`.
- An odd handle is a program object; an even one is an FVF, built into the static `0x2067E8` (as 5558
  builds into `0x266FC8`).
- It stores `[device+0x470]`/`+0x474`.
- A program object with flag bit 4 goes through `Load` and `Select`, as in 5558 `0x24C3F0`.
  Otherwise it pushes `0x41EA0`/`0x81E94` or `0x41E94`.

Callers: `CDevice_Init` `0x1FBF54` (`SetVertexShader(D3DFVF_XYZ)`, which is also 5558's init order)
and 7 title calls. **Confidence: certain.**

### D3DDevice_SetShaderConstantMode = 0x1F8160
`retn 4`. It is the same as 5558 `0x24C180`:
1. `mode & 0x10` toggles device flag `0x200`, and the mode is stored at `[device+0x2140]`.
2. Mode 0 pushes `0x41EA4, 0x3C, 0x300B80` and 12 dwords, the four fixed-function matrices
   (`0x400840`, `0x400880`, `0x4008C0`, `0x400900`) from the identity at `0x2026E0`, then
   `0x1009D0, 0, 0, 1.0f, 0`.

Title calls: `0x3CF2F` and `0x3D0B6`. **Confidence: certain.**

### D3DDevice_SetPixelShader = 0x202320
`retn 4`. The token sequence is identical to 5558 `0x24E740`:
- **NULL handle:** call the TextureFactor setter `0x1F68C0` (5558 calls
  `SetRenderState_TextureFactor` here), then push the default combiner and `0x41E78, 0x210000`.
- **Otherwise:** `def = [handle+8]`. Push `0x200260` + 8 dwords, `0x800A60` + 32, `0x417F8`,
  `0x81E20` + 2, `0x241E40` + 9 and `0x81E74` + 2, plus optionally `0x80288` + 2. Unless flag `0x10`
  is set, copy `0x39` dwords of the definition into `D3D_RenderState` and `0x203B20`.

The handle is stored at `[device+0x414]`. The title never calls it directly; only `Present`'s
filter path (`0x201860`, `0x201AC0`) does. **Confidence: certain.**

### D3DDevice_DrawVerticesUP = 0x201390
`retn 10h`. It parallels 5558 `0x24D9E0` throughout:
1. State sync `0x1FA000`.
2. `0x417FC` (BEGIN_END) with the primitive; flags or `0x800`.
3. Batches of 16, or `0x7FF / stride`, through the reserve helper with `0x40001818 + (dwords << 18)`
   (inline array), copying the vertices.
4. `0x417FC, 0`; `SetFence(1)` if flag `0x1000`; flags and `0xFFFFE7FF`.

Seven title calls. **Confidence: certain.**

### D3DDevice_DrawVertices = 0x2014D0, D3DDevice_DrawIndexedVertices = 0x201570
These were already in `config/game.toml`; I re-verified and confirm both.
- `DrawVertices` is the 5558 body: `0x417FC`, then `0x40001810` with 256-vertex runs.
- `DrawIndexedVertices` reads `BaseVertex` `[device+0x478]` and ends with `SetFence(1)` under flag
  `0x1000`.

**Confidence: certain.**

## Held back (proven, not emitted)

### D3DDevice_SetRenderStateNotInline = 0x1F66A0 — commented out in the TOML
`retn 8`; 90 bytes, against 93. It has the exact shape of 5558 `0x249910`:
- for `state < 0x52`, call `SetRenderState_Simple(encode[state] (0x253520), value)` and store
  `D3D_RenderState[state]` (`0x203950`);
- for `state < 0x74`, or the dirty mask `0x2024E8[state]` into `0x203B98` and store;
- otherwise call `[0x2023F0 + state*4]`, the 28 complex setters `0x2025C0`…

`D3D_InitDefaultState` calls it exactly where 5558's calls `SetRenderStateNotInline`.

It is held back because `sdk/runtime/xdk/d3d8.cpp` treats `state < gpu::kRsDeferredFirst`, the 5849
bound, as simple. 3925's simple range ends at `0x52`, its deferred range at `0x74`, and its complex
range runs `0x74-0x8F`. For states `0x52-0x5B` the host would read past the 82-entry encode table,
and it would file deferred and complex states under 5849 numbers.

Left at 0, the guest body routes 3925 numbers through 3925's own tables into the value-only
`SetRenderState_Simple` and complex-setter host bodies, which is correct. Merge it only once the host
translates 3925 numbers.

## Not emitted

| Name | Why |
| --- | --- |
| **D3DDevice_Reset** (extra) | **Absent.** No function calls `D3D_KickOffAndWaitForIdle` then `SetRenderTarget` and `Clear` with `retn 4`. The only callers of `0x1FB300` are the `BlockUntilIdle` thunk, `PersistDisplay`, `CDevice_UnInit` and `Present`. The title creates the device once (`bStartDisplay`) and never resets it, so the dropped function cannot be the boot crash here. |
| **D3DDevice_InsertCallback** (extra) | **Absent.** The image has exactly two `0x81D8C` stores (`0x1F72B3`, `0x1F72D9`), both inside `0x1F7230`, a `retn`-0 render-state helper that uses the NOP software methods `0x31C`/`0x320` to poke PGRAPH registers. The other `0x40100` writers are `BlockOnTime` and `Present`'s flip paths. No `retn 0Ch` writer of (callback, context) exists, and the device-section gaps hold only switch tables. The title never inserts callbacks. |
| D3DDevice_Swap | **Absent in XDK 3925.** See "Present" below; the 4-argument `D3DDevice_Present` at `0x202150` is its counterpart. |
| D3DDevice_GetBackBuffer2 | Absent. 3925 has `GetBackBuffer(BackBuffer, Type, ppSurface)`, `retn 0Ch`, at `0x1F58A0`. The title calls it as `(0, 0, &p)` and `(-1, 0, &p)` at `0x42236`/`0x42296`. It returns **embedded** surfaces at `device + 0x2150 + index*0x18` (index 0, 1 = front for −1, 2). A different ABI: do not wire it to the `...2` body. |
| D3DDevice_GetDepthStencilSurface2 | Absent. 3925 has `GetDepthStencilSurface(ppSurface)`, `retn 4`, at `0x1F5B40`, called at `0x42240`. It writes `[device+0x410]` and AddRefs it, returning `0x88760866` if NULL. A different ABI. Note that the guest body is harmless once the host keeps `CDevice_Depth` (`+0x410`) filled. |
| D3DDevice_GetRenderTarget2 | Absent. No reader of `[device+0x40C]` is a getter. The readers are SetRenderTarget, SetViewport, the state handlers `0x1F65F0`/`0x1F6970`, the viewport push `0x1F6EB0`, `0x1F70E0`, `0x1F82C0`, the frame-buffer teardown `0x1FB640`, Clear, the init `0x20057A`, and Present's helpers `0x201930`/`0x201BE0`/`0x202060`. |
| D3DDevice_Begin, D3DDevice_End | Absent. `0x417FC` is written only by DrawVerticesUP, DrawVertices, DrawIndexedVertices and Present's filter quad `0x201BE0`. |
| D3DDevice_BlockOnFence, D3DDevice_IsFencePending | Absent. `D3D_BlockOnTime`'s callers are MakeSpace, KickOffAndWaitForIdle and BlockOnResource. The readers of the completed-clock pointer `[device+0x3F0]` are KickOff, BlockOnTime, MakeSpace, the init routines and IsBusy. |
| D3DDevice_CreatePixelShader, D3DDevice_DeletePixelShader, D3DDevice_SetPixelShaderConstant | Absent. No allocator of pixel-shader objects exists. `0xA60` (COMBINER_FACTOR) is pushed only by `SetPixelShader` and the TextureFactor setter. |
| D3DDevice_GetVertexShaderFunction | Absent. The `0x2078`/`0x7378` header tokens appear only in CreateVertexShader, and nothing copies a program back out. |
| D3DDevice_EnableOverlay | Absent. Nothing touches the overlay registers that 5558's `0x259700` drives. |
| D3DDevice_FlushVertexCache | Absent as a function. `0x41710` is pushed only inline by `D3DVertexBuffer_Lock` `0x201810`. |
| D3DDevice_SwitchTexture | Absent. The only direct `[0x203B90]` pusher is `SetRenderState_Simple`; no fastcall 3-dword pusher exists. |
| D3D_MakeRequestedSpace | No 3925 counterpart with its ABI. The (Min, Requested) body is merged into the thiscall `MakeSpace` `0x1FB110`. `0x1FB3D0` (`retn 8`) takes (device, count) and is `XapiCrtFns`' to leave unnamed. |
| D3DDevice_SetTile | Not proven. 5849's is `call D3D_KickOffAndWaitForIdle; jmp SetTileNoWait`; no such wrapper exists here. `0x1F6240` (Index, pTile, `retn 8`) is the no-wait body, called only by the frame-buffer init and teardown and never by the title. I cannot show that the exported 3925 name is `SetTile`. |

## Present — what the host must replace

`0x202150`, `stdcall(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion)`, `retn 10h`. The title
calls it at `0x41F3F`, `0x42481`, `0x4249D` and `0x42C5E`, always with four zeros. Its steps:
1. `SetFence(2)`.
2. Spin until `[device+0x2B60] − [device+0x2518] < 2`.
3. `++[device+0x2B60]`, the swap count.
4. Flip through `0x202060`, which rotates the data pointers of the embedded frame-buffer surfaces at
   `+0x2150` and pushes `0x4012C/0x40100, 0x300/0x304` flip software methods. With multisampling it
   goes through `0x201E80` instead, a filter blit that calls `SetPixelShader` and
   `SetRenderStateNotInline`.
5. `CDevice_KickOff`, then stamp the front buffer.
6. If a mode change is pending (`[device+0x2514]`), apply it: `D3D_KickOffAndWaitForIdle`,
   `BlockUntilVerticalBlank`, `AvSetDisplayMode` in a loop, `AvSendTVEncoderOption`, and dropping the
   persisted display.

On a host-created device the guest body cannot work. `[device+0x214C]` (the buffer count) is 0, so the
rotation loop in `0x202060` starts at −1. A host `D3DDevice_Present` should do what `D3DDevice_Swap(0)`
does and ignore its four arguments. The host's `D3DDevice_Release` returns 1 forever, so the title's
failure-path loop at `0x42266` would spin. That path is only reachable if `GetDepthStencilSurface`
fails.

## The title's D3D call surface

These are all `D3D`-section entries called from game code. The game never touches `D3D_pDevice`,
`D3D_Device` or the push buffer directly; the one exception is a read of `D3D_IndexData` at
`0x1397E9`. The names in rows marked "not targeted" are identifications by shape only, not proofs.

| Address | Entry (3925) | retn | calls | status |
| --- | --- | --- | --- | --- |
| 0x1F5340 | D3DDevice_GetDeviceCaps(pCaps) | 4 | 2 | not targeted, benign |
| 0x1F5360 / 0x1F5470 / 0x1F54A0 | SetTransform / GetTransform / SetMaterial | 8/8/4 | 27/2/3 | D3DStateFns |
| 0x1F54D0 | D3DDevice_Release | 0 | 3 | emitted |
| 0x1F5510 | BlockUntilIdle thunk → KickOffAndWaitForIdle | — | 7 | covered by emitted target |
| 0x1F5550 | D3DDevice_InsertFence | 0 | 4 | emitted |
| 0x1F5640 | D3DDevice_SetRenderTarget | 8 | 3 | emitted |
| 0x1F58A0 | GetBackBuffer (3-arg 3925 form) | 0xC | 2 | **needs a 3925 host body** |
| 0x1F5B40 | GetDepthStencilSurface (1-arg 3925 form) | 4 | 1 | benign once `+0x410` is kept |
| 0x1F5B70, 0x1F5CD0, 0x1F5EB0, 0x1F5F90, 0x1F6170, 0x1F8000 | state setters (SetViewport, SetTexture, SetIndices, SetStreamSource and two `retn 8` setters) | — | — | D3DStateFns |
| 0x1F60F0 | SetPalette | 8 | 6 | XapiCrtFns |
| 0x1F6200 | GetDisplayFieldStatus | 4 | 2 | benign with the host's DisplayFlags |
| 0x1F6490 | D3DDevice_PersistDisplay | 0 | 2 | emitted |
| 0x1F6650 / 0x1F6680 / 0x1F6700 | Simple / deferred RS / deferred TSS setters (fastcall) | 0/0/4 | 82/25/291 | D3DStateFns |
| 0x1F7650 | Direct3D_CreateDevice | 0x18 | 2 | emitted |
| 0x1F77C0 | D3DTexture_GetSurfaceLevel(tex, level, pp) | 0xC | 1 | not targeted, benign |
| 0x1F7810 / 0x1F7860 | D3DTexture_LockRect / D3DSurface_LockRect → Lock2DSurface | 0x14/0x10 | 1/1 | covered by Lock2DSurface |
| 0x1F7840 | D3DSurface_GetDesc | 8 | 1 | not targeted, benign |
| 0x1F7EC0, 0x1F82A0, 0x1F83C0, 0x1F8160 | Create/Delete/SetVertexShader, SetShaderConstantMode | — | — | emitted |
| 0x1F84A0 | SetVertexShaderConstant (stdcall, 3 args) | 0xC | 29 | not the 5849 fastcall forms (D3DStateFns) |
| 0x1FC1B0 | D3DDevice_Clear | 0x18 | 7 | emitted |
| 0x1FC680 | D3DResource_BlockUntilNotBusy | — | 1 | emitted |
| 0x1FC800 / 0x1FC8E0 | D3DResource_Release / D3DResource_Register | 4/8 | 2/9 | not targeted, benign |
| 0x1FC850 | D3DResource_IsBusy | 4 | 1 | emitted |
| 0x201390, 0x2014D0, 0x201570 | DrawVerticesUP / DrawVertices / DrawIndexedVertices | — | 7/28/1 | emitted |
| 0x2017E0 / 0x201810 | buffer lock (res, ppData, flags) / D3DVertexBuffer_Lock | 0xC/0x14 | 1/1 | not targeted; call BlockOnResource |
| **0x202150** | **D3DDevice_Present (4 args)** | **0x10** | **4** | **no host body — must be added** |
| 0x2022E0 / 0x2022F0 | Direct3DCreate8 / Direct3D_GetDeviceCaps | 4/0xC | 2/1 | benign |

## CDevice layout for XDK 3925

The static device is `D3D_Device` `0x203B90`. `D3D_pDevice` `0x206720` points at it, and the embedded
miniport is `CDevice + 0x2308`. `CDevice_Init` passes `this + 0x2308` (`0x1FBBDC`) as `ecx` to the
miniport init `0x1FE540` (call at `0x1FBBE4`), and `PersistDisplay` `0x1F64CA` and the init's
debugger block `0x1FBFD7` take the same address. Each line
below is proven by this image at the address given. Lines starting with `#` have no 3925 equivalent,
and the reason is given on the line.

```toml
# D3D8 CDevice, the static device D3D__pDevice (0x206720) points at (D3D_Device 0x203B90)
CDevice_Push          = 0x0     # the push-room helper 0x1FB3B0 reads [device] at 0x1FB3B4; D3DDevice_SetRenderState_Simple 0x1F6650 advances [0x203B90]
CDevice_PushThreshold = 0x4     # 0x1FB3B6 compares the push pointer with [device + 4] and calls D3DDevice_MakeSpace 0x1FB110; 0x1F6658 compares with [0x203B94]
CDevice_BaseVertex    = 0x478   # D3DDevice_SetIndices 0x1F6170 stores BaseVertexIndex at 0x1F61C9/0x1F61DE/0x1F61F3; D3DDevice_DrawIndexedVertices 0x20157E reads it
CDevice_IndexBuffer   = 0x47C   # D3DDevice_SetIndices 0x1F619A reads the old one (released at 0x1F61B9), stores the new one at 0x1F61C2/0x1F61D7/0x1F61EC
CDevice_ZScale        = 0x4F8   # D3DDevice_SetRenderTarget 0x1F575D passes &[device + 0x4F8] to 0x1FCCB0, which stores the depth-format maximum (65535.0 D16, 16777215.0 D24S8, 0x1FCCC7/0x1FCCD4); the viewport push 0x1F6FB4/0x1F6FC4 scales MinZ/MaxZ by it
CDevice_SuperSampleX  = 0x500   # 0x1F70E0 (from SetRenderTarget) stores the X scale at 0x1F7199; the viewport push 0x1F6ECC/0x1F6F6F and D3DDevice_Clear 0x1FC432 scale X by it
CDevice_SuperSampleY  = 0x504   # 0x1F70E0 stores the Y scale at 0x1F719F; the viewport push 0x1F6EEE/0x1F6F95 and D3DDevice_Clear 0x1FC468 scale Y by it
CDevice_Transforms    = 0x880   # D3DDevice_SetTransform 0x1F5371: lea edi, [type + 22h]; shl edi, 6; add edi, device -> 16 dwords at +0x880 + type * 64 (0x1F5380); GetTransform 0x1F547A reads the same slot
CDevice_Textures      = 0xBA0   # D3DDevice_SetTexture 0x1F5F9E reads, 0x1F5FD3 stores [device + stage * 4 + 0xBA0]
CDevice_Palettes      = 0xBB0   # D3DDevice_SetPalette 0x1F60FD reads, 0x1F6135 stores [device + stage * 4 + 0xBB0]
CDevice_RenderTarget  = 0x40C   # D3DDevice_SetRenderTarget 0x1F5655 reads it for a NULL target, stores the new target at 0x1F572A; D3DDevice_Clear 0x1FC1C3 reads it
CDevice_Depth         = 0x410   # D3DDevice_SetRenderTarget 0x1F574F stores the depth surface; the 3925 GetDepthStencilSurface 0x1F5B40 returns [device + 0x410] (0x1F5B45)
CDevice_SwapCount     = 0x2B60  # D3DDevice_Present 0x20218C increments it; CDevice_KickOff's no-hardware path 0x1FAF69 copies it to the shown-flip count +0x2518 (5558 0x24D1C2: +0x2478 -> +0x1DE4); CDevice_Init 0x1FBFC3 publishes &[+0x2B60] in the debugger-block slot where 5558 0x250558 publishes &[+0x2478]
CDevice_Size          = 0x2B90  # D3DDevice_Release 0x1F54EB clears 0xAE4 dwords of the device; 0x203B90 + 0x2B90 = 0x206720 (D3D_pDevice, the next variable)
CDevice_Flags             = 0xC    # Direct3D_CreateDevice 0x1F7685 ors BehaviorFlags & 0x10 into [0x203B9C]; LoadVertexShader 0x1F80A2 tests 0x10, SetShaderConstantMode 0x1F8172 sets 0x200, DrawVerticesUP 0x2013D7 sets 0x800 and 0x2014B8 clears 0x1800, CDevice_KickOff 0x1FAEDE tests 4
CDevice_Time              = 0x1C   # D3D_KickOffAndWaitForIdle 0x1FB305 passes [device + 0x1C] to D3D_BlockOnTime; D3D_SetFence 0x1FB00D adds 2
CDevice_PixelShader       = 0x414  # D3DDevice_SetPixelShader 0x20232E reads the old handle, 0x202334 stores the new one
CDevice_VertexShader      = 0x470  # CDevice_Init 0x1FBF4A stores the FVF shader object 0x2067E8; D3DDevice_SetVertexShader 0x1F8406 stores the current one (handle at +0x474, 0x1F840C)
CDevice_Viewport          = 0xB00  # D3DDevice_SetViewport 0x1F5C6D to 0x1F5C91 stores X, Y, Width, Height, MinZ, MaxZ at +0xB00..+0xB14
CDevice_AutoDepth         = 0x2198 # the frame buffer init 0x1FB9A9 stores the auto depth buffer here, D3D_InitDefaultState 0x1FB5D0 tests it != 0 for ZEnable (state 0x7B); NOTE it holds the buffer MEMORY, the embedded depth surface is +0x219C (CDevice_Init 0x1FBF67)
CDevice_VBlankCallback    = 0x24EC # the vertical blank handler 0x1FD802 calls [miniport + 0x1E4] with the blank count BY VALUE (cdecl, 1 dword), not a pointer; nothing in this image stores it
CDevice_VBlankEvent       = 0x24F0 # D3DDevice_BlockUntilVerticalBlank 0x1F5537 waits on device + 0x24F0 and 0x1F552B clears its SignalState; the handler 0x1FD7FC signals miniport + 0x1E8
CDevice_DisplayFlags      = 0x2510 # GetDisplayFieldStatus 0x1F6212 tests [device + 0x2510] & 0x1200000; the mode set 0x1FD61D stores the mode entry's flags at miniport + 0x208
CDevice_VBlankCount       = 0x2B28 # the vertical blank handler 0x1FD715 increments miniport + 0x820; GetDisplayFieldStatus 0x1F6205 returns [device + 0x2B28] as the count
CDevice_GammaRamps        = 0x251C # the vertical blank handler 0x1FD78A loads the ramp at miniport + 0x214 + index * 0x300; 0x1FDF83 builds the identity ramps there
CDevice_GammaPending      = 0x2B1C # the vertical blank handler 0x1FD77A tests [miniport + 0x814 + index * 4] == 1 and clears it at 0x1FD799; the mode set 0x1FD613 sets it
# CDevice_BackBuffer        - no pointer field: back buffer 0 is an EMBEDDED 0x18-byte surface at +0x2150 (GetBackBuffer 0x1F58BE returns device + 0x2150 for index 0; back buffer 1 is +0x2180)
# CDevice_FrontBuffer       - no pointer field: the front buffer is the embedded surface at +0x2168 (GetBackBuffer index -1; PersistDisplay 0x1F64EA; CDevice_Init 0x1FBFD0 publishes &[+0x2168] where 5558 publishes the pointer [+0x1A18])
# CDevice_BackBufferScale   - no field: 3925 has no back-buffer scale, its only viewport scale pair is the super-sample pair +0x500/+0x504 (no other float pair is stored by the init)
# CDevice_ScreenSpaceOffset - no field: 0.53125 is the constant 0x2025A4 the viewport push adds (0x1F6EDA); there is no SetScreenSpaceOffset in 3925
# CDevice_Field             - no field: GetDisplayFieldStatus 0x1F621E and the ISR 0x1FD68E read the field from I/O port 0x80C0 bit 5
PixelShader_Definition = 0x8  # D3DDevice_SetPixelShader 0x20238B reads the definition pointer at [handle + 8]
# D3D8 keys the host Direct3D_CreateDevice also XDK_REQUIREs (texture/stream setters are D3DStateFns' names)
TextureState_Stage    = 32      # the deferred texture-state setter 0x1F6700: shl ecx, 5; add ecx, edx -> D3D_DeferredTextureState[stage * 32 + type] (0x1F671F)
Stream_Size           = 12      # D3DDevice_SetStreamSource 0x1F801B/0x1F801E indexes the stream table by n * 12
Stream_Stride         = 0x0     # D3DDevice_SetStreamSource 0x1F807A stores Stride at 0x206728 + n * 12
Stream_Buffer         = 0x8     # D3DDevice_SetStreamSource 0x1F8074 stores the vertex buffer at 0x206730 + n * 12
DirtyFlags_Texture    = 0x408   # D3DDevice_SetTexture 0x1F6002 ors 0x408 into D3D_DirtyFlags when it clears a stage (0x400 at 0x1F60C6, plus 8 at 0x1F60D8 when the stage was empty, when binding)
```

The five `[data]` globals the host `Direct3D_CreateDevice` aborts on, plus two it reads elsewhere:

```toml
D3D_DirtyFlags              = 0x203B98  # = D3D_Device + 8; SetRenderStateNotInline 0x1F66D5 `or dword_203B98, eax`; deferred setters 0x1F6687/0x1F6719; CDevice_Init 0x1FBBD1 ors 0x7FF into [device + 8]
D3D_DeferredTextureState    = 0x203750  # the deferred texture-state setter 0x1F671F stores [stage * 32 + type] here
D3D_RenderState             = 0x203950  # SetRenderStateNotInline 0x1F66BD/0x1F66DF store [state]; SetPixelShader 0x2024BD copies 0x39 definition dwords here (as 5558 0x24E82F into 0x2646B0)
D3D_Streams                 = 0x206728  # D3DDevice_SetStreamSource 0x1F8000: stride at +0, vertex buffer at +8, 12 bytes per stream
D3D_SimpleRenderStateEncode = 0x253520  # SetRenderStateNotInline 0x1F66AA loads the method for SetRenderState_Simple from [state * 4]; 0x52 NV2A methods (0x40260 ...)
D3D_IndexData               = 0x20374C  # D3DDevice_SetIndices 0x1F6189 stores [pIndexBuffer + 4] (0 at 0x1F6190); read by title code at 0x1397E9
D3D_FvfShader               = 0x2067E8  # CDevice_Init 0x1FBF4A stores it in CDevice_VertexShader; SetVertexShader 0x1F83E2 builds FVF programs into it
```

Integration notes for these values:

- **Numbering.** `D3D_RenderState`, `D3D_DeferredTextureState` and `D3D_SimpleRenderStateEncode` are
  indexed by **3925** state numbers. The bounds here are simple `< 0x52`, deferred `< 0x74` and
  complex `0x74-0x8F`, and the deferred texture-stage block is rotated against 5849's. Any host code
  that indexes them with 5849 `gpu::kRs*`/`kTss*` constants misreads them; `D3DStateFns`' doc has the
  maps.
  - The render-state array has 144 entries and ends exactly at `D3D_Device`
    (`0x203950 + 144*4 = 0x203B90`). A 5849 index of 144 or more, for example `SetComplex` with
    `kRsCullMode = 147`, would write into `CDevice`; 147 lands on `+0xC`, `CDevice_Flags`.
  - The six data values here agree with `config/found-d3d8-data.toml`, which is staged
    (`staged = true`) for exactly this reason. I add nothing beyond `D3D_FvfShader` and the keys
    above.
- **Embedded frame buffers.** 3925 keeps them inside `CDevice` (`+0x2150`, `+0x2168`, `+0x2180`) and
  `Present` rotates their data pointers. A host that wants the guest `GetBackBuffer` to keep working
  could place its surfaces' headers there instead of writing pointer fields. Otherwise
  `GetBackBuffer` needs a 3925-ABI host body.
- **`CDevice_AutoDepth`.** If the host stores a surface pointer at `+0x2198`, the only reader that
  matters, `D3D_InitDefaultState`'s nonzero test, is satisfied. The guest teardown `0x1FB6D7` would
  pass that pointer to `MmFreeContiguousMemory`, but it only runs if the guest `UnInit` runs.
- **`CDevice_VBlankCallback`.** It is never set in this title. If a host ever calls a callback found
  there, it must pass the blank count by value, not the 5849 `{count, swaps, flag}` pointer.
