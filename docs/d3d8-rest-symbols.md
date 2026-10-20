# D3D8 remaining names — Fuzion Frenzy (USA), XDK 3925

The 28 `D3D`-section names still at 0 in `config/game.toml`, plus the one further name in the brief
that has no row there at all, `D3DDevice_SetVertexShaderConstant`. Image: `out/ff.exe`, section `D3D`
0x1F5340-0x206970. Reference: `out/sims5558.exe` (XDK 5558, names applied); every twin below was read
there (Ghidra decompile/assembly), and every candidate in `ff.exe` was read in the shared IDA database.
Machine-readable result: `config/found-d3d8-rest.toml`.

**Result: no name in this set can be merged today.** One name is proven but cannot reach the build yet,
one candidate is proven to be a library-internal body but cannot be shown to carry the exported name,
and the other 27 do not exist in this image. Per name:

| Verdict | Count | Names |
|---|---|---|
| Proven, held back | 1 | `D3DDevice_SetVertexShaderConstant` |
| Unprovable candidate, left out | 1 | `D3DDevice_SetTile` |
| Absent in XDK 3925 (different state / older API there) | 5 | `SetRenderState_MultiSampleMode` (see its own section), `SetRenderState_MultiSampleRenderTargetMode`, `SetRenderState_SampleAlpha`, `GetBackBuffer2`, `GetDepthStencilSurface2` |
| Not linked by this title | 22 | the rest |

That is a stronger statement than the previous analyst's "not proven" for most of these names, and this
document replaces it where it says so. For 22 names the searches below are exhaustive over the `D3D`
range: each name's body must contain a specific NV2A token, call a specific helper, allocate, or read a
specific `CDevice` field, and every occurrence of that token / call / read in the range is enumerated
and identified. A body that contains none of its required marks does not exist here, in whatever form
3925 might have compiled it — the marks are the API's hardware semantics, not 5558's byte shape. The
two exceptions, where an exhaustive search of that kind is not possible, are `SetTile` and
`SetVertexShaderConstant`, and they are exactly the two names not classified absent.

**Host bodies.** `sdk/runtime/xdk/d3d8.cpp` has an `HLE_*` body for every one of these names except
`D3DDevice_SetVertexShaderConstant` (grep for `HLE_STDCALL(`, `HLE_FASTCALL(`, `HLE_WRAP(`). So host
readiness is not what blocks any of them: 28 names have a body waiting and no guest address to attach
it to, and the one proven guest address has no body waiting. Sorted by value: the proven name comes
first, then the unprovable candidate (it would be valuable — a no-op host body already exists), then
the absent names, which would each have been immediately live if a body had existed.

I renamed the two identified bodies in the shared `ff.exe` IDA database for navigation:
`D3DDevice_SetVertexShaderConstant_3925` at 0x1F84A0 and `D3DDevice_SetTileNoWait_3925` at 0x1F6240.
The `_3925` suffix follows `D3DCoreFns`' convention (`docs/d3d8-core-symbols.md`); neither rename is a
claim that the unsuffixed name may be merged.

## Method

- NV2A push headers are `(dwords << 18) | method` in both builds, so a setter's header is one exact
  dword: `Begin`/`End` write 0x417FC, `FlushVertexCache` 0x41710, the vertex-shader-constant paths
  0x41EA4 plus a `(n << 18) | 0xB80` chunk header, and `SetPixelShaderConstant` the combiner-factor
  headers 0x40A60-0x40A7C / 0x40A80-0x40A9C, 0x41E20/0x41E24 and 0xC181C. Each was searched as a raw
  4-byte immediate over the whole `D3D` range of `ff.exe`; every hit is attributed to its function
  below.
- Where a name has no token of its own, the search enumerates the thing it cannot avoid touching:
  callers of `D3D_BlockOnTime` 0x1FB020, of the push-reserve helpers 0x1FB3B0/0x1FB3D0 and of
  `D3DDevice_MakeSpace` 0x1FB110, readers of `CDevice+0x3F0` (completed-clock pointer) and of
  `CDevice+0x40C` (render target), callers of the allocator 0x33759 and the free 0x33776, call sites
  of the `AvSendTVEncoderOption` import pointer 0x253450, and the small functions (≤ 20 bytes) of the
  range, of which there are 19, all identified.
- `ret`/`regs` from `docs/symbol-targets.json` are used as disqualifiers and length as corroboration,
  as in the earlier D3D work. Anchors already proven in `config/game.toml` fix the library's local
  order around several of the empty slots.

## Proven but held back

### D3DDevice_SetVertexShaderConstant = 0x1F84A0 — proven, not emitted

This verifies the previous analyst's unproven claim, and strengthens it: the name is proven, by title
call sites as well as by the body.

- **Body (158 bytes, `retn 0Ch`).** `mov cl, [ebx+0Ch]` tests device flag 0x10 (`CDevice_Flags`);
  unless set, it copies the data to the device's own constant mirror at
  `device + (Register + 300) * 16` (`lea edi, [edx+0CCh]; shl edi, 4` with `edx = Register + 60h`).
  It reserves `4 * Count + 26` dwords through the reserve helper 0x1FB3D0, writes the header
  **0x41EA4** (SET_TRANSFORM_CONSTANT_LOAD) and the register, then the data in chunks headed
  `0x800B80` — `(32 << 18) | 0xB80` — with a final chunk `(remaining << 18) | 0xB80` at 0x1F8520, and
  stores the new push cursor to `[device]`. This is the 5558 constant-load packet in its stdcall form;
  5558's five variants all write the same 0x41EA4 header and 0xB80-method chunks.
- **Title call sites: 29, all of them.** Every xref to 0x1F84A0 is in game code (0x38D80, 0x3A30C,
  0x3CF3D, 0x14CA22, …); no library function calls it. Game code only calls the public device API, so
  this entry is exported, and a 3-argument stdcall `(Register, pData, Count)` `retn 0Ch` entry that
  emits a vertex-constant load has exactly one name in the D3D8 API. One site (0x38D77-0x38D80) reads
  `push 4; push <data>; push 5Ch` — register 4, count 92, the full constant file. This also refutes
  any "internal helper" reading: internal helpers in this library are not called from 29 game sites.
- **The numbering offset is 3925's.** The guest register is biased by 60h (96) before use, both in
  the mirror address and in the pushed register dword at 0x1F84F1. The host's constant paths are
  indexed by the guest register as passed, so a future host body must account for the +96 the same
  way the guest body does — it is this build's constant numbering (compare `SetShaderConstantMode`
  0x1F8160 and the internal loader 0x1F82C0, which push register 0 for the viewport constants the
  title's programs read at their biased slots), not a detail that can be dropped.
- **Why it is not in the TOML as an entry.** It is a 3925-only form, like `D3DDevice_Present`: the name
  has no `[symbols]` row in `config/game.toml` (only the four fastcall variants have rows), no row in
  any `sdk/xdk/*/signatures.toml`, and **no host body** — `d3d8.cpp` defines only
  `HLE_FASTCALL(D3DDevice_SetVertexShaderConstant1Fast, 1)`,
  `HLE_FASTCALL(D3DDevice_SetVertexShaderConstant1, 0)`,
  `HLE_FASTCALL(D3DDevice_SetVertexShaderConstant4, 0)` and
  `HLE_FASTCALL(D3DDevice_SetVertexShaderConstantNotInlineFast, 1)`, none of which this build has
  (next section). Merging it now would be refused by `tools/merge_symbols.py` and, if forced, would
  bind the name to no implementation. It is commented out in the TOML with the three prerequisites:
  add the hand-written 3925 signature row (the no-signature form, as for `D3DDevice_Present`), let a
  scan put the row in `game.toml`, and add `HLE_STDCALL(D3DDevice_SetVertexShaderConstant, 3)` that
  stores the constants (honouring the +96 above). **Host body: none — this is the one name here where
  the host is the missing half.**

### The four fastcall variants — absent (previous claim verified)

`D3DDevice_SetVertexShaderConstant1` (95 bytes, `ret 0`, regs [ecx, edx]),
`SetVertexShaderConstant1Fast` (73, `ret 0`, [ecx, edx]), `SetVertexShaderConstant4` (162, `ret 0`,
[ecx, edx]) and `SetVertexShaderConstantNotInlineFast` (208, `ret 4`, [ecx, edx]) in 5558
(0x24BA10/0x24BA70/0x24BAC0/0x24BB70) all open with the header 0x41EA4 and an immediate chunk header:
0x100B80 (`(4 << 18) | 0xB80`) for the 1-variants, 0x400B80 (`(16 << 18) | 0xB80`) for the 4- and
NotInline variants. In `ff.exe`:

- 0x100B80 occurs **nowhere** in the `D3D` range. The 1-variants cannot exist here.
- 0x41EA4 occurs exactly four times: 0x1F7E22 in `sub_1F7DC0` (the declaration/program parser
  `CreateVertexShader` uses), 0x1F81AD in `D3DDevice_SetShaderConstantMode` 0x1F8160, 0x1F8310 in
  `sub_1F82C0` (the internal viewport-constants loader: thiscall, `ecx` = device, pushes register 0),
  and 0x1F84EA in the stdcall form above. None is a fastcall (ecx = register, edx = data) entry, none
  ends in `retn 0`, and none is called from game code except the stdcall form.
- 0x400B80 occurs at 0x1F72E0 (`sub_1F7230`, the RopZ/stencil render-state helper) and 0x2011E9
  (`sub_2010FD`, a draw helper) — neither function contains 0x41EA4, so neither is a constant path.

The guest body the title actually uses is the stdcall form; the fastcall forms were introduced after
3925. All four host bodies exist (lines 1244-1278) and are dead here. The previous analyst's claim is
verified in both halves.

## Unprovable

### D3DDevice_SetTile — candidate 0x1F6240 is the NoWait body; the exported name cannot be shown

The candidate's identity as a body is certain; only the name is in doubt, so this stays unprovable,
exactly as the previous analyst left it — the new work here is the caller comparison, which cuts
against the name rather than for it.

- **5558 has two bodies.** The exported `D3DDevice_SetTile` at 0x247A20 is 10 bytes:
  `call D3D_KickOffAndWaitForIdle` (0x24D610); `jmp sub_247940`. The real work is the unnamed
  `sub_247940` (209 bytes, `retn 8`, `(Index, pTile)`): copy the 6-dword tile, zero its last two
  dwords when `tile[0] >= 0`, store it at `device + Index * 24 + 0x1AC0`, and program it through the
  miniport, or, for a NULL/empty tile, unprogram and zero the stored tile.
- **0x1F6240 (184 bytes, `retn 8`) is that second body, step for step:** the same 6-dword copy and
  conditional zeroing (`test ebx, ebx; js` at 0x1F626A), the store at
  `device + Index * 24 + 21BCh` (0x1F627D), the miniport call with `ecx = device + 2308h` (0x1F62B0,
  `call sub_1FE678`, mask 0x3FFFFFF against 5558's 0xFFFFFFF), and the NULL path
  (`call sub_1FE866`, then six zero dwords at 0x1F62E1). I renamed it
  `D3DDevice_SetTileNoWait_3925` in the IDA database.
- **Callers match the internal body, not the export.** In 5558 the NoWait body's callers are the
  wrapper (a jump) plus the library's own frame-buffer setup/teardown functions (0x24FA10 ×2,
  0x24FB40, 0x250020). In `ff.exe` 0x1F6240 is called only at 0x1FB927 and 0x1FBA07
  (`CDevice_InitFrameBuffers`, from `CDevice_Init`) and 0x1FB6CB and 0x1FB6F8 (`sub_1FB640`, from
  `CDevice_UnInit`) — the same internal positions. Internal callers prove nothing about an exported
  name: in 5558 they deliberately bypass the export to skip the wait.
- **The wrapper form does not exist here.** `D3D_KickOffAndWaitForIdle` 0x1FB300 has exactly four
  incoming edges: the jump from the `BlockUntilIdle` thunk 0x1F5510, and calls in `PersistDisplay`
  0x1F654D, `CDevice_UnInit` 0x1FC01A and `Present` 0x2021FD. No 10-byte `call; jmp 0x1F6240` body
  exists anywhere in the range (the 19 functions ≤ 20 bytes are all identified; none jumps to
  0x1F6240, whose only xrefs are the four calls above).
- **Why not emit the NoWait address anyway.** Two reasons. First, the identity that is proven is
  "the internal tile body", and in the only build where both bodies exist the exported name belongs
  to the other one; whether 3925 merged the wait into the export, exported the NoWait body as-is, or
  had no export at all is not decidable from this image, and a wrong name is silent corruption where
  an absent one is not. Second, the host body (`HLE_STDCALL(D3DDevice_SetTile, 2) { return 0; }`,
  line 1613) would replace the guest body for the library's own callers too — the frame-buffer setup
  that stores the tile at `+0x21BC` and programs the miniport would become a no-op if that guest path
  ever runs. The title itself never calls either form (no game-code xref), so leaving it at 0 costs
  nothing today: the guest body keeps running for its internal callers. **Host body: yes (a no-op
  stub), but not emitted.** Commented out in the TOML with this reasoning.

## Not emitted, by design of the earlier XDK

### D3DDevice_SetRenderState_MultiSampleMode — do not emit for 0x1F65F0, and the host body is the reason as much as the name

This was left open by both previous analysts ("Main can choose to bind it knowingly"). Having read the
host body, the answer is no; binding it would be incorrect, for two independent reasons, either of
which is enough.

1. **The value domains differ, and the host would silently misfile the value.** 3925's slot 134 is
   `D3DRS_MULTISAMPLETYPE`. Its handler 0x1F65F0 (`retn 4`) stores its argument at `D3D_RenderState +
   134 * 4` = 0x203B68 and tail-calls `SetRenderTarget([device+40Ch], [device+410h])`. The argument is
   a `D3DMULTISAMPLE_TYPE` bit field (x samples in bits 4-7, y in bits 0-3, 0x1000 multisampling,
   0x2000 supersampling; the readers at 0x1F70E0 (`test ah, 30h`, `shr ecx, 4; and ecx, 0Fh`) and
   0x1F6EFC (`test esi, 1000h`) decode it as such). The host body,
   `COMPLEX_STATE(MultiSampleMode, gpu::kRsMultiSampleMode)` (line 1020), calls
   `SetComplex(154, v)`, which does `Set(g_state.rs[154], v)` — filing a bit field like 0x2222 or
   0x11 into the host slot the renderer logs and would interpret as a `D3DMULTISAMPLEMODE` enum —
   and then, because `sdk/xdk/3925/layout.toml` correctly records `RenderState_MultiSampleMode = -1`
   (absent), writes nothing to the title's table and merely logs. So the one store the guest body
   exists to make, into 0x203B68, would not happen. The guest readers of that slot (the viewport /
   supersample path 0x1F70E0, the constants loader 0x1F82C0, which tests `dword_203B68 & 0x1000`)
   would keep reading the stale default. That is the silent-corruption pattern, not an approximation
   of it.
2. **The side effect would be dropped.** The guest handler's whole purpose after storing is to
   re-apply the current render target and depth so the new type takes effect (the supersample scale
   is recomputed from the stored type). The host body performs no render-target re-application and
   its own supersample state (`SetMultiSampleType`, line 404) is fed from the presentation
   parameters, not from this state, so nothing on the host side would take effect either.

The ABI match (both are 1-argument stdcall, `retn 4`) is real but irrelevant: it is the only thing
that matches. 3925's API for this slot is `SetRenderState` with state 134, which the title reaches
through its own `SetRenderStateNotInline` and the handler table at 0x2025C0; left unnamed, 0x1F65F0
keeps doing exactly that. **Host body: yes (via the `COMPLEX_STATE` macro), and it must not be
bound to this address.**

### D3DDevice_SetRenderState_MultiSampleRenderTargetMode and _SampleAlpha — absent from XDK 3925

Established by `D3DStateFns` (`docs/d3d8-state-symbols.md`, "Render-state indices for XDK 3925") and
re-verified against the tokens here. The complex handler table at 0x2025C0 has 28 entries for states
116-143; slot 134 is `MULTISAMPLETYPE` (above) and ShadowFunc (135) follows it directly, leaving no
slot for `MultiSampleRenderTargetMode`, and LineWidth (136) is followed directly by Dxt1NoiseEnable
(137), leaving none for `SampleAlpha`. Token corroboration: 0x41D7C (the multisample control header
all three 5558 setters push) occurs exactly twice in the range, at 0x1F75EC in
`D3DDevice_SetRenderState_MultiSampleAntiAlias` 0x1F75B0 and 0x1F763C in
`D3DDevice_SetRenderState_MultiSampleMask` 0x1F7600, both already named, and the value both build is
`(RS[133] << 16) | (RS[132] & 0xF)` — a 5558 `SampleAlpha` body would be a third 0x41D7C writer
storing its own slot and OR-ing its argument into that word, and there is none. Host bodies exist
for both (lines 1021, 1024) and stay dead. Previous claims verified.

### D3DDevice_GetBackBuffer2 and D3DDevice_GetDepthStencilSurface2 — do not exist in 3925

Established (brief; `docs/d3d8-core-symbols.md`), restated without re-derivation: this build has the
older out-pointer forms, both already named and already given 3925 host bodies —
`D3DDevice_GetBackBuffer(BackBuffer, Type, ppSurface)` at 0x1F58A0 (`retn 0Ch`; *ppSurface is a
surface embedded in `CDevice` at `+0x2150 + index * 0x18`) and
`D3DDevice_GetDepthStencilSurface(ppSurface)` at 0x1F5B40 (`retn 4`). The 5558 twins
(0x2463E0 `retn 4` returning a heap surface pointer; 0x246B20 `ret 0` likewise) have no counterpart:
every argument-shaped reader of the relevant fields was enumerated during the core pass and the two
bodies above are the only getters. The `...2` host bodies (lines 851, 865) take and return a
*different* ABI — a caller who wired 0x1F58A0 to `HLE_STDCALL(D3DDevice_GetBackBuffer2, 1)` would
hand the title a return value where it expects a stored pointer, into the wrong surface
representation. Leave both at 0. **Host bodies: yes, for the wrong functions.**

## Absent: no body of the required shape exists in the image

For each name: the 5558 twin, what its body must contain, and the exhaustive `ff.exe` search that
finds every occurrence of that mark and identifies it as something else. "Not linked" means the same
thing in each case — the library object was never pulled in, because the title (a fixed-function,
draw-call title) never references it; the guest body is not hiding under a different shape, because
the marks searched for are the hardware operations that define the API.

### Push-buffer immediate mode: Begin, End, BeginPush, EndPush

- **`D3DDevice_Begin`** (5558 0x24E200; 53 bytes, `retn 4`; header **0x417FC** with the primitive,
  then `flags |= 0x800`) and **`D3DDevice_End`** (0x24E240; 65 bytes, `ret 0`; **0x417FC** with 0,
  `flags &= 0xFFFFE7FF`, `SetFence(1)` if flag 0x1000 was set). 0x417FC occurs exactly 8 times in
  the `D3D` range: 0x2013BD and 0x201496 in `D3DDevice_DrawVerticesUP` 0x201390, 0x201513 and
  0x201559 in `D3DDevice_DrawVertices` 0x2014D0, 0x2015B7 and 0x2017A5 in
  `D3DDevice_DrawIndexedVertices` 0x201570, and 0x201CC4 and 0x201E4D in `sub_201BE0` (Present's
  filter quad). Every writer is a draw path, inlining its own begin/end — in 3925 the draw calls
  own the primitive delimiters, and there is no standalone begin or end entry for a title to call.
  Host bodies: `HLE_STDCALL(D3DDevice_Begin, 1)` line 1616, `HLE_STDCALL(D3DDevice_End, 0)` line
  1628. Previous claim verified.
- **`D3DDevice_BeginPush`** (5558 0x24D5C0; 68 bytes, `retn 4`, one argument `Count`: if
  `push + Count * 4 >= threshold + 0x200`, call `D3D_MakeRequestedSpace(max(Count*4 + 0x204, …))`).
  A 3925 form must reach `D3DDevice_MakeSpace` 0x1FB110. Its direct callers are exactly three:
  `D3DDevice_SetRenderState_Simple` 0x1F6650 (inline path) and the two reserve helpers 0x1FB3B0
  (74 callers: the named state/texture setters and internal state-sync readers, listed in
  `docs/d3d8-state-symbols.md` and re-enumerated for this pass) and 0x1FB3D0 (7 callers:
  `LoadVertexShader`, `sub_1F82C0`, `SetVertexShaderConstant` 0x1F84A0, and the `Draw*` anchors).
  Every one of those 82 call sites is an identified internal pusher taking `(device)` or
  `(device, count)`; none is a 1-argument `BeginPush(Count)` wrapper, and no game code calls any of
  them. 3925's library reserves push space through the internal two-argument 0x1FB3D0 (`retn 8`) —
  naming that as `BeginPush` would hand the host body `HLE_STDCALL(D3DDevice_BeginPush, 1)`
  (line 1561) a device pointer where it reads a dword count (this is the ABI warning in
  `docs/xapi-crt-symbols.md`, verified against the call sites). Previous claim verified, including
  the "wrong ABI if named" half. **Host body: yes.**
- **`D3DDevice_EndPush`** (5558 0x24D0C0; 13 bytes of code, `retn 4`:
  `mov eax, [esp+4]; mov ecx, [pDevice]; mov [ecx], eax`). It is the only API whose whole body is
  "store my argument into the push cursor". Among the 19 functions of ≤ 20 bytes in the range
  (InsertFence, the three 17-byte RopZ setters, `D3D_KickOffAndWaitForIdle`, the reserve helper
  0x1FB3B0, the `BlockUntilIdle` and `BlockUntilNotBusy` thunks, and small data helpers such as
  0x1FC690 `return [a1+4] | 0x80000000`), none loads a stack argument and stores it through
  `D3D_pDevice` 0x206720; the only absolute store to the push cursor `D3D_Device` 0x203B90 in the
  image is in `SetRenderState_Simple`. No `EndPush` body exists. **Host body:
  `HLE_STDCALL(D3DDevice_EndPush, 1)` line 1567.**

### Fences: BlockOnFence, IsFencePending

- **`D3DDevice_BlockOnFence`** (5558 0x2453C0; 15 bytes: `push 4; push <fence>; call
  D3D_BlockOnTime; retn 4`). `D3D_BlockOnTime` 0x1FB020 has exactly five call sites: 0x1FB2E1 in
  `D3DDevice_MakeSpace` (second arg 1), 0x1FB30B in `D3D_KickOffAndWaitForIdle` (0), and 0x1FB35A,
  0x1FB37E, 0x1FB390 in `D3D_BlockOnResource` (0). No site pushes 4, and no 15-byte `retn 4` wrapper
  calls it. The wrapper does not exist; a title that wants to wait calls the library's internal
  paths. **Host body: `HLE_STDCALL(D3DDevice_BlockOnFence, 1)` line 750.** Previous claim verified.
- **`D3DDevice_IsFencePending`** (5558 0x24D010; 30 bytes, `retn 4`; returns
  `(clock - fence) < (clock - *completed)` unsigned, reading both `[device+2Ch]` (clock) and
  `[[device+30h]]` (completed clock)). The 3925 fields are clock at `CDevice+0x1C` and the completed
  pointer at `CDevice+0x3F0`; every instruction in the range that reads displacement 0x3F0 is at
  0x1FACE2 (`sub_1FACC0`, a device setup helper), 0x1FAF5A (`CDevice_KickOff`), 0x1FB037
  (`D3D_BlockOnTime`), 0x1FB1AD (`D3DDevice_MakeSpace`), 0x1FBB47 (`CDevice_Init`, the store),
  0x1FC8B4 (`D3DResource_IsBusy`) and 0x2006F4 (`sub_20057A`, setup). None is a 30-byte fence test —
  and the last-but-one shows where the logic went in 3925: `D3DResource_IsBusy` 0x1FC850 inlines
  the exact unsigned-difference test on the resource's own stamp (`[device+1Ch] - stamp <
  [device+1Ch] - **[device+3F0h]` at 0x1FC8B4). The standalone entry was never linked. **Host body:
  `HLE_STDCALL(D3DDevice_IsFencePending, 1)` line 759.** Previous claim verified.

### Push one-offs: FlushVertexCache, SwitchTexture

- **`D3DDevice_FlushVertexCache`** (5558 0x2476F0; 39 bytes, `ret 0`; pushes header **0x41710**
  and 0). 0x41710 occurs exactly once in the range: 0x201829, inline in `sub_201810` (77 bytes,
  5 arguments, `retn 14h`) — the vertex-buffer lock, which flushes the cache itself under flag
  0x10 before calling `D3D_BlockOnResource`. There is no standalone 39-byte pusher; 3925 inlined
  the flush into the lock. Absent as a function, present as two instructions. **Host body:
  `HLE_STDCALL(D3DDevice_FlushVertexCache, 0) { return 0; }` line 804.** Previous claim verified.
- **`D3DDevice_SwitchTexture`** (5558 0x245300; 48 bytes, fastcall regs [ecx, edx] + 1 stack arg,
  `retn 4`; writes ecx, edx and the stack dword as three raw push dwords — it is the only setter
  with no header of its own, the caller supplies the method in ecx). Any 3925 form must reserve
  push space, and every reserver caller in the image is enumerated under `BeginPush` above: of
  those, the only ones of comparable size are the named render-state/texture-state leaves (each
  writes its own constant header first), the programme-load helper `sub_1F8260` (51 bytes; writes
  header 0x41E9C and copies a programme with 0x1F7B90 — a vertex-programme path, not a texture
  switch) and the vertex-buffer lock just discussed. None writes its ecx input as the first push
  dword, and none is called from game code. No switch-texture entry exists. **Host body:
  `HLE_FASTCALL(D3DDevice_SwitchTexture, 1)` line 1124.** Previous claim verified.

### Overlay and getters: EnableOverlay, GetRenderTarget2, GetVertexShaderFunction

- **`D3DDevice_EnableOverlay`** (5558 0x259700; 99 bytes, `retn 4`; spins on `[miniport+8700h]`,
  then writes 0x11 to `+8100h`, 0/−1/0 to `+8920h/+8928h/+8930h`, 0 to `+8900h` and 0x1000 to
  `+8910h/+8918h`, via the miniport pointer at `[device+934h]`). In `ff.exe` the miniport lives at
  `CDevice+0x2308`, but the register offsets are hardware offsets and would be the same: a search
  of the whole `D3D` range for displacements 0x8700, 0x8704, 0x8900, 0x8910, 0x8920, 0x8928, 0x8930
  finds **none of them**; the only 0x8100 accesses are at 0x1FDE36/0x1FDE3F in
  `D3D_Miniport_Dpc` 0x1FDDC0, testing and setting bit 0 of a status register in the interrupt
  path — not an overlay enable sequence, and not callable as one. No overlay entry exists; this
  title never enables the video overlay. **Host body: `HLE_STDCALL(D3DDevice_EnableOverlay, 1)
  { return 0; }` line 791.** Previous claim verified.
- **`D3DDevice_GetRenderTarget2`** (5558 0x246B00; 26 bytes, `ret 0`; returns
  `[device+1A04h]` AddRef-ed). The 3925 field is `CDevice_RenderTarget` at +0x40C. Every reader of
  displacement 0x40C in the range: `SetRenderTarget` 0x1F5655, `SetViewport` 0x1F5B7C, the
  MultiSampleType handler 0x1F6605, `Dxt1NoiseEnable` 0x1F697C, the viewport push `sub_1F6EB0`
  0x1F6F17, `sub_1F70E0` 0x1F70EE, the constants loader 0x1F836C, the frame-buffer teardown
  `sub_1FB640` 0x1FB664, `Clear` 0x1FC1C3, the setup writer `sub_20057A` 0x200725, and Present's
  helpers 0x20193C, 0x201BED, 0x2020FF. None is a getter: none returns the field to a caller with
  an AddRef and `retn 0` (the AddRef-variant 0x1FC7C0's callers are likewise only
  `GetBackBuffer_3925`, `GetDepthStencilSurface_3925`, `sub_1F7880`, itself and `sub_201930`).
  3925's API returns the target through `SetRenderTarget(NULL, …)` semantics instead; there is no
  `...2` getter. **Host body: `HLE_STDCALL(D3DDevice_GetRenderTarget2, 0)` line 859.** Previous
  claim verified.
- **`D3DDevice_GetVertexShaderFunction`** (5558 0x24C870; 188 bytes, `retn 0Ch`; checks the
  programme header tokens 0x2078/0x7378/0x7778 at the handle, copies the programme out from
  handle+0x117, returns 0x88760867 `D3DERR_MOREDATA` when the buffer is short). In `ff.exe`:
  0x7778 and 0x88760867 occur nowhere in the `D3D` range, displacement 0x117 occurs nowhere, and
  0x2078/0x7378 occur only at 0x1F7F85/0x1F7F98, inside `D3DDevice_CreateVertexShader` 0x1F7EC0
  stamping a new programme's header — the write side. Nothing copies a programme back out of a
  handle (the ff object keeps its programme at handle+0x168 and its length at handle+0x14, and no
  function reads the pair for a caller). Absent. **Host body:
  `HLE_STDCALL(D3DDevice_GetVertexShaderFunction, 3)` line 1206.** Previous claim verified.

### Pixel shaders: CreatePixelShader, DeletePixelShader, SetPixelShaderConstant

- **`D3DDevice_CreatePixelShader`** (5558 0x24E290; 72 bytes, `retn 8`; allocates 0xFC bytes,
  returns 0x8007000E on failure, sets `[obj]=1, [obj+1]=1, [obj+2]=obj+3`, copies 0x3C dwords of
  definition, stores the handle through the out pointer). The allocator 0x33759 is called from
  exactly three places in the `D3D` range: 0x1F5D06 in `D3DDevice_SetLight` (grows the light
  array), 0x1F7885 in `sub_1F7880` (allocates a 24-byte surface header, `Common = 0x1050001`), and
  0x1F7F12 in `D3DDevice_CreateVertexShader`. None allocates a pixel-shader object, and immediate
  0xFC occurs only in `sub_1FFCC0`, `sub_20057A` and `D3DDevice_SetPixelShader` — none of them a
  creator with an out pointer. No pixel-shader allocator exists in this library build; the title
  is fixed-function and its only pixel-shader definition is the one `Present`'s filter path builds
  internally for `D3DDevice_SetPixelShader` 0x202320. **Host body:
  `HLE_STDCALL(D3DDevice_CreatePixelShader, 2)` line 1295.** Previous claim verified.
- **`D3DDevice_DeletePixelShader`** (5558 0x24E2E0; 29 bytes, `retn 4`; `--[handle]`, free when it
  reaches 0 and `[handle+1] != 0`). The free 0x33776 is called from exactly five places in the
  range: `D3DDevice_SetLight` 0x1F5D9D, `D3DDevice_DeleteVertexShader` 0x1F82B1 (the vertex twin,
  already named), `CDevice_UnInit` 0x1FC174, `sub_1FC580` (a 70-byte registered-object list
  unlink) and the resource destroyer `sub_1FC6A0` (three sites). None is a 29-byte
  decrement-and-free on a pixel-shader handle, and with no creator there is nothing for one to
  delete. **Host body: `HLE_STDCALL(D3DDevice_DeletePixelShader, 1)` line 1306.** Previous claim
  verified.
- **`D3DDevice_SetPixelShaderConstant`** (5558 0x24E940; 711 bytes, `retn 0Ch`; per register,
  packs the float4 and pushes the combiner-factor headers **0x40A60-0x40A7C** and **0x40A80-
  0x40A9C** (one per stage bank), 0x41E20/0x41E24, and **0xC181C** for register 0, mirroring into
  the render-state table). Every one of those header dwords occurs **zero times** in the `D3D`
  range of `ff.exe` (the related but distinct 16-dword header 0x400A60 occurs once, at 0x1F68E0
  in the TextureFactor setter, and 0x800A60 once, at 0x2023F4 in `SetPixelShader` — both already
  accounted for). A 711-byte body writing a dozen distinct headers cannot hide from its own
  tokens. Absent. **Host body: `HLE_STDCALL(D3DDevice_SetPixelShaderConstant, 3)` line 1357.**
  Previous claim verified.

### Display settings the title never links: SetGammaRamp, SetFlickerFilter, SetSoftDisplayFilter, SetScreenSpaceOffset

- **`D3DDevice_SetGammaRamp`** (5558 0x244EC0; 131 bytes, `retn 8` `(Flags, pRamp)`; compares and
  copies 0xC0 dwords into the ramp slot at `device + 1C28h + 1DCh + index * 300h`, selected by
  `SwapCount & 1`, then sets the pending flag or uploads immediately). In `ff.exe` the ramp slots
  live at `CDevice_GammaRamps` +0x251C (miniport+0x214); the only instructions touching
  displacement 0x214 are the vertical-blank upload 0x1FD78A, the miniport ioctl dispatcher
  `sub_1FD8C0` 0x1FD91E and the identity-ramp builder `sub_1FDF83` 0x1FDF8D — all miniport-side,
  none a caller-facing copy from a stack pointer. The positional evidence agrees: in 5558 the
  setter sits between the 212-byte device-caps copy helper 0x244E60 and `SetTransform` 0x244FE0;
  in `ff.exe` the twin helper `sub_1F5340` (23 bytes, copies the same 212 bytes from 0x284A38)
  ends at 0x1F5357 and `D3DDevice_SetTransform` begins at 0x1F5360 — nine bytes of padding where
  the 131-byte setter would have to be. Not linked (the game sets its gamma, if at all, through
  its own `mov ecx, 0C0h` copies in game code at 0x28651 and 0x3092F, outside the library).
  **Host body: `HLE_STDCALL(D3DDevice_SetGammaRamp, 2)` line 806.** Previous claim verified.
- **`D3DDevice_SetFlickerFilter`** (5558 0x245620; 66 bytes, `retn 4`; cached call
  `AvSendTVEncoderOption(miniport, 0Bh, value, 0)`) and **`D3DDevice_SetSoftDisplayFilter`**
  (5558 0x245670; 81 bytes, `retn 4`; same with option 0Eh and a boolean-coerced value). The
  `AvSendTVEncoderOption` import pointer 0x253450 has exactly four call sites in the image:
  0x1FB65E in `sub_1FB640` (option 9), 0x1FD423 in `sub_1FD410` (option 6), 0x1FE947 in
  `sub_1FE928` (option 9) and, loaded into `ebx` at 0x202241, the two calls in
  `D3DDevice_Present` 0x202254 (option 0Bh, value 0 — the mode-change path resetting the filter
  itself) and 0x202266 (option 0Fh). No site passes option 0Eh at all, and the only 0Bh call is
  `Present`'s internal literal, not a cached 1-argument wrapper; there is no `retn 4` function
  whose body is one of these calls. Positionally, in 5558 both setters sit between
  `BlockUntilVerticalBlank` and `InsertFence`; in `ff.exe` `D3DDevice_BlockUntilVerticalBlank`
  0x1F5520 ends at 0x1F5544 and `D3DDevice_InsertFence` starts at 0x1F5550. Not linked.
  **Host bodies: `HLE_STDCALL(D3DDevice_SetFlickerFilter, 1)` and
  `HLE_STDCALL(D3DDevice_SetSoftDisplayFilter, 1)`, both `{ return 0; }`, lines 789-790.**
  Previous claims verified.
- **`D3DDevice_SetScreenSpaceOffset`** (5558 0x2480F0; 73 bytes, `retn 8` `(float, float)`;
  stores both floats (each + 0.53125's constant) at `[device+0EF8h]/[device+0EFCh]`, refreshes the
  viewport constants and re-pushes the viewport). 3925 has no such field: the viewport push
  `sub_1F6EB0` adds the constant `flt_2025A4` = 0.53125 inline at 0x1F6EDA and 0x1F6F02, and that
  constant's only two xrefs in the image are those reads — it is never written, so there is no
  stored offset for a setter to set. Call-shape corroboration: the twin refreshes through the
  constants loader and the viewport push; the loader `sub_1F82C0`'s callers are `SetTransform`,
  `SetRenderTarget`, `ZEnable`, the two MultiSample setters, `SetVertexShader` and the fog
  reader `sub_1F8E20`, and the viewport push 0x1F6EB0's callers are `SetViewport`, the two
  MultiSample setters, `SelectVertexShader` and `SetVertexShader` — no 2-float `retn 8` function
  is among either set. Not linked, and the field it would write does not exist.
  **Host body: `HLE_STDCALL(D3DDevice_SetScreenSpaceOffset, 2)` line 965.** Previous claim
  verified.

## What the integrator gets from this file

Nothing to merge — deliberately. `config/found-d3d8-rest.toml` contains two commented-out rows:
the proven `D3DDevice_SetVertexShaderConstant` 0x1F84A0, waiting on a 3925 signature row, a
`game.toml` row and a host body (the only genuinely new work this set produces, and the only name
here whose absence leaves title-called code running on the guest path — its 29 call sites currently
execute the guest body, which pushes real NV2A packets the host then has to walk), and the
unprovable `D3DDevice_SetTile` 0x1F6240, waiting on evidence this image does not contain. Every
other name in the brief stays 0 because the function it names is not in the image; naming any of
them would attach a host body to whatever unrelated body happened to sit at a guessed address.
The two `...2` getters and the three render-state names likewise stay 0 because 3925's API at
those slots is a different function, already named under its own name where it exists.
