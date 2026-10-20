# XDK names by neighbour-order alignment

Written by `tools/align_symbols.py` (run from the repo root, no arguments); rerun it rather than
editing this file. It writes `config/found-align.toml` with the names below that the image proves,
and lists every other library target name here with its ranked candidates and what failed.

## Result

| Section | Target names | Already anchors | Aligned here | Emitted | Not emitted |
| --- | ---: | ---: | ---: | ---: | ---: |
| D3D | 103 | 11 | 92 | 20 | 72 |
| DSOUND | 77 | 24 | 53 | 0 | 53 |
| XPP | 5 | 5 | 0 | 0 | 0 |
| all | 185 | 40 | 145 | 20 | 125 |

- **20 names emitted** to `config/found-align.toml`, out of the 145 library-section
  target names that `config/game.toml` did not already name (50 names were non-zero there at this run, 40 of them in D3D, DSOUND and XPP).
- **Decoder self-check:** 185 of 185 signed 5558 target functions re-measured from `out/sims5558.exe` come out exactly as `config/ref/5558-signed.toml` records them (len, ret, regs, k, calls).
- **Held-out anchors:** each of the 40 library anchors was removed in turn and placed by the others: 12 re-emitted at their proven address, **0 emitted at a wrong address**, 28 not emitted (details under *Held-out anchors*).
- **Independent cross-check:** other agents' `config/found-*.toml` files (read for this report only, never as input) name 19 of the emitted names; 19 agree, **0 disagree**. 1 emitted name appears in no other file.
- **Larger truth set:** the same hold-out over the union of `config/game.toml` and those files (101 library names): 51 re-emitted correctly, **0 wrongly** (details under *Validation against the other agents' results*).

## Inputs

| Input | Used for |
| --- | --- |
| `config/game.toml` | the anchors (every non-zero `[symbols]` entry), the XBE path, its code sections |
| `assets/default.xbe` | the section table (`out/ff.exe` is a flat image and has lost it); the tool checks every section's bytes against `out/ff.exe` |
| `out/ff.exe` | the target bytes |
| `generated/game/x86_func_mapping.cpp` | the target function inventory: the recompiler's own `X86FuncMappings` table |
| `out/sims5558.exe` | the reference bytes and its function starts (its exports) |
| `docs/symbol-targets.json` | the target names and their 5558 addresses |
| `sdk/xdk/5849/signatures.toml`, `config/ref/5558-signed.toml` | each name's ret, regs, len, calls and k in two independent builds |

**Inventory used.** The recompiler's table lists 10589 starts. In the three library sections the tool adds the starts the SDK signer's own start rule finds there that the recompiler did not list (16-aligned code right after a `ret`, `jmp`, `int3` or `nop` that no conditional or short jump targets; these are functions reached only through another function's jump table), and drops starts that are not call targets and lie inside the extent of the start before them (mid-function entries from pointer seeding):

| Section | Range | Recompiler starts | Added | Dropped as interior | Aligned |
| --- | --- | ---: | ---: | ---: | ---: |
| D3D | 0x1F5340-0x206970 | 288 | 8 | 36 | 260 |
| DSOUND | 0x206980-0x221840 | 439 | 5 | 12 | 432 |
| XPP | 0x24C220-0x253348 | 237 | 3 | 3 | 237 |

## How the invariants are measured

`ret`, `regs`, `len`, `calls` and `k` are computed for every candidate by the tool itself, with a standard-library x86 decoder that reproduces what `sdk/tools/symbols/main.cpp` reads through Zydis in sign mode:

- **len** is the signer's `ExtentBefore`: code reachable from the start, following conditional and unconditional relative branches but not calls, stopping at `ret`/`int3`/`hlt`, bounded by the next known start. Known starts are the signer's: every rel32 call target of a linear sweep of the code sections, plus the anchors.
- **ret** is the set of stack bytes the reachable `ret`/`retn imm16` instructions pop; an empty set means the function leaves by jump.
- **regs** is the signer's `Trace`: a path-sensitive walk that records a general register (eax ecx edx ebx ebp esi edi) when an instruction reads it, explicitly or as a hidden operand (`cdq`, `rep movs`, `mul`, ...), before every path to that instruction has written it. A call writes eax, ecx and edx; `xor r,r`/`sub r,r`/`sbb r,r` and `push r` read nothing. **regs is measured, not dropped.**
- **calls** is the number of rel32 call and jump targets that leave the function's extent.
- **k** is the signer's constant list. Only its *stable* part is compared: constants ≥ 0x10000 (NV2A push-buffer method headers, flag masks, HRESULTs, floats). Smaller ones are mostly structure offsets, which moved between 3925 and 5558.

Proof that the decoder measures what the signer measures: re-measuring the 185 signed 5558 target functions from `out/sims5558.exe` reproduces 185 of them field for field. The 5558 side has no XBE in this repository, so its sections are rebuilt from the data (`.text` up to the first library target, code up to the end of the last export); that reproduces the signer's sweep and the check above.

## Link order is preserved within an object, not across a section

The premise of neighbour-order alignment is that both images link the same library in the same order. The anchors show that this holds for runs of functions but not for a whole section: whole blocks (object files) sit in a different order in 3925. Anchors per section, in target order, with their rank in 5558 order:

- **D3D** (11 anchors; 2 of 10 target-order neighbours are not neighbours in 5558): `4 5 6 7 8 0 1 2 3 9 10`
- **DSOUND** (24 anchors; 10 of 23 target-order neighbours are not neighbours in 5558): `0 1 2 3 4 21 19 22 5 14 15 16 17 8 9 10 11 12 13 6 7 18 20 23`
- **XPP** (5 anchors; 0 of 4 target-order neighbours are not neighbours in 5558): `0 1 2 3 4`

A single monotone alignment of a whole section is therefore wrong. The tool aligns each name only inside the bracket its own nearest anchors give it, and classifies that bracket (next section).

## Algorithm

1. **Sequences.** Reference: the 5558 functions in address order (the target names, plus the unnamed functions between them as spacers, so the alignment knows what separates two names). Target: the inventory above, in address order. Anchors are taken out of both sequences: they are the pins.
2. **Bracket.** For a name, take the anchor nearest below it and nearest above it in 5558 order (same section). The kinds:
   - *tight*: both exist, they are in the same order in the target, no other anchor lies between them in the target, and the two stretches they enclose differ in byte size by at most 3x;
   - *loose*: same order, but another anchor lies between them in the target (a moved block) or the stretches differ grossly in size, so the target stretch holds code from elsewhere;
   - *inverted*: the two anchors are in opposite order in the target (the name sits between two moved blocks); *forward*/*backward*: an anchor on one side only; *unanchored*: no anchor in the section.
   Inverted, one-sided and unanchored names are still aligned (inverted: the stretch after the lower anchor followed by the stretch before the upper one; one-sided: from the anchor to the next anchor or section edge; unanchored: the whole section) so they get candidates, but they are never emitted.
3. **Alignment.** Needleman-Wunsch over the bracket: maximise the summed pair score of a monotone, one-to-one pairing, where leaving either side unpaired costs nothing, so only positive pairings are made and both missing and extra functions are absorbed. A pair whose `ret` differs cannot be made.
4. **Lead.** Forward and backward score tables give, for each name, the best total with the name paired to each candidate and with it unpaired (max-marginals). The lead is the best of those minus the runner-up: how much worse the best alignment is that does not put the name where it was put.
5. **Emission.** A name is written only when every one of these holds:
   - the bracket is tight or loose (two proven anchors, in agreeing order);
   - `ret` equal (required), `regs` equal (required), `calls` within ±1 (required);
   - when the reference builds agree on stable constants, the candidate shares at least one;
   - length within the band, or a *distinctive* shared stable constant stands in for it (one that at most 2 target functions in the section contain);
   - alignment lead ≥ 2;
   - exclusive in the bracket: every other target function in the bracket that itself passes the required checks scores at least 1.5 lower on the pair score alone (so identical-looking functions are never told apart by position alone);
   - in a loose bracket, additionally: length within the band, and the same exclusivity over the whole section;
   - no other name chose the same address.

## Pair score, bands and why

| Term | Value |
| --- | --- |
| ret differs | pairing not allowed |
| regs equal / differ | +2 / -4 |
| len exact | +3 |
| len within band | +0.5 … +2.5, falling linearly to the band edge |
| len outside band | −0.5 … −3, growing with the excess |
| calls equal / ±1 / further | +1 / 0 / −1 per extra call |
| stable constant: distinctive shared / shared / reference has some, candidate none | +3 / +1.5 / -1.5 |

**Length band: |len − reference| ≤ max(8 bytes, 12% of the reference length)**, the reference length being whichever of len5849/len5558 is closer. Measured on the anchors (functions proven by other means, so the measurement is not circular), 3925 against 5558:

| Anchor | Section | len 3925 | len 5849/5558 | Δ | ret | regs | calls Δ |
| --- | --- | ---: | ---: | ---: | --- | --- | ---: |
| `D3DDevice_SetPalette` | D3D | 127 | 141 | -14 | = | = | 1 |
| `D3DDevice_SetRenderState_FogColor` | D3D | 71 | 77 | -6 | = | = | 0 |
| `D3DDevice_SetRenderState_CullMode` | D3D | 98 | 104 | -6 | = | = | 0 |
| `D3DDevice_SetRenderState_LineWidth` | D3D | 95 | 101 | -6 | = | = | 0 |
| `D3DDevice_SetRenderState_StencilFail` | D3D | 105 | 109/111 | -4 | = | = | 0 |
| `Direct3D_CreateDevice` | D3D | 147 | 164 | -17 | = | = | 0 |
| `D3D_InternalAddRef` | D3D | 53 | 50 | +3 | = | = | 0 |
| `D3DResource_IsBusy` | D3D | 141 | 138 | +3 | = | = | 0 |
| `D3D_InternalRelease` | D3D | 98 | 98 | +0 | = | = | 0 |
| `D3DDevice_DrawVertices` | D3D | 159 | 148 | +11 | = | = | 0 |
| `D3DDevice_DrawIndexedVertices` | D3D | 616 | 599 | +17 | = | = | 0 |
| `IDirectSound_Release` | DSOUND | 22 | 22 | +0 | = | = | 0 |
| `IDirectSoundBuffer_Unlock` | DSOUND | 5 | 5 | +0 | = | = | 0 |
| `IDirectSoundBuffer_Release` | DSOUND | 22 | 22 | +0 | = | = | 0 |
| `DirectSoundUseFullHRTF` | DSOUND | 31 | 31 | +0 | = | = | 1 |
| `IDirectSound_DownloadEffectsImage` | DSOUND | 39 | 39 | +0 | = | = | 0 |
| `IDirectSound_SetDistanceFactor` | DSOUND | 36 | 36 | +0 | = | = | 0 |
| `IDirectSound_CommitDeferredSettings` | DSOUND | 24 | 24 | +0 | = | = | 0 |
| `IDirectSound_SetI3DL2Listener` | DSOUND | 32 | 32 | +0 | = | = | 0 |
| `IDirectSoundBuffer_SetVolume` | DSOUND | 28 | 28 | +0 | = | = | 0 |
| `IDirectSoundBuffer_SetMaxDistance` | DSOUND | 36 | 36 | +0 | = | = | 0 |
| `IDirectSoundBuffer_SetMinDistance` | DSOUND | 36 | 36 | +0 | = | = | 0 |
| `IDirectSoundBuffer_SetPosition` | DSOUND | 53 | 53 | +0 | = | = | 0 |
| `IDirectSoundBuffer_SetVelocity` | DSOUND | 53 | 53 | +0 | = | = | 0 |
| `IDirectSoundBuffer_GetStatus` | DSOUND | 28 | 28 | +0 | = | = | 0 |
| `IDirectSoundBuffer_GetCurrentPosition` | DSOUND | 32 | 32 | +0 | = | = | 0 |
| `IDirectSoundBuffer_SetCurrentPosition` | DSOUND | 28 | 28 | +0 | = | = | 0 |
| `IDirectSoundBuffer_Lock` | DSOUND | 48 | 48 | +0 | = | = | 0 |
| `DirectSoundDoWork` | DSOUND | 40 | 41 | -1 | = | = | 0 |
| `IDirectSoundBuffer_SetFrequency` | DSOUND | 28 | 28 | +0 | = | = | 0 |
| `IDirectSoundBuffer_Play` | DSOUND | 36 | 36 | +0 | = | = | 0 |
| `IDirectSoundBuffer_Stop` | DSOUND | 24 | 24 | +0 | = | = | 0 |
| `IDirectSoundBuffer_SetBufferData` | DSOUND | 32 | 32 | +0 | = | = | 0 |
| `IDirectSound_CreateSoundBuffer` | DSOUND | 36 | 36 | +0 | = | = | 0 |
| `DirectSoundCreate` | DSOUND | 158 | 71 | +87 | = | = | 4 |
| `XInitDevices` | XPP | 142 | 121 | +21 | = | = | 0 |
| `XInputOpen` | XPP | 117 | 86 | +31 | = | = | 1 |
| `XInputClose` | XPP | 12 | 12 | +0 | = | = | 0 |
| `XInputGetState` | XPP | 113 | 115 | -2 | = | = | 0 |
| `XInputSetState` | XPP | 56 | 51 | +5 | = | = | 0 |

Over these 40 anchors: ret equal 40/40, regs equal 40/40, calls equal 36/40 and within ±1 39/40, length identical 24/40 and within the band 37/40 (92%).

Between the two reference builds (5849 against 5558), over the library target names both sign: ret equal 168/168, regs 168/168, len 164/168, calls 168/168.

- **ret and regs are required.** ret held for 40/40 anchors and 168/168 reference pairs, regs for 40/40 and 168/168. A mismatch means a different calling convention, which is exactly the failure that corrupts the port.
- **calls within ±1 is required**: it held for 39/40 anchors, and the reference builds agree on 168/168 names.
- **Length is corroboration, not proof.** 3925 drifts further from 5558 than 5849 does (the anchored render-state setters FogColor, CullMode and LineWidth are each 6 bytes shorter), so the band keeps the anchors' usual drift, and a length outside it is not disqualifying when a distinctive hardware constant (the NV2A method the function writes) carries the identity instead. When the reference has no stable constant, the length must be inside the band.
- **Stable constants** (NV2A methods, flags, HRESULTs) are hardware and ABI identities; a candidate that shares none of the constants both reference builds agree on is not emitted.
- **Lead ≥ 2** is the weight of one agreeing required invariant (regs): any alignment that places the name elsewhere, or nowhere, must lose at least that much evidence, not a rounding amount of length closeness.
- **Exclusivity ≥ 1.5** is one shared-constant bonus: the candidate must out-score every credible alternative by real evidence, not by a length difference of a few bytes. Order alone does not prove which of several identical-looking functions is which.
- **Size ratio ≤ 3**: an object's code changes by tens of percent between XDK versions, not by multiples; a bracket whose two stretches differ by more holds code from elsewhere.

## Emitted names

### `D3DDevice_SetIndices` = 0x1F6170

- **Bracket (loose):** `D3DDevice_SetPalette` (0x1F60F0; 5558 0x2473A0) … `D3DDevice_SetRenderState_FogColor` (0x1F6790; 5558 0x249EA0).
- **Reference:** 5558 0x247430; ret 8, regs -, len 132, calls 1; stable k 0x80000 0xFFF80000.
- **Target 0x1F6170:** ret 8, regs -, len 141, calls 2.
- **Invariants held:** ret, regs, len +9 (band ±16), calls ±1, k 0xFFF80000.
- **Alignment:** score 4.86, lead 4.86; after anchor `D3DDevice_SetPalette` = 0x1F60F0, before `sub_00247630` = 0x1F6200.
- **Exclusivity:** best credible rival in the bracket none; in the section none.
- **Cross-check:** `found-d3d8-state.toml` gives 0x1F6170 (agrees).

### `D3DDevice_PersistDisplay` = 0x1F6490

- **Bracket (loose):** `D3DDevice_SetPalette` (0x1F60F0; 5558 0x2473A0) … `D3DDevice_SetRenderState_FogColor` (0x1F6790; 5558 0x249EA0).
- **Reference:** 5558 0x247FC0; ret 0, regs -, len 302, calls 7; stable k 0x80004005.
- **Target 0x1F6490:** ret 0, regs -, len 289, calls 6.
- **Invariants held:** ret, regs, len -13 (band ±36), calls ±1, distinctive k 0x80004005.
- **Alignment:** score 6.78, lead 6.78; after `sub_00247940` = 0x1F6240, before `sub_00248C60` = 0x1F65F0.
- **Exclusivity:** best credible rival in the bracket none; in the section none.
- **Cross-check:** `found-d3d8-core.toml` gives 0x1F6490 (agrees).

### `D3DDevice_SetRenderState_Simple` = 0x1F6650

- **Bracket (loose):** `D3DDevice_SetPalette` (0x1F60F0; 5558 0x2473A0) … `D3DDevice_SetRenderState_FogColor` (0x1F6790; 5558 0x249EA0).
- **Reference:** 5558 0x2498E0; ret 0, regs ecx,edx, len 39, calls 1.
- **Target 0x1F6650:** ret 0, regs ecx,edx, len 45, calls 1.
- **Invariants held:** ret, regs, len +6 (band ±8), calls.
- **Alignment:** score 4.00, lead 3.75; after `D3DDevice_SetRenderState_PSTextureModes` = 0x1F6620, before `D3DDevice_SetRenderStateNotInline` = 0x1F66A0.
- **Exclusivity:** best credible rival in the bracket none; in the section none.
- **Cross-check:** `found-d3d8-state.toml` gives 0x1F6650 (agrees).

### `D3DDevice_SetRenderStateNotInline` = 0x1F66A0

- **Bracket (loose):** `D3DDevice_SetPalette` (0x1F60F0; 5558 0x2473A0) … `D3DDevice_SetRenderState_FogColor` (0x1F6790; 5558 0x249EA0).
- **Reference:** 5558 0x249910; ret 8, regs -, len 93, calls 1.
- **Target 0x1F66A0:** ret 8, regs -, len 90, calls 1.
- **Invariants held:** ret, regs, len -3 (band ±11), calls.
- **Alignment:** score 4.96, lead 4.96; after `D3DDevice_SetRenderState_Simple` = 0x1F6650, before `D3DDevice_SetRenderState_EdgeAntiAlias` = 0x1F6730.
- **Exclusivity:** best credible rival in the bracket none; in the section 0x1F8090 (2.89).

### `D3DDevice_SetRenderState_EdgeAntiAlias` = 0x1F6730

- **Bracket (loose):** `D3DDevice_SetPalette` (0x1F60F0; 5558 0x2473A0) … `D3DDevice_SetRenderState_FogColor` (0x1F6790; 5558 0x249EA0).
- **Reference:** 5558 0x249E20; ret 4, regs -, len 50, calls 1; stable k 0x80320.
- **Target 0x1F6730:** ret 4, regs -, len 44, calls 1.
- **Invariants held:** ret, regs, len -6 (band ±8), calls, distinctive k 0x80320.
- **Alignment:** score 7.00, lead 6.00; after `D3DDevice_SetRenderStateNotInline` = 0x1F66A0, before `D3DDevice_SetRenderState_ShadowFunc` = 0x1F6760.
- **Exclusivity:** best credible rival in the bracket none; in the section none.
- **Cross-check:** `found-d3d8-state.toml` gives 0x1F6730 (agrees).

### `D3DDevice_SetRenderState_ShadowFunc` = 0x1F6760

- **Bracket (loose):** `D3DDevice_SetPalette` (0x1F60F0; 5558 0x2473A0) … `D3DDevice_SetRenderState_FogColor` (0x1F6790; 5558 0x249EA0).
- **Reference:** 5558 0x249E60; ret 4, regs -, len 53, calls 1; stable k 0x41E6C 0xFFFFFE00.
- **Target 0x1F6760:** ret 4, regs -, len 47, calls 1.
- **Invariants held:** ret, regs, len -6 (band ±8), calls, distinctive k 0x41E6C 0xFFFFFE00.
- **Alignment:** score 7.00, lead 7.00; after `D3DDevice_SetRenderState_EdgeAntiAlias` = 0x1F6730, before anchor `D3DDevice_SetRenderState_FogColor` = 0x1F6790.
- **Exclusivity:** best credible rival in the bracket none; in the section none.
- **Cross-check:** `found-d3d8-state.toml` gives 0x1F6760 (agrees).

### `D3DDevice_SetRenderState_FrontFace` = 0x1F6850

- **Bracket (tight):** `D3DDevice_SetRenderState_CullMode` (0x1F67E0; 5558 0x249EF0) … `D3DDevice_SetRenderState_LineWidth` (0x1F6910; 5558 0x24A040).
- **Reference:** 5558 0x249F60; ret jmp, regs -, len 58, calls 2; stable k 0x403A0.
- **Target 0x1F6850:** ret jmp, regs -, len 52, calls 2.
- **Invariants held:** ret, regs, len -6 (band ±8), calls, distinctive k 0x403A0.
- **Alignment:** score 7.00, lead 7.00; after anchor `D3DDevice_SetRenderState_CullMode` = 0x1F67E0, before `D3DDevice_SetRenderState_NormalizeNormals` = 0x1F6890.
- **Exclusivity:** best credible rival in the bracket none; in the section none.
- **Cross-check:** `found-d3d8-state.toml` gives 0x1F6850 (agrees).

### `D3DDevice_SetRenderState_NormalizeNormals` = 0x1F6890

- **Bracket (tight):** `D3DDevice_SetRenderState_CullMode` (0x1F67E0; 5558 0x249EF0) … `D3DDevice_SetRenderState_LineWidth` (0x1F6910; 5558 0x24A040).
- **Reference:** 5558 0x249FA0; ret 4, regs -, len 57, calls 1; stable k 0x403A4.
- **Target 0x1F6890:** ret 4, regs -, len 45, calls 1.
- **Invariants held:** ret, regs, calls, distinctive k 0x403A4.
- **Alignment:** score 5.00, lead 5.00; after `D3DDevice_SetRenderState_FrontFace` = 0x1F6850, before `D3DDevice_SetRenderState_TextureFactor` = 0x1F68C0.
- **Exclusivity:** best credible rival in the bracket none; in the section none.
- **Cross-check:** `found-d3d8-state.toml` gives 0x1F6890 (agrees).

### `D3DDevice_SetRenderState_TextureFactor` = 0x1F68C0

- **Bracket (tight):** `D3DDevice_SetRenderState_CullMode` (0x1F67E0; 5558 0x249EF0) … `D3DDevice_SetRenderState_LineWidth` (0x1F6910; 5558 0x24A040).
- **Reference:** 5558 0x249FE0; ret 4, regs -, len 81, calls 1.
- **Target 0x1F68C0:** ret 4, regs -, len 76, calls 1.
- **Invariants held:** ret, regs, len -5 (band ±10), calls.
- **Alignment:** score 4.47, lead 4.47; after `D3DDevice_SetRenderState_NormalizeNormals` = 0x1F6890, before anchor `D3DDevice_SetRenderState_LineWidth` = 0x1F6910.
- **Exclusivity:** best credible rival in the bracket none; in the section 0x1F6B50 (4.88).
- **Cross-check:** `found-d3d8-state.toml` gives 0x1F68C0 (agrees).

### `D3DDevice_SetRenderState_Dxt1NoiseEnable` = 0x1F6970

- **Bracket (tight):** `D3DDevice_SetRenderState_LineWidth` (0x1F6910; 5558 0x24A040) … `D3DDevice_SetRenderState_StencilFail` (0x1F73F0; 5558 0x24AFE0).
- **Reference:** 5558 0x24A0B0; ret 4, regs -, len 119, calls 1; stable k 0x40100 0x40110.
- **Target 0x1F6970:** ret 4, regs -, len 119, calls 1.
- **Invariants held:** ret, regs, len exact, calls, k 0x40100 0x40110.
- **Alignment:** score 7.50, lead 7.50; after anchor `D3DDevice_SetRenderState_LineWidth` = 0x1F6910, before `sub_0024A130` = 0x1F69F0.
- **Exclusivity:** best credible rival in the bracket none; in the section none.
- **Cross-check:** `found-d3d8-state.toml` gives 0x1F6970 (agrees).

### `D3DDevice_SetRenderState_LogicOp` = 0x1F6A70

- **Bracket (tight):** `D3DDevice_SetRenderState_LineWidth` (0x1F6910; 5558 0x24A040) … `D3DDevice_SetRenderState_StencilFail` (0x1F73F0; 5558 0x24AFE0).
- **Reference:** 5558 0x24A1B0; ret 4, regs -, len 82, calls 1; stable k 0x417BC 0x817BC.
- **Target 0x1F6A70:** ret 4, regs -, len 76, calls 1.
- **Invariants held:** ret, regs, len -6 (band ±10), calls, distinctive k 0x417BC 0x817BC.
- **Alignment:** score 7.28, lead 7.28; after `sub_0024A130` = 0x1F69F0, before `D3DDevice_SetRenderState_FillMode` = 0x1F6AC0.
- **Exclusivity:** best credible rival in the bracket none; in the section none.
- **Cross-check:** `found-d3d8-state.toml` gives 0x1F6A70 (agrees).

### `D3DDevice_SetRenderState_VertexBlend` = 0x1F6BB0

- **Bracket (tight):** `D3DDevice_SetRenderState_LineWidth` (0x1F6910; 5558 0x24A040) … `D3DDevice_SetRenderState_StencilFail` (0x1F73F0; 5558 0x24AFE0).
- **Reference:** 5558 0x24A340; ret 4, regs -, len 65, calls 1; stable k 0x40328.
- **Target 0x1F6BB0:** ret 4, regs -, len 50, calls 1.
- **Invariants held:** ret, regs, calls, distinctive k 0x40328.
- **Alignment:** score 4.62, lead 4.62; after `D3DDevice_SetRenderState_TwoSidedLighting` = 0x1F6B50, before `D3DDevice_SetTextureState_TexCoordIndex` = 0x1F6BF0.
- **Exclusivity:** best credible rival in the bracket none; in the section none.
- **Cross-check:** `found-d3d8-state.toml` gives 0x1F6BB0 (agrees).

### `D3DDevice_SetTextureState_BumpEnv` = 0x1F6CD0

- **Bracket (tight):** `D3DDevice_SetRenderState_LineWidth` (0x1F6910; 5558 0x24A040) … `D3DDevice_SetRenderState_StencilFail` (0x1F73F0; 5558 0x24AFE0).
- **Reference:** 5558 0x24A4A0; ret 12, regs -, len 99, calls 1; stable k 0x41AD0.
- **Target 0x1F6CD0:** ret 12, regs -, len 93, calls 1.
- **Invariants held:** ret, regs, len -6 (band ±12), calls, distinctive k 0x41AD0.
- **Alignment:** score 7.49, lead 7.49; after `D3DDevice_SetTextureState_TexCoordIndex` = 0x1F6BF0, before `D3DDevice_SetTextureState_BorderColor` = 0x1F6D30.
- **Exclusivity:** best credible rival in the bracket none; in the section none.
- **Cross-check:** `found-d3d8-state.toml` gives 0x1F6CD0 (agrees).

### `D3DDevice_SetTextureState_BorderColor` = 0x1F6D30

- **Bracket (tight):** `D3DDevice_SetRenderState_LineWidth` (0x1F6910; 5558 0x24A040) … `D3DDevice_SetRenderState_StencilFail` (0x1F73F0; 5558 0x24AFE0).
- **Reference:** 5558 0x24A510; ret 8, regs -, len 61, calls 1; stable k 0x41B24.
- **Target 0x1F6D30:** ret 8, regs -, len 55, calls 1.
- **Invariants held:** ret, regs, len -6 (band ±8), calls, distinctive k 0x41B24.
- **Alignment:** score 7.00, lead 7.00; after `D3DDevice_SetTextureState_BumpEnv` = 0x1F6CD0, before `D3DDevice_SetTextureState_ColorKeyColor` = 0x1F6D70.
- **Exclusivity:** best credible rival in the bracket none; in the section none.
- **Cross-check:** `found-d3d8-state.toml` gives 0x1F6D30 (agrees).

### `D3DDevice_SetTextureState_ColorKeyColor` = 0x1F6D70

- **Bracket (tight):** `D3DDevice_SetRenderState_LineWidth` (0x1F6910; 5558 0x24A040) … `D3DDevice_SetRenderState_StencilFail` (0x1F73F0; 5558 0x24AFE0).
- **Reference:** 5558 0x24A550; ret 8, regs -, len 57, calls 1; stable k 0x40AE0.
- **Target 0x1F6D70:** ret 8, regs -, len 51, calls 1.
- **Invariants held:** ret, regs, len -6 (band ±8), calls, distinctive k 0x40AE0.
- **Alignment:** score 7.00, lead 7.00; after `D3DDevice_SetTextureState_BorderColor` = 0x1F6D30, before `sub_0024A690` = 0x1F6E40.
- **Exclusivity:** best credible rival in the bracket none; in the section none.
- **Cross-check:** `found-d3d8-state.toml` gives 0x1F6D70 (agrees).

### `D3DDevice_SetRenderState_ZEnable` = 0x1F7300

- **Bracket (tight):** `D3DDevice_SetRenderState_LineWidth` (0x1F6910; 5558 0x24A040) … `D3DDevice_SetRenderState_StencilFail` (0x1F73F0; 5558 0x24AFE0).
- **Reference:** 5558 0x24AEC0; ret 4, regs -, len 143, calls 6; stable k 0x4030C 0x41D78.
- **Target 0x1F7300:** ret 4, regs -, len 108, calls 5.
- **Invariants held:** ret, regs, calls ±1, distinctive k 0x4030C.
- **Alignment:** score 3.46, lead 3.46; after `sub_0024A9F0` = 0x1F7230, before `D3DDevice_SetRenderState_StencilEnable` = 0x1F7370.
- **Exclusivity:** best credible rival in the bracket none; in the section none.
- **Cross-check:** `found-d3d8-state.toml` gives 0x1F7300 (agrees).

### `D3DDevice_SetRenderState_StencilEnable` = 0x1F7370

- **Bracket (tight):** `D3DDevice_SetRenderState_LineWidth` (0x1F6910; 5558 0x24A040) … `D3DDevice_SetRenderState_StencilFail` (0x1F73F0; 5558 0x24AFE0).
- **Reference:** 5558 0x24AF50; ret 4, regs -, len 132/152, calls 1; stable k 0x4032C 0x41D84.
- **Target 0x1F7370:** ret 4, regs -, len 128, calls 1.
- **Invariants held:** ret, regs, len -4 (band ±16), calls, distinctive k 0x4032C 0x41D84.
- **Alignment:** score 7.99, lead 7.99; after `D3DDevice_SetRenderState_ZEnable` = 0x1F7300, before anchor `D3DDevice_SetRenderState_StencilFail` = 0x1F73F0.
- **Exclusivity:** best credible rival in the bracket none; in the section none.
- **Cross-check:** `found-d3d8-state.toml` gives 0x1F7370 (agrees).

### `D3DDevice_CreateVertexShader` = 0x1F7EC0

- **Bracket (loose):** `D3DDevice_SetRenderState_StencilFail` (0x1F73F0; 5558 0x24AFE0) … `D3DDevice_DrawVertices` (0x2014D0; 5558 0x24DC90).
- **Reference:** 5558 0x24B8B0; ret 16, regs -, len 318, calls 5; stable k 0x8007000E.
- **Target 0x1F7EC0:** ret 16, regs -, len 318, calls 5.
- **Invariants held:** ret, regs, len exact, calls, k 0x8007000E.
- **Alignment:** score 7.50, lead 7.50; after `sub_0024B7B0` = 0x1F7DC0, before `D3DDevice_SetStreamSource` = 0x1F8000.
- **Exclusivity:** best credible rival in the bracket none; in the section none.
- **Cross-check:** `found-d3d8-core.toml` gives 0x1F7EC0 (agrees).

### `D3DDevice_SetStreamSource` = 0x1F8000

- **Bracket (loose):** `D3DDevice_SetRenderState_StencilFail` (0x1F73F0; 5558 0x24AFE0) … `D3DDevice_DrawVertices` (0x2014D0; 5558 0x24DC90).
- **Reference:** 5558 0x24BE90; ret 12, regs -, len 140, calls 1; stable k 0x80000 0xFFF80000.
- **Target 0x1F8000:** ret 12, regs -, len 133, calls 2.
- **Invariants held:** ret, regs, len -7 (band ±17), calls ±1, k 0xFFF80000.
- **Alignment:** score 5.17, lead 5.17; after `D3DDevice_CreateVertexShader` = 0x1F7EC0, before `D3DDevice_LoadVertexShader` = 0x1F8090.
- **Exclusivity:** best credible rival in the bracket none; in the section none.
- **Cross-check:** `found-d3d8-state.toml` gives 0x1F8000 (agrees).

### `D3DDevice_LoadVertexShader` = 0x1F8090

- **Bracket (loose):** `D3DDevice_SetRenderState_StencilFail` (0x1F73F0; 5558 0x24AFE0) … `D3DDevice_DrawVertices` (0x2014D0; 5558 0x24DC90).
- **Reference:** 5558 0x24BFC0; ret 8, regs -, len 84, calls 2; stable k 0x41E9C.
- **Target 0x1F8090:** ret 8, regs -, len 84, calls 2.
- **Invariants held:** ret, regs, len exact, calls, k 0x41E9C.
- **Alignment:** score 7.50, lead 3.83; after `D3DDevice_SetStreamSource` = 0x1F8000, before `D3DDevice_SelectVertexShader` = 0x1F80F0.
- **Exclusivity:** best credible rival in the bracket none; in the section none.
- **Cross-check:** `found-d3d8-core.toml` gives 0x1F8090 (agrees).

## Rejections

First rule each rejected name fails, in the order the rules are checked:

| Reason | D3D | DSOUND | XPP | all |
| --- | ---: | ---: | ---: | ---: |
| alignment leaves it unmatched | 21 | 45 | 0 | 66 |
| nearest anchors inverted in the target | 19 | 6 | 0 | 25 |
| anchored on one side only | 9 | 2 | 0 | 11 |
| not exclusive in its bracket | 8 | 0 | 0 | 8 |
| loose bracket: length or section exclusivity | 5 | 0 | 0 | 5 |
| alignment ambiguous | 4 | 0 | 0 | 4 |
| length outside band, no distinctive constant | 3 | 0 | 0 | 3 |
| calls outside ±1 | 2 | 0 | 0 | 2 |
| regs differ | 1 | 0 | 0 | 1 |

Every check the alignment's own candidate fails, counted over the rejected names that have one (a name can fail several):

| Check | Failed |
| --- | ---: |
| (rejected names with a candidate) | 32 |
| regs differ | 1 |
| calls outside ±1 | 6 |
| shares no reference constant | 1 |
| length outside band | 10 |
| length outside band, no distinctive constant | 4 |
| alignment lead too small | 10 |
| not exclusive in bracket | 13 |
| not bracketed by two anchors | 9 |
| ret differs | 0 (a pairing with another ret is never made) |

## Candidates not emitted

### Strongest leads (16)

Names whose pick passes every invariant (ret, regs, calls, constants and the length band) and is held back only by position or ambiguity. They are where static RE pays off first; they are still not proof (the next table shows each one's competitors).

| Name | Pick | Score | Held back because |
| --- | --- | ---: | --- |
| `D3DDevice_BlockUntilVerticalBlank` | 0x1F5520 | 6.0 | its nearest anchors are in opposite order in the target (a moved block) |
| `D3DDevice_InsertFence` | 0x1F5550 | 6.0 | its nearest anchors are in opposite order in the target (a moved block) |
| `D3DDevice_Release` | 0x1F54D0 | 6.0 | its nearest anchors are in opposite order in the target (a moved block) |
| `D3DDevice_SetRenderState_DoNotCullUncompressed` | 0x1F7590 | 6.0 | not exclusive in its bracket: 0x1F7550 also passes, scoring 6.00 against 6.00; loose bracket and not exclusive in D3D: 0x1F7550 also passes, scoring 6.00 against 6.00 |
| `D3DDevice_SetRenderState_RopZCmpAlwaysRead` | 0x1F7550 | 6.0 | not exclusive in its bracket: 0x1F7570 also passes, scoring 6.00 against 6.00; loose bracket and not exclusive in D3D: 0x1F7570 also passes, scoring 6.00 against 6.00 |
| `D3DDevice_SetRenderState_RopZRead` | 0x1F7570 | 6.0 | not exclusive in its bracket: 0x1F7550 also passes, scoring 6.00 against 6.00; loose bracket and not exclusive in D3D: 0x1F7550 also passes, scoring 6.00 against 6.00 |
| `D3DDevice_SetRenderState_OcclusionCullEnable` | 0x1F7490 | 6.0 | not exclusive in its bracket: 0x1F74F0 also passes, scoring 5.98 against 5.98; loose bracket and not exclusive in D3D: 0x1F74F0 also passes, scoring 5.98 against 5.98 |
| `D3DDevice_SetRenderState_StencilCullEnable` | 0x1F74F0 | 6.0 | not exclusive in its bracket: 0x1F7490 also passes, scoring 5.98 against 5.98; loose bracket and not exclusive in D3D: 0x1F7490 also passes, scoring 5.98 against 5.98 |
| `D3DDevice_SetTransform` | 0x1F5360 | 5.8 | its nearest anchors are in opposite order in the target (a moved block) |
| `D3DDevice_SetRenderState_BackFillMode` | 0x1F6B00 | 5.8 | ambiguous alignment: lead 0.52 < 2.0; not exclusive in its bracket: 0x1F6B50 also passes, scoring 6.38 against 5.77 |
| `D3DDevice_SetRenderState_FillMode` | 0x1F6AC0 | 5.5 | ambiguous alignment: lead 0.76 < 2.0; not exclusive in its bracket: 0x1F6B00 also passes, scoring 5.28 against 5.53 |
| `D3DDevice_BeginPush` | 0x1FC580 | 5.0 | ambiguous alignment: lead 0.76 < 2.0; not exclusive in its bracket: 0x1FA6F0 also passes, scoring 5.01 against 5.01; loose bracket and not exclusive in D3D: 0x1FA6F0 also passes, scoring 5.01 against 5.01 |
| `D3DDevice_DeleteVertexShader` | 0x1F82A0 | 5.0 | not exclusive in its bracket: 0x1FCAE0 also passes, scoring 4.25 against 5.00; loose bracket and not exclusive in D3D: 0x1FCAE0 also passes, scoring 4.25 against 5.00 |
| `D3DDevice_MakeSpace` | 0x1FB300 | 5.0 | ambiguous alignment: lead 0.76 < 2.0; loose bracket and not exclusive in D3D: 0x1F5550 also passes, scoring 3.75 against 5.00 |
| `D3DDevice_SetRenderState_PSTextureModes` | 0x1F6620 | 5.0 | not exclusive in its bracket: 0x1F65F0 also passes, scoring 3.75 against 5.00; loose bracket and not exclusive in D3D: 0x1FCB00 also passes, scoring 5.25 against 5.00 |
| `D3DDevice_SetRenderState_YuvEnable` | 0x1F7460 | 4.0 | not exclusive in its bracket: 0x1FA6B0 also passes, scoring 3.00 against 4.00; loose bracket and not exclusive in D3D: 0x1F6730 also passes, scoring 4.25 against 4.00 |

### All, by section

Ranked within each section by the best pair score any candidate reaches, so the most promising names for static RE come first. *Pick* is the alignment's choice in the name's bracket (score, lead, failed checks); *Others* are the best-scoring functions anywhere in the section with the same `ret`. An address already held by an anchor or an emitted name is marked `= Name`. A pick or an alternative is a lead for a human, **not** evidence: no row here passed the emission rule.

#### D3D (72)

| Name | 5558 | Reference | Bracket | Pick | Others | Why not emitted |
| --- | --- | --- | --- | --- | --- | --- |
| `Lock2DSurface` | 0x250FC0 | ret 24, regs -, len 155, calls 2 | forward D3DDevice_DrawIndexedVertices … edge | unmatched | 0x1FD160 9.0 (none); 0x1FC1B0 -9.5 (len +739, calls ±8, k) | anchored on one side only: no anchor above it in 5558 order; the alignment leaves it unmatched |
| `D3DDevice_Clear` | 0x248570 | ret 24, regs -, len 927, calls 10 | loose D3DDevice_SetPalette … D3DDevice_SetRenderState_FogColor | unmatched | 0x1FC1B0 7.9 (none); 0x1FD160 -9.5 (len -772, calls ±8, k) | the alignment leaves it unmatched |
| `D3DDevice_SetRenderState_BackFillMode` | 0x24A260 | ret 4, regs -, len 81, calls 1 | tight D3DDevice_SetRenderState_LineWidth … D3DDevice_SetRenderState_StencilFail | 0x1F6B00 score 5.8, lead 0.5; fails: none | 0x1F6B50 6.4 (none); 0x1FC800 3.4 (k); 0x1F6AC0 3.0 (len -19) | ambiguous alignment: lead 0.52 < 2.0; not exclusive in its bracket: 0x1F6B50 also passes, scoring 6.38 against 5.77 |
| `D3DDevice_SetRenderState_SampleAlpha` | 0x24B2F0 | ret 4, regs -, len 78, calls 1 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | 0x1F7600 score 6.1, lead 3.4; fails: calls ±2 | 0x1F75B0 6.1 (calls ±2); 0x1FC800 4.5 (k); 0x1F68C0 = `D3DDevice_SetRenderState_TextureFactor` 3.6 (k) | calls off by 2 |
| `D3DDevice_BlockUntilVerticalBlank` | 0x245490 | ret 0, regs -, len 36, calls 0 | inverted D3D_InternalRelease … D3DDevice_SetPalette | 0x1F5520 score 6.0, lead 6.0; fails: none | 0x1FD410 4.2 (none); 0x1FACA0 3.8 (none); 0x1F65C0 2.4 (len +9) | its nearest anchors are in opposite order in the target (a moved block) |
| `D3DDevice_InsertFence` | 0x245790 | ret 0, regs -, len 8, calls 1 | inverted D3D_InternalRelease … D3DDevice_SetPalette | 0x1F5550 score 6.0, lead 2.0; fails: none | 0x1FB300 2.4 (len +9); 0x1F54D0 0.0 (len +47); 0x1F7230 0.0 (len +192) | its nearest anchors are in opposite order in the target (a moved block) |
| `D3DDevice_Release` | 0x245370 | ret 0, regs -, len 55, calls 1 | inverted D3D_InternalRelease … D3DDevice_SetPalette | 0x1F54D0 score 6.0, lead 6.0; fails: none | 0x1F65C0 1.2 (len -10); 0x1FAEA0 1.2 (len -10); 0x1F5520 0.1 (len -19) | its nearest anchors are in opposite order in the target (a moved block) |
| `D3DDevice_SetRenderState_DoNotCullUncompressed` | 0x24B1A0 | ret 4, regs -, len 17, calls 1 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | 0x1F7590 score 6.0, lead 5.2; fails: none | 0x1F7550 6.0 (none); 0x1F7570 6.0 (none); 0x1FB3B0 5.0 (none) | not exclusive in its bracket: 0x1F7550 also passes, scoring 6.00 against 6.00; loose bracket and not exclusive in D3D: 0x1F7550 also passes, scoring 6.00 against 6.00 |
| `D3DDevice_SetRenderState_RopZCmpAlwaysRead` | 0x24B160 | ret 4, regs -, len 17, calls 1 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | 0x1F7550 score 6.0, lead 5.2; fails: none | 0x1F7570 6.0 (none); 0x1F7590 6.0 (none); 0x1FB3B0 5.0 (none) | not exclusive in its bracket: 0x1F7570 also passes, scoring 6.00 against 6.00; loose bracket and not exclusive in D3D: 0x1F7570 also passes, scoring 6.00 against 6.00 |
| `D3DDevice_SetRenderState_RopZRead` | 0x24B180 | ret 4, regs -, len 17, calls 1 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | 0x1F7570 score 6.0, lead 5.2; fails: none | 0x1F7550 6.0 (none); 0x1F7590 6.0 (none); 0x1FB3B0 5.0 (none) | not exclusive in its bracket: 0x1F7550 also passes, scoring 6.00 against 6.00; loose bracket and not exclusive in D3D: 0x1F7550 also passes, scoring 6.00 against 6.00 |
| `D3DResource_BlockUntilNotBusy` | 0x244BF0 | ret jmp, regs -, len 5, calls 1 | loose D3DResource_IsBusy … D3D_InternalRelease | unmatched | 0x1F5510 6.0 (none); 0x1FC680 6.0 (none); 0x1FCC50 6.0 (none) | the alignment leaves it unmatched |
| `D3D_KickOffAndWaitForIdle` | 0x24D610 | ret 0, regs -, len 17, calls 1 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | unmatched | 0x1FB300 6.0 (none); 0x1F5550 2.4 (len -9); 0x1FACA0 1.0 (len +12) | the alignment leaves it unmatched |
| `D3DDevice_EndPush` | 0x24D0C0 | ret 4, regs -, len 15, calls 0 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | unmatched | 0x1FC690 6.0 (none); 0x1FCA00 4.5 (none); 0x1F7550 4.0 (none) | the alignment leaves it unmatched |
| `D3DDevice_SetRenderState_OcclusionCullEnable` | 0x24B080 | ret 4, regs -, len 98, calls 1 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | 0x1F7490 score 6.0, lead 6.0; fails: none | 0x1F74F0 6.0 (none); 0x1FCA20 3.5 (k); 0x1FA640 2.8 (k) | not exclusive in its bracket: 0x1F74F0 also passes, scoring 5.98 against 5.98; loose bracket and not exclusive in D3D: 0x1F74F0 also passes, scoring 5.98 against 5.98 |
| `D3DDevice_SetRenderState_StencilCullEnable` | 0x24B0F0 | ret 4, regs -, len 98, calls 1 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | 0x1F74F0 score 6.0, lead 6.0; fails: none | 0x1F7490 6.0 (none); 0x1FCA20 3.5 (k); 0x1FA640 2.8 (k) | not exclusive in its bracket: 0x1F7490 also passes, scoring 5.98 against 5.98; loose bracket and not exclusive in D3D: 0x1F7490 also passes, scoring 5.98 against 5.98 |
| `D3DDevice_SetRenderTarget` | 0x246040 | ret 8, regs -, len 628, calls 11 | inverted D3D_InternalRelease … D3DDevice_SetPalette | 0x1F5640 score 5.9, lead 5.9; fails: calls ±2 | 0x1FB020 -3.5 (len -397, calls ±5); 0x1F5CD0 -4.1 (len -156, calls ±4, k); 0x1F5F90 -7.5 (len -284, calls ±6, k) | its nearest anchors are in opposite order in the target (a moved block); calls off by 2 |
| `D3DDevice_SetTransform` | 0x244FE0 | ret 8, regs -, len 280, calls 2 | inverted D3D_InternalRelease … D3DDevice_SetPalette | 0x1F5360 score 5.8, lead 3.1; fails: none | 0x1F79A0 1.5 (calls ±2, k); 0x1F7DC0 0.0 (calls ±2, k); 0x1F5EB0 -0.7 (len -58, k) | its nearest anchors are in opposite order in the target (a moved block) |
| `D3D_BlockOnResource` | 0x24D630 | ret 4, regs -, len 120, calls 5 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | 0x1FC6A0 score 2.5, lead 0.8; fails: len +36 | 0x1FB320 5.8 (none); 0x1F69F0 3.4 (k); 0x1F7300 = `D3DDevice_SetRenderState_ZEnable` 2.3 (k) | len +36 outside ±14 with no distinctive constant; ambiguous alignment: lead 0.76 < 2.0; not exclusive in its bracket: 0x1FB320 also passes, scoring 5.75 against 2.50; loose bracket and length outside band; loose bracket and not exclusive in D3D: 0x1FB320 also passes, scoring 5.75 against 2.50 |
| `D3DDevice_SetRenderState_MultiSampleMask` | 0x24B2A0 | ret 4, regs -, len 80, calls 1 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | 0x1F75B0 score 5.7, lead 2.9; fails: calls ±2 | 0x1F7600 5.7 (calls ±2); 0x1FC800 3.6 (k); 0x1F68C0 = `D3DDevice_SetRenderState_TextureFactor` 3.2 (k) | calls off by 2 |
| `D3DDevice_SetRenderState_FillMode` | 0x24A210 | ret 4, regs -, len 68, calls 1 | tight D3DDevice_SetRenderState_LineWidth … D3DDevice_SetRenderState_StencilFail | 0x1F6AC0 score 5.5, lead 0.8; fails: none | 0x1F6B00 5.3 (none); 0x1FA6F0 3.5 (k); 0x1FC580 3.5 (k) | ambiguous alignment: lead 0.76 < 2.0; not exclusive in its bracket: 0x1F6B00 also passes, scoring 5.28 against 5.53 |
| `D3DDevice_SetPixelShader` | 0x24E740 | ret 4, regs -, len 499, calls 5 | forward D3DDevice_DrawIndexedVertices … edge | 0x201E80 score 1.2, lead 1.2; fails: calls ±2, k | 0x202320 5.5 (len -61); 0x1F8E20 5.4 (calls ±2); 0x1F8A20 1.9 (calls ±2, k) | anchored on one side only: no anchor above it in 5558 order; calls off by 2; shares none of the reference's stable constants; ambiguous alignment: lead 1.23 < 2.0; not exclusive in its bracket: 0x202320 also passes, scoring 5.48 against 1.23 |
| `D3DDevice_SetShaderConstantMode` | 0x24C180 | ret 4, regs -, len 280, calls 5 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | 0x1F8160 score 5.4, lead 5.4; fails: len -37 | 0x1F9650 -0.1 (len +36, k); 0x1F5B70 -1.0 (len +67, k); 0x1F69F0 -1.5 (len -164, k) | loose bracket and length outside band |
| `D3DDevice_EnableOverlay` | 0x259700 | ret 4, regs -, len 99, calls 0 | forward D3DDevice_DrawIndexedVertices … edge | unmatched | 0x1FCA20 5.3 (none); 0x1FA640 5.2 (none); 0x1F5560 5.0 (none) | anchored on one side only: no anchor above it in 5558 order; the alignment leaves it unmatched |
| `D3DDevice_SetSoftDisplayFilter` | 0x245670 | ret 4, regs -, len 81, calls 0 | inverted D3D_InternalRelease … D3DDevice_SetPalette | unmatched | 0x1FC9A0 5.3 (none); 0x1F6B50 3.9 (none); 0x1FC800 3.9 (none) | its nearest anchors are in opposite order in the target (a moved block); the alignment leaves it unmatched |
| `D3DDevice_SetRenderState_MultiSampleMode` | 0x24B1C0 | ret 4, regs -, len 39, calls 1 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | unmatched | 0x1F5B40 5.2 (none); 0x1F65F0 5.0 (none); 0x1F54A0 4.2 (none) | the alignment leaves it unmatched |
| `D3DDevice_SetRenderState_MultiSampleRenderTargetMode` | 0x24B1F0 | ret 4, regs -, len 39, calls 1 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | unmatched | 0x1F5B40 5.2 (none); 0x1F65F0 5.0 (none); 0x1F54A0 4.2 (none) | the alignment leaves it unmatched |
| `D3DDevice_SetRenderState_PSTextureModes` | 0x2498B0 | ret 4, regs -, len 34, calls 0 | loose D3DDevice_SetPalette … D3DDevice_SetRenderState_FogColor | 0x1F6620 score 5.0, lead 2.9; fails: none | 0x1FCB00 5.2 (none); 0x1FDA90 5.0 (none); 0x1F54A0 4.5 (none) | not exclusive in its bracket: 0x1F65F0 also passes, scoring 3.75 against 5.00; loose bracket and not exclusive in D3D: 0x1FCB00 also passes, scoring 5.25 against 5.00 |
| `D3D_SetFence` | 0x24D240 | ret 4, regs -, len 184, calls 2 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | 0x1FAF80 score 5.1, lead 5.1; fails: len -31 | 0x1FC5D0 0.1 (calls ±2, k); 0x1F7460 -1.5 (len -147, k); 0x1F8730 -1.5 (len +175, k) | loose bracket and length outside band |
| `D3DDevice_BeginPush` | 0x24D5C0 | ret 4, regs -, len 68, calls 1 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | 0x1FC580 score 5.0, lead 0.8; fails: none | 0x1FA6F0 5.0 (none); 0x1F6AC0 4.0 (none); 0x1F6B00 3.8 (none) | ambiguous alignment: lead 0.76 < 2.0; not exclusive in its bracket: 0x1FA6F0 also passes, scoring 5.01 against 5.01; loose bracket and not exclusive in D3D: 0x1FA6F0 also passes, scoring 5.01 against 5.01 |
| `D3DDevice_BlockOnFence` | 0x2453C0 | ret 4, regs -, len 15, calls 1 | inverted D3D_InternalRelease … D3DDevice_SetPalette | unmatched | 0x1F7550 5.0 (none); 0x1F7570 5.0 (none); 0x1F7590 5.0 (none) | its nearest anchors are in opposite order in the target (a moved block); the alignment leaves it unmatched |
| `D3DDevice_DeleteVertexShader` | 0x24C2E0 | ret 4, regs -, len 27, calls 1 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | 0x1F82A0 score 5.0, lead 5.0; fails: none | 0x1FCAE0 4.2 (none); 0x1FDA90 3.8 (none); 0x1F5340 3.5 (none) | not exclusive in its bracket: 0x1FCAE0 also passes, scoring 4.25 against 5.00; loose bracket and not exclusive in D3D: 0x1FCAE0 also passes, scoring 4.25 against 5.00 |
| `D3DDevice_MakeSpace` | 0x24D5B0 | ret 0, regs -, len 15, calls 1 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | 0x1FB300 score 5.0, lead 0.8; fails: none | 0x1F5550 3.8 (none); 0x1FACA0 0.8 (len +14); 0x1FD410 0.5 (len +16) | ambiguous alignment: lead 0.76 < 2.0; loose bracket and not exclusive in D3D: 0x1F5550 also passes, scoring 3.75 against 5.00 |
| `D3DDevice_SetFlickerFilter` | 0x245620 | ret 4, regs -, len 66, calls 0 | inverted D3D_InternalRelease … D3DDevice_SetPalette | unmatched | 0x1FA6F0 5.0 (none); 0x1F6200 4.8 (none); 0x1F6AC0 3.5 (none) | its nearest anchors are in opposite order in the target (a moved block); the alignment leaves it unmatched |
| `D3DDevice_DeletePixelShader` | 0x24E2E0 | ret 4, regs -, len 29, calls 1 | forward D3DDevice_DrawIndexedVertices … edge | unmatched | 0x1F82A0 4.5 (none); 0x1FDA90 4.2 (none); 0x1FCAE0 3.8 (none) | anchored on one side only: no anchor above it in 5558 order; the alignment leaves it unmatched |
| `D3DDevice_IsFencePending` | 0x24D010 | ret 4, regs -, len 30, calls 0 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | unmatched | 0x1FCAE0 4.5 (none); 0x1FCB00 4.2 (none); 0x1F6620 4.0 (none) | the alignment leaves it unmatched |
| `D3DDevice_DrawVerticesUP` | 0x24D9E0 | ret 16, regs -, len 357, calls 5 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | 0x201390 score 4.5, lead 4.5; fails: len -44 | 0x1F7EC0 = `D3DDevice_CreateVertexShader` 2.2 (k); 0x202150 0.7 (calls ±2, k); 0x1FDDC0 -3.5 (len -195, calls ±2, k) | loose bracket and length outside band |
| `D3DDevice_GetBackBuffer2` | 0x2463E0 | ret 4, regs -, len 67, calls 2 | inverted D3D_InternalRelease … D3DDevice_SetPalette | unmatched | 0x1FA6F0 4.3 (none); 0x1FC580 3.8 (none); 0x1F6AC0 3.3 (none) | its nearest anchors are in opposite order in the target (a moved block); the alignment leaves it unmatched |
| `D3DDevice_GetDepthStencilSurface2` | 0x246B20 | ret 0, regs -, len 30, calls 1 | inverted D3D_InternalRelease … D3DDevice_SetPalette | unmatched | 0x1FACA0 4.2 (none); 0x1FD410 4.2 (none); 0x1F5520 3.0 (none) | its nearest anchors are in opposite order in the target (a moved block); the alignment leaves it unmatched |
| `D3DDevice_SetMaterial` | 0x2451D0 | ret 4, regs -, len 48, calls 0 | inverted D3D_InternalRelease … D3DDevice_SetPalette | unmatched | 0x1F6760 = `D3DDevice_SetRenderState_ShadowFunc` 4.2 (none); 0x1FA6B0 4.2 (none); 0x1FC740 4.2 (none) | its nearest anchors are in opposite order in the target (a moved block); the alignment leaves it unmatched |
| `D3DDevice_SetRenderState_YuvEnable` | 0x24B050 | ret 4, regs -, len 43, calls 2 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | 0x1F7460 score 4.0, lead 4.0; fails: none | 0x1F6730 = `D3DDevice_SetRenderState_EdgeAntiAlias` 4.2 (none); 0x1F6890 = `D3DDevice_SetRenderState_NormalizeNormals` 4.0 (none); 0x1F5B40 3.8 (none) | not exclusive in its bracket: 0x1FA6B0 also passes, scoring 3.00 against 4.00; loose bracket and not exclusive in D3D: 0x1F6730 also passes, scoring 4.25 against 4.00 |
| `D3DDevice_SelectVertexShader` | 0x24C090 | ret 8, regs -, len 156, calls 3 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | 0x1F80F0 score 4.1, lead 3.1; fails: len -44 | 0x1F6170 = `D3DDevice_SetIndices` 1.4 (k); 0x1F6240 -0.5 (len +28, k); 0x1FD640 -1.8 (len +33, calls ±2, k) | loose bracket and length outside band |
| `D3DDevice_Swap` | 0x2495F0 | ret 4, regs -, len 227/229, calls 9 | loose D3DDevice_SetPalette … D3DDevice_SetRenderState_FogColor | unmatched | 0x1F83C0 3.9 (none); 0x1F8160 0.5 (calls ±4); 0x1F9AB0 -2.0 (len +254, calls ±2) | the alignment leaves it unmatched |
| `D3DDevice_GetRenderTarget2` | 0x246B00 | ret 0, regs -, len 26, calls 1 | inverted D3D_InternalRelease … D3DDevice_SetPalette | unmatched | 0x1FACA0 3.8 (none); 0x1FD410 3.2 (none); 0x1FB300 2.4 (len -9) | its nearest anchors are in opposite order in the target (a moved block); the alignment leaves it unmatched |
| `D3DDevice_SetRenderState_TwoSidedLighting` | 0x24A2C0 | ret 4, regs -, len 128, calls 2 | tight D3DDevice_SetRenderState_LineWidth … D3DDevice_SetRenderState_StencilFail | 0x1F6B50 score 1.1, lead 0.5; fails: len -44 | 0x1F7370 = `D3DDevice_SetRenderState_StencilEnable` 3.5 (k); 0x1F55C0 2.1 (k); 0x1F6970 = `D3DDevice_SetRenderState_Dxt1NoiseEnable` 1.8 (k) | len -44 outside ±15 with no distinctive constant; ambiguous alignment: lead 0.52 < 2.0 |
| `D3DDevice_SetTextureState_TexCoordIndex` | 0x24A390 | ret 8, regs -, len 261, calls 1 | tight D3DDevice_SetRenderState_LineWidth … D3DDevice_SetRenderState_StencilFail | 0x1F6BF0 score 3.5, lead 3.5; fails: len -47 | 0x1F5360 2.9 (k); 0x1F7DC0 2.1 (k); 0x1F79A0 1.2 (k) | len -47 outside ±31 with no distinctive constant |
| `D3DDevice_SetTile` | 0x247A20 | ret jmp, regs -, len 10, calls 2 | loose D3DDevice_SetPalette … D3DDevice_SetRenderState_FogColor | unmatched | 0x1F5510 3.2 (none); 0x1FC680 3.2 (none); 0x1FCC50 3.2 (none) | the alignment leaves it unmatched |
| `D3DDevice_SetGammaRamp` | 0x244EC0 | ret 8, regs -, len 131, calls 1 | inverted D3D_InternalRelease … D3DDevice_SetPalette | unmatched | 0x1F6170 = `D3DDevice_SetIndices` 3.2 (none); 0x1F66A0 = `D3DDevice_SetRenderStateNotInline` 0.9 (len -41); 0x1F80F0 0.3 (len -19, calls ±2) | its nearest anchors are in opposite order in the target (a moved block); the alignment leaves it unmatched |
| `D3DDevice_SwitchTexture` | 0x245300 | ret 4, regs ecx,edx, len 48, calls 1 | inverted D3D_InternalRelease … D3DDevice_SetPalette | unmatched | 0x1F6700 3.0 (none); 0x1F6760 = `D3DDevice_SetRenderState_ShadowFunc` -0.8 (regs); 0x1FA6B0 -0.8 (regs) | its nearest anchors are in opposite order in the target (a moved block); the alignment leaves it unmatched |
| `D3DDevice_SetTexture` | 0x2471C0 | ret 8, regs -, len 420, calls 3 | inverted D3D_InternalRelease … D3DDevice_SetPalette | 0x1F5F90 score 3.0, lead 3.0; fails: len -76, calls ±2 | 0x1F94B0 0.7 (calls ±3, k); 0x1F6170 = `D3DDevice_SetIndices` 0.5 (len -279); 0x1FD640 -0.5 (len -231, calls ±2) | its nearest anchors are in opposite order in the target (a moved block); calls off by 2 |
| `D3DDevice_SetRenderState_MultiSampleAntiAlias` | 0x24B220 | ret 4, regs -, len 114, calls 4 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | unmatched | 0x1F75B0 2.7 (len -38); 0x1F7600 2.7 (len -38); 0x1F69F0 2.7 (k) | the alignment leaves it unmatched |
| `D3DDevice_Begin` | 0x24E200 | ret 4, regs -, len 53, calls 2 | forward D3DDevice_DrawIndexedVertices … edge | unmatched | 0x1FC740 2.5 (calls ±2, k); 0x1F6BB0 = `D3DDevice_SetRenderState_VertexBlend` 2.2 (k); 0x1FA6B0 2.0 (k) | anchored on one side only: no anchor above it in 5558 order; the alignment leaves it unmatched |
| `D3DDevice_SetVertexShaderConstant1` | 0x24BA10 | ret 0, regs ecx,edx, len 95, calls 1 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | unmatched | 0x1F6E40 2.5 (k); 0x1F7080 1.9 (k); 0x1FADB0 1.1 (k) | the alignment leaves it unmatched |
| `D3DDevice_SetLight` | 0x246D70 | ret 8, regs -, len 691, calls 7 | inverted D3D_InternalRelease … D3DDevice_SetPalette | unmatched | 0x1F5CD0 2.4 (len -219); 0x1FB020 -2.5 (len -460, k); 0x1F5F90 -3.5 (len -347, calls ±2, k) | its nearest anchors are in opposite order in the target (a moved block); the alignment leaves it unmatched |
| `D3DDevice_SetVertexShader` | 0x24C3F0 | ret 4, regs -, len 355, calls 7 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | 0x1F83C0 score 2.3, lead 2.3; fails: len -136 | 0x1F5B70 0.6 (calls ±3, k); 0x1F9650 -0.8 (calls ±3, k); 0x201E80 -0.8 (len +121, k) | loose bracket and length outside band |
| `D3DDevice_CreatePixelShader` | 0x24E290 | ret 8, regs -, len 72, calls 1 | forward D3DDevice_DrawIndexedVertices … edge | unmatched | 0x1FCCB0 2.3 (k); 0x1F6D30 = `D3DDevice_SetTextureState_BorderColor` 0.0 (len -17, k); 0x1F66A0 = `D3DDevice_SetRenderStateNotInline` -0.1 (len +18, k) | anchored on one side only: no anchor above it in 5558 order; the alignment leaves it unmatched |
| `D3DDevice_FlushVertexCache` | 0x2476F0 | ret 0, regs -, len 39, calls 1 | loose D3DDevice_SetPalette … D3DDevice_SetRenderState_FogColor | unmatched | 0x1F5520 2.2 (k); 0x1F65C0 1.5 (k); 0x1FAEA0 1.5 (k) | the alignment leaves it unmatched |
| `D3D_MakeRequestedSpace` | 0x24D470 | ret 8, regs -, len 311, calls 5 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | unmatched | 0x1F5F90 2.2 (k); 0x1FB020 -1.1 (len -80, k); 0x1F79A0 -2.2 (calls ±5, k) | the alignment leaves it unmatched |
| `D3DDevice_SetScreenSpaceOffset` | 0x2480F0 | ret 8, regs -, len 73, calls 3 | loose D3DDevice_SetPalette … D3DDevice_SetRenderState_FogColor | unmatched | 0x1FCCB0 2.0 (calls ±3); 0x1F8090 = `D3DDevice_LoadVertexShader` 1.2 (len +11); 0x1F80F0 0.0 (len +39) | the alignment leaves it unmatched |
| `D3D_InitDefaultState` | 0x24F940 | ret 0, regs -, len 194, calls 9 | forward D3DDevice_DrawIndexedVertices … edge | unmatched | 0x1FB580 2.0 (calls ±2); 0x1F9CA0 -1.0 (len -88); 0x1F6490 = `D3DDevice_PersistDisplay` -3.0 (len +95, calls ±3) | anchored on one side only: no anchor above it in 5558 order; the alignment leaves it unmatched |
| `D3DDevice_SetTextureStageStateNotInline` | 0x24A5A0 | ret 12, regs -, len 236, calls 4 | tight D3DDevice_SetRenderState_LineWidth … D3DDevice_SetRenderState_StencilFail | unmatched | 0x1FA560 1.4 (k); 0x1FA460 -1.0 (calls ±4, k); 0x1F6DB0 -1.5 (len -103, k) | the alignment leaves it unmatched |
| `D3DDevice_SetViewport` | 0x246B40 | ret 4, regs -, len 546, calls 8 | inverted D3D_InternalRelease … D3DDevice_SetPalette | unmatched | 0x201E80 1.4 (len -70); 0x1F9AB0 0.6 (calls ±3); 0x1F83C0 0.0 (len -327) | its nearest anchors are in opposite order in the target (a moved block); the alignment leaves it unmatched |
| `D3DDevice_LightEnable` | 0x247030 | ret 8, regs -, len 337, calls 1 | inverted D3D_InternalRelease … D3DDevice_SetPalette | unmatched | 0x1F79A0 1.3 (len -48); 0x1F5F90 1.2 (calls ±4); 0x1F5EB0 0.7 (len -115) | its nearest anchors are in opposite order in the target (a moved block); the alignment leaves it unmatched |
| `D3DDevice_GetVertexShaderFunction` | 0x24C870 | ret 12, regs -, len 188, calls 0 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | unmatched | 0x1FCE70 1.0 (calls ±2, k); 0x1F84A0 -0.3 (len -30, k); 0x1FA460 -0.7 (len +62, k) | the alignment leaves it unmatched |
| `CDevice_KickOff` | 0x24D120 | ret 0, regs ecx, len 176, calls 1 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | 0x1FAED0 score 0.6, lead 0.6; fails: regs | 0x20107C -0.2 (len -47, k); 0x1FFF25 -0.5 (len +31, k); 0x1FFE27 -1.5 (len +78, k) | regs differ; ambiguous alignment: lead 0.65 < 2.0 |
| `D3DDevice_End` | 0x24E240 | ret 0, regs -, len 65, calls 2 | forward D3DDevice_DrawIndexedVertices … edge | unmatched | 0x1F54D0 -0.2 (len -10, k); 0x1FEF30 -1.2 (len +10, calls ±2, k); 0x1FF880 -2.2 (len +18, calls ±2, k) | anchored on one side only: no anchor above it in 5558 order; the alignment leaves it unmatched |
| `D3D_BlockOnTime` | 0x24D300 | ret 8, regs -, len 358/362, calls 9 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | unmatched | 0x1F5F90 -0.7 (calls ±4, k); 0x1FB020 -1.0 (len -127, calls ±3); 0x1F5CD0 -2.5 (len +110, calls ±2, k) | the alignment leaves it unmatched |
| `D3DDevice_SetVertexShaderConstant1Fast` | 0x24BA70 | ret 0, regs ecx,edx, len 73, calls 1 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | unmatched | 0x1F7080 -0.8 (len +16, k); 0x1F6650 = `D3DDevice_SetRenderState_Simple` -1.2 (len -28, k); 0x1F6E40 -1.9 (len +25, k) | the alignment leaves it unmatched |
| `D3DDevice_CopyRects` | 0x2467D0 | ret 20, regs -, len 803, calls 6 | inverted D3D_InternalRelease … D3DDevice_SetPalette | unmatched | 0x1F58E0 -1.1 (len -198, k); 0x201810 -4.5 (len -726, calls ±3, k); 0x1F7880 -5.5 (len -714, calls ±4, k) | its nearest anchors are in opposite order in the target (a moved block); the alignment leaves it unmatched |
| `D3DDevice_SetPixelShaderConstant` | 0x24E940 | ret 12, regs -, len 711, calls 1 | forward D3DDevice_DrawIndexedVertices … edge | unmatched | 0x1F58A0 -1.5 (len -659, k); 0x1F6CD0 = `D3DDevice_SetTextureState_BumpEnv` -1.5 (len -618, k); 0x1F84A0 -1.5 (len -553, k) | anchored on one side only: no anchor above it in 5558 order; the alignment leaves it unmatched |
| `D3DDevice_SetVertexShaderConstant4` | 0x24BAC0 | ret 0, regs ecx,edx, len 162, calls 1 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | unmatched | 0x1F6650 = `D3DDevice_SetRenderState_Simple` -1.5 (len -117, k); 0x1FADB0 -1.9 (len -56, k); 0x1F6E40 -2.3 (len -64, k) | the alignment leaves it unmatched |
| `D3DDevice_SetVertexShaderConstantNotInlineFast` | 0x24BB70 | ret 4, regs ecx,edx, len 208, calls 2 | loose D3DDevice_SetRenderState_StencilFail … D3DDevice_DrawVertices | unmatched | 0x1F6700 -3.5 (len -166, calls ±2, k); 0x1FED30 -4.9 (regs, calls ±2, k); 0x1F8160 -5.4 (regs, len +35, calls ±3) | the alignment leaves it unmatched |
| `D3DDevice_SetScissors` | 0x247C40 | ret 12, regs -, len 768, calls 13 | loose D3DDevice_SetPalette … D3DDevice_SetRenderState_FogColor | unmatched | 0x1F6300 -5.0 (len -376, calls ±8); 0x1FF9D0 -8.4 (len -128, calls ±9, k); 0x1F6DB0 -10.5 (len -635, calls ±9, k) | the alignment leaves it unmatched |

#### DSOUND (53)

| Name | 5558 | Reference | Bracket | Pick | Others | Why not emitted |
| --- | --- | --- | --- | --- | --- | --- |
| `IDirectSoundBuffer_SetFilter` | 0x28DF4A | ret 8, regs -, len 28, calls 1 | loose IDirectSoundBuffer_SetVolume … IDirectSoundBuffer_Play | unmatched | 0x207253 6.0 (none); 0x20DF9A 5.2 (none); 0x208E1F 4.0 (none) | the alignment leaves it unmatched |
| `IDirectSoundBuffer_SetHeadroom` | 0x28DF66 | ret 8, regs -, len 28, calls 1 | loose IDirectSoundBuffer_SetVolume … IDirectSoundBuffer_Play | unmatched | 0x207253 6.0 (none); 0x20DF9A 5.2 (none); 0x208E1F 4.0 (none) | the alignment leaves it unmatched |
| `IDirectSoundBuffer_SetI3DL2Source` | 0x28FFE5 | ret 12, regs -, len 32, calls 1 | loose IDirectSoundBuffer_SetVelocity … IDirectSoundBuffer_SetBufferData | unmatched | 0x207296 6.0 (none); 0x2079CB 6.0 (none); 0x207612 4.2 (none) | the alignment leaves it unmatched |
| `IDirectSoundBuffer_SetLoopRegion` | 0x28E062 | ret 12, regs -, len 32, calls 1 | inverted IDirectSoundBuffer_Stop … IDirectSoundBuffer_GetStatus | unmatched | 0x207296 6.0 (none); 0x2079CB 6.0 (none); 0x207612 4.2 (none) | its nearest anchors are in opposite order in the target (a moved block); no target function in the bracket |
| `IDirectSoundBuffer_SetMixBinVolumes` | 0x28DF82 | ret 8, regs -, len 28, calls 1 | loose IDirectSoundBuffer_SetVolume … IDirectSoundBuffer_Play | unmatched | 0x207253 6.0 (none); 0x20DF9A 5.2 (none); 0x208E1F 4.0 (none) | the alignment leaves it unmatched |
| `IDirectSoundBuffer_SetMixBins` | 0x28FDCE | ret 8, regs -, len 28, calls 1 | inverted IDirectSoundBuffer_SetFrequency … IDirectSoundBuffer_SetMaxDistance | unmatched | 0x207253 6.0 (none); 0x20DF9A 5.2 (none); 0x208E1F 4.0 (none) | its nearest anchors are in opposite order in the target (a moved block); no target function in the bracket |
| `IDirectSoundStream_SetEG` | 0x28E18D | ret jmp, regs -, len 5, calls 1 | loose IDirectSoundBuffer_Lock … DirectSoundDoWork | unmatched | 0x20772C 6.0 (none); 0x207731 6.0 (none); 0x20794E 6.0 (none) | no target function in the bracket |
| `IDirectSound_EnableHeadphones` | 0x28DDDC | ret 8, regs -, len 28, calls 1 | loose IDirectSound_DownloadEffectsImage … IDirectSoundBuffer_SetVolume | unmatched | 0x207253 6.0 (none); 0x20DF9A 5.2 (none); 0x208E1F 4.0 (none) | the alignment leaves it unmatched |
| `IDirectSound_GetSpeakerConfig` | 0x28DD30 | ret 8, regs -, len 28, calls 1 | loose DirectSoundUseFullHRTF … IDirectSound_DownloadEffectsImage | unmatched | 0x207253 6.0 (none); 0x20DF9A 5.2 (none); 0x208E1F 4.0 (none) | the alignment leaves it unmatched |
| `IDirectSound_SetMixBinHeadroom` | 0x28DDF8 | ret 12, regs -, len 32, calls 1 | loose IDirectSound_DownloadEffectsImage … IDirectSoundBuffer_SetVolume | unmatched | 0x207296 6.0 (none); 0x2079CB 6.0 (none); 0x207612 4.2 (none) | the alignment leaves it unmatched |
| `IDirectSoundBuffer_Pause` | 0x28E022 | ret 8, regs -, len 28, calls 1 | inverted IDirectSoundBuffer_Stop … IDirectSoundBuffer_GetStatus | unmatched | 0x207253 6.0 (none); 0x20DF9A 5.2 (none); 0x208E1F 4.0 (none) | its nearest anchors are in opposite order in the target (a moved block); no target function in the bracket |
| `IDirectSoundBuffer_SetAllParameters` | 0x28FDEA | ret 12, regs -, len 32, calls 1 | inverted IDirectSoundBuffer_SetFrequency … IDirectSoundBuffer_SetMaxDistance | unmatched | 0x207296 6.0 (none); 0x2079CB 6.0 (none); 0x207612 4.2 (none) | its nearest anchors are in opposite order in the target (a moved block); no target function in the bracket |
| `IDirectSoundBuffer_SetFormat` | 0x290792 | ret 8, regs -, len 28, calls 1 | loose IDirectSoundBuffer_SetVelocity … IDirectSoundBuffer_SetBufferData | unmatched | 0x207253 6.0 (none); 0x20DF9A 5.2 (none); 0x208E1F 4.0 (none) | the alignment leaves it unmatched |
| `IDirectSoundBuffer_SetOutputBuffer` | 0x28FDB2 | ret 8, regs -, len 28, calls 1 | inverted IDirectSoundBuffer_SetFrequency … IDirectSoundBuffer_SetMaxDistance | unmatched | 0x207253 6.0 (none); 0x20DF9A 5.2 (none); 0x208E1F 4.0 (none) | its nearest anchors are in opposite order in the target (a moved block); no target function in the bracket |
| `IDirectSoundBuffer_SetPlayRegion` | 0x290005 | ret 12, regs -, len 32, calls 1 | loose IDirectSoundBuffer_SetVelocity … IDirectSoundBuffer_SetBufferData | unmatched | 0x207296 6.0 (none); 0x2079CB 6.0 (none); 0x207612 4.2 (none) | the alignment leaves it unmatched |
| `IDirectSound_GetOutputLevels` | 0x28DE34 | ret 12, regs -, len 32, calls 1 | loose IDirectSound_DownloadEffectsImage … IDirectSoundBuffer_SetVolume | unmatched | 0x207296 6.0 (none); 0x2079CB 6.0 (none); 0x207612 4.2 (none) | the alignment leaves it unmatched |
| `IDirectSound_CommitEffectData` | 0x28DDC4 | ret 4, regs -, len 24, calls 1 | loose IDirectSound_DownloadEffectsImage … IDirectSoundBuffer_SetVolume | unmatched | 0x2091B0 5.2 (none); 0x209435 5.2 (none); 0x20D627 5.2 (none) | the alignment leaves it unmatched |
| `IDirectSound_SynchPlayback` | 0x28DE54 | ret 4, regs -, len 24, calls 1 | loose IDirectSound_DownloadEffectsImage … IDirectSoundBuffer_SetVolume | unmatched | 0x2091B0 5.2 (none); 0x209435 5.2 (none); 0x20D627 5.2 (none) | the alignment leaves it unmatched |
| `IDirectSound_GetEffectData` | 0x28DD73 | ret 20, regs -, len 39, calls 1 | loose IDirectSound_DownloadEffectsImage … IDirectSoundBuffer_SetVolume | unmatched | 0x207201 5.0 (none); 0x20722A 5.0 (none); 0x209673 4.0 (none) | the alignment leaves it unmatched |
| `IDirectSound_SetDopplerFactor` | 0x291318 | ret 12, regs -, len 36, calls 1 | loose IDirectSound_SetDistanceFactor … IDirectSound_SetI3DL2Listener | unmatched | 0x207296 4.5 (none); 0x2079CB 4.5 (none); 0x207612 3.8 (none) | no target function in the bracket |
| `IDirectSound_SetRolloffFactor` | 0x2913BB | ret 12, regs -, len 36, calls 1 | loose IDirectSound_SetDistanceFactor … IDirectSound_SetI3DL2Listener | unmatched | 0x207296 4.5 (none); 0x2079CB 4.5 (none); 0x207612 3.8 (none) | no target function in the bracket |
| `CDirectSoundStream_GetStatus` | 0x28CF50 | ret 8, regs -, len 81, calls 2 | loose DirectSoundUseFullHRTF … IDirectSound_DownloadEffectsImage | unmatched | 0x206B38 3.8 (k); 0x206D45 -0.4 (len -23, k); 0x207633 -0.4 (len -14, k) | the alignment leaves it unmatched |
| `CDirectSoundStream_Pause` | 0x28D018 | ret 8, regs -, len 81, calls 2 | loose DirectSoundUseFullHRTF … IDirectSound_DownloadEffectsImage | unmatched | 0x206B38 3.8 (k); 0x206D45 -0.4 (len -23, k); 0x207633 -0.4 (len -14, k) | the alignment leaves it unmatched |
| `CDirectSoundStream_SetFilter` | 0x28DAFC | ret 8, regs -, len 82, calls 2 | loose DirectSoundUseFullHRTF … IDirectSound_DownloadEffectsImage | unmatched | 0x206B38 3.6 (k); 0x20CA6F 0.2 (calls ±2, k); 0x206D45 -0.4 (len -24, k) | the alignment leaves it unmatched |
| `CDirectSoundStream_SetFormat` | 0x290740 | ret 8, regs -, len 82, calls 2 | loose IDirectSoundBuffer_SetVelocity … IDirectSoundBuffer_SetBufferData | unmatched | 0x206B38 3.6 (k); 0x20CA6F 0.2 (calls ±2, k); 0x206D45 -0.4 (len -24, k) | the alignment leaves it unmatched |
| `CDirectSoundStream_SetFrequency` | 0x28F791 | ret 8, regs -, len 82, calls 2 | loose DirectSoundDoWork … IDirectSoundBuffer_SetFrequency | unmatched | 0x206B38 3.6 (k); 0x20CA6F 0.2 (calls ±2, k); 0x206D45 -0.4 (len -24, k) | the alignment leaves it unmatched |
| `CDirectSoundStream_SetHeadroom` | 0x28DB4E | ret 8, regs -, len 82, calls 2 | loose DirectSoundUseFullHRTF … IDirectSound_DownloadEffectsImage | unmatched | 0x206B38 3.6 (k); 0x20CA6F 0.2 (calls ±2, k); 0x206D45 -0.4 (len -24, k) | the alignment leaves it unmatched |
| `CDirectSoundStream_SetMixBinVolumes` | 0x28DBA0 | ret 8, regs -, len 82, calls 2 | loose DirectSoundUseFullHRTF … IDirectSound_DownloadEffectsImage | unmatched | 0x206B38 3.6 (k); 0x20CA6F 0.2 (calls ±2, k); 0x206D45 -0.4 (len -24, k) | the alignment leaves it unmatched |
| `CDirectSoundStream_SetMixBins` | 0x28F835 | ret 8, regs -, len 82, calls 2 | loose DirectSoundDoWork … IDirectSoundBuffer_SetFrequency | unmatched | 0x206B38 3.6 (k); 0x20CA6F 0.2 (calls ±2, k); 0x206D45 -0.4 (len -24, k) | the alignment leaves it unmatched |
| `CDirectSoundStream_SetVolume` | 0x28DA06 | ret 8, regs -, len 82, calls 2 | loose DirectSoundUseFullHRTF … IDirectSound_DownloadEffectsImage | unmatched | 0x206B38 3.6 (k); 0x20CA6F 0.2 (calls ±2, k); 0x206D45 -0.4 (len -24, k) | the alignment leaves it unmatched |
| `DirectSoundUseLightHRTF` | 0x28BB88 | ret 0, regs -, len 31, calls 2 | loose DirectSoundUseFullHRTF … IDirectSound_DownloadEffectsImage | unmatched | 0x2069AA 3.2 (calls ±2); 0x20DFB7 2.8 (none); 0x20DF09 -0.2 (len +14, calls ±2) | the alignment leaves it unmatched |
| `DirectSoundCreateBuffer` | 0x291621 | ret 8, regs -, len 87, calls 3 | forward DirectSoundCreate … edge | 0x20B586 score 0.7, lead 0.7; fails: len +29 | 0x206B38 3.2 (none); 0x20CA6F 1.7 (calls ±3); 0x207633 1.6 (len -20) | anchored on one side only: no anchor above it in 5558 order; len +29 outside ±10 with no distinctive constant; ambiguous alignment: lead 0.72 < 2.0 |
| `DirectSoundCreateStream` | 0x291678 | ret 8, regs -, len 87, calls 3 | forward DirectSoundCreate … edge | 0x20CA6F score 1.7, lead 0.7; fails: calls ±3 | 0x206B38 3.2 (none); 0x207633 1.6 (len -20); 0x20B586 0.7 (len +29) | anchored on one side only: no anchor above it in 5558 order; calls off by 3; ambiguous alignment: lead 0.72 < 2.0 |
| `CDirectSoundStream_SetI3DL2Source` | 0x28FD40 | ret 12, regs -, len 86, calls 2 | loose DirectSoundDoWork … IDirectSoundBuffer_SetFrequency | unmatched | 0x20780D 2.8 (k); 0x20B77F 2.6 (k); 0x206C46 0.9 (len -11, k) | the alignment leaves it unmatched |
| `CDirectSoundStream_Process` | 0x28CFA1 | ret 12, regs -, len 119, calls 3 | loose DirectSoundUseFullHRTF … IDirectSound_DownloadEffectsImage | unmatched | 0x207736 2.7 (k); 0x206C46 0.9 (len -44); 0x20B77F -0.2 (len -31, k) | the alignment leaves it unmatched |
| `CDirectSoundStream_SetMaxDistance` | 0x28F9F8 | ret 12, regs -, len 90, calls 2 | loose DirectSoundDoWork … IDirectSoundBuffer_SetFrequency | unmatched | 0x20B77F 2.6 (k); 0x20780D 2.1 (k); 0x206C46 0.6 (len -15, k) | the alignment leaves it unmatched |
| `CDirectSoundStream_SetMinDistance` | 0x28FA52 | ret 12, regs -, len 90, calls 2 | loose DirectSoundDoWork … IDirectSoundBuffer_SetFrequency | unmatched | 0x20B77F 2.6 (k); 0x20780D 2.1 (k); 0x206C46 0.6 (len -15, k) | the alignment leaves it unmatched |
| `CDirectSoundStream_Release` | 0x28CE03 | ret 4, regs -, len 78, calls 2 | loose DirectSoundUseFullHRTF … IDirectSound_DownloadEffectsImage | unmatched | 0x20C9F8 2.4 (k); 0x20951B 1.6 (calls ±2, k); 0x20D0B0 -0.7 (len +16, k) | the alignment leaves it unmatched |
| `CDirectSoundStream_Discontinuity` | 0x28CEB8 | ret 4, regs -, len 77, calls 2 | loose DirectSoundUseFullHRTF … IDirectSound_DownloadEffectsImage | unmatched | 0x20C9F8 2.1 (k); 0x20951B 1.4 (calls ±2, k); 0x207A0F -0.7 (len -16, k) | the alignment leaves it unmatched |
| `IDirectSound_SetPosition` | 0x291386 | ret 20, regs -, len 53, calls 1 | loose IDirectSound_SetDistanceFactor … IDirectSound_SetI3DL2Listener | unmatched | 0x207201 2.0 (len -12); 0x20722A 2.0 (len -12); 0x209673 0.5 (len -16) | no target function in the bracket |
| `IDirectSound_SetVelocity` | 0x2913DF | ret 20, regs -, len 53, calls 1 | loose IDirectSound_SetDistanceFactor … IDirectSound_SetI3DL2Listener | unmatched | 0x207201 2.0 (len -12); 0x20722A 2.0 (len -12); 0x209673 0.5 (len -16) | no target function in the bracket |
| `CDirectSoundStream_AddRef` | 0x28CDBC | ret 4, regs -, len 71, calls 1 | loose DirectSoundUseFullHRTF … IDirectSound_DownloadEffectsImage | unmatched | 0x20B239 1.8 (k); 0x20D03C 0.5 (len -13, k); 0x20D076 0.5 (len -13, k) | the alignment leaves it unmatched |
| `CDirectSoundStream_Flush` | 0x28CF05 | ret 4, regs -, len 75, calls 2 | loose DirectSoundUseFullHRTF … IDirectSound_DownloadEffectsImage | unmatched | 0x20C9F8 1.7 (k); 0x20951B 0.9 (calls ±2, k); 0x20B239 0.0 (calls ±2, k) | the alignment leaves it unmatched |
| `IDirectSoundBuffer_PlayEx` | 0x28DFC2 | ret 16, regs -, len 36, calls 1 | tight IDirectSoundBuffer_Play … IDirectSoundBuffer_Stop | unmatched | 0x2077AB 0.8 (len +14); 0x2093FB 0.8 (len +22); 0x2088B4 0.5 (len -24) | no target function in the bracket |
| `IDirectSoundBuffer_SetRolloffCurve` | 0x28FFC1 | ret 16, regs -, len 36, calls 1 | loose IDirectSoundBuffer_SetVelocity … IDirectSoundBuffer_SetBufferData | unmatched | 0x2077AB 0.8 (len +14); 0x2093FB 0.8 (len +22); 0x2088B4 0.5 (len -24) | the alignment leaves it unmatched |
| `IDirectSoundBuffer_StopEx` | 0x28DFFE | ret 16, regs -, len 36, calls 1 | inverted IDirectSoundBuffer_Stop … IDirectSoundBuffer_GetStatus | unmatched | 0x2077AB 0.8 (len +14); 0x2093FB 0.8 (len +22); 0x2088B4 0.5 (len -24) | its nearest anchors are in opposite order in the target (a moved block); no target function in the bracket |
| `CDirectSoundStream_FlushEx` | 0x28D0C2 | ret 16, regs -, len 106, calls 3 | loose DirectSoundUseFullHRTF … IDirectSound_DownloadEffectsImage | unmatched | 0x206EDE 0.4 (len -20, k); 0x20CB2C -0.4 (calls ±3, k); 0x20B965 -1.5 (len +100, k) | the alignment leaves it unmatched |
| `CDirectSoundStream_GetInfo` | 0x28CE51 | ret 8, regs -, len 103, calls 2 | loose DirectSoundUseFullHRTF … IDirectSound_DownloadEffectsImage | unmatched | 0x206B38 0.1 (len -23, k); 0x20CA6F 0.1 (calls ±2, k); 0x20B586 -0.1 (len +13, k) | the alignment leaves it unmatched |
| `CDirectSoundStream_SetPosition` | 0x28FB02 | ret 20, regs -, len 107, calls 2 | loose DirectSoundDoWork … IDirectSoundBuffer_SetFrequency | unmatched | 0x206DFD 0.1 (len -25, k); 0x206E4F 0.1 (len -25, k); 0x206B88 -2.0 (len -39, k) | the alignment leaves it unmatched |
| `CDirectSoundStream_SetVelocity` | 0x28FB6D | ret 20, regs -, len 107, calls 2 | loose DirectSoundDoWork … IDirectSoundBuffer_SetFrequency | unmatched | 0x206DFD 0.1 (len -25, k); 0x206E4F 0.1 (len -25, k); 0x206B88 -2.0 (len -39, k) | the alignment leaves it unmatched |
| `IDirectSound_SetOrientation` | 0x29133C | ret 32, regs -, len 74, calls 1 | loose IDirectSound_SetDistanceFactor … IDirectSound_SetI3DL2Listener | unmatched | 0x206F9A -1.0 (len +92) | no target function in the bracket |
| `IDirectSound_SetEffectData` | 0x28DD9A | ret 24, regs -, len 42, calls 1 | loose IDirectSound_DownloadEffectsImage … IDirectSoundBuffer_SetVolume | unmatched | 0x20B636 -2.0 (len +149, calls ±2); 0x211269 -6.6 (regs, len +25) | the alignment leaves it unmatched |
| `XAudioDownloadEffectsImage` | 0x28E23D | ret 16, regs -, len 514, calls 27 | loose DirectSoundDoWork … IDirectSoundBuffer_SetFrequency | unmatched | 0x207C3D -20.5 (len -386, calls ±22); 0x20B86C -21.5 (len -265, calls ±20, k); 0x20D227 -24.5 (len -444, calls ±26) | the alignment leaves it unmatched |

## Held-out anchors

Each library anchor, removed from the anchor set and placed by the rest exactly as an unknown name would be. This measures the emission rule on names whose answer is known.

| Anchor | Proven | Bracket without it | Result |
| --- | --- | --- | --- |
| `D3DDevice_SetPalette` | 0x1F60F0 | inverted | not emitted, pick 0x1F60F0 (right): its nearest anchors are in opposite order in the target (a moved block); not exclusive in its bracket: 0x1F6170 also passes, scoring 7.50 against 5.85 |
| `D3DDevice_SetRenderState_FogColor` | 0x1F6790 | loose | emitted, **correct** |
| `D3DDevice_SetRenderState_CullMode` | 0x1F67E0 | tight | emitted, **correct** |
| `D3DDevice_SetRenderState_LineWidth` | 0x1F6910 | tight | emitted, **correct** |
| `D3DDevice_SetRenderState_StencilFail` | 0x1F73F0 | loose | emitted, **correct** |
| `Direct3D_CreateDevice` | 0x1F7650 | backward | not emitted, no pick: anchored on one side only: no anchor below it in 5558 order; the alignment leaves it unmatched |
| `D3D_InternalAddRef` | 0x1FC780 | loose | not emitted, pick 0x1FC780 (right): not exclusive in its bracket: 0x1FC740 also passes, scoring 6.25 against 6.25; loose bracket and not exclusive in D3D: 0x1FC740 also passes, scoring 6.25 against 6.25 |
| `D3DResource_IsBusy` | 0x1FC850 | tight | not emitted, no pick: the alignment leaves it unmatched |
| `D3D_InternalRelease` | 0x1FC910 | inverted | not emitted, pick 0x1FC910 (right): its nearest anchors are in opposite order in the target (a moved block) |
| `D3DDevice_DrawVertices` | 0x2014D0 | loose | emitted, **correct** |
| `D3DDevice_DrawIndexedVertices` | 0x201570 | forward | not emitted, pick 0x201570 (right): anchored on one side only: no anchor above it in 5558 order |
| `IDirectSound_Release` | 0x206A55 | backward | not emitted, pick 0x206A55 (right): anchored on one side only: no anchor below it in 5558 order |
| `IDirectSoundBuffer_Unlock` | 0x206A6B | tight | emitted, **correct** |
| `IDirectSoundBuffer_Release` | 0x206A70 | loose | not emitted, no pick: the alignment leaves it unmatched |
| `DirectSoundUseFullHRTF` | 0x206A86 | loose | not emitted, no pick: the alignment leaves it unmatched |
| `IDirectSound_DownloadEffectsImage` | 0x20726F | loose | not emitted, pick 0x207201 (wrong): not exclusive in its bracket: 0x20726F also passes, scoring 6.00 against 5.00; loose bracket and not exclusive in DSOUND: 0x20726F also passes, scoring 6.00 against 5.00 |
| `IDirectSound_SetDistanceFactor` | 0x2072B6 | inverted | not emitted, no pick: its nearest anchors are in opposite order in the target (a moved block); the alignment leaves it unmatched |
| `IDirectSound_CommitDeferredSettings` | 0x2072DA | loose | not emitted, no pick: the alignment leaves it unmatched |
| `IDirectSound_SetI3DL2Listener` | 0x2072F2 | loose | not emitted, pick 0x2079CB (wrong): not exclusive in its bracket: 0x2072F2 also passes, scoring 6.00 against 6.00; loose bracket and not exclusive in DSOUND: 0x207296 also passes, scoring 6.00 against 6.00 |
| `IDirectSoundBuffer_SetVolume` | 0x207312 | loose | not emitted, no pick: the alignment leaves it unmatched |
| `IDirectSoundBuffer_SetMaxDistance` | 0x20732E | inverted | not emitted, pick 0x20732E (right): its nearest anchors are in opposite order in the target (a moved block); ambiguous alignment: lead 1.50 < 2.0 |
| `IDirectSoundBuffer_SetMinDistance` | 0x207352 | tight | not emitted, pick 0x207352 (right): ambiguous alignment: lead 1.50 < 2.0 |
| `IDirectSoundBuffer_SetPosition` | 0x207376 | tight | emitted, **correct** |
| `IDirectSoundBuffer_SetVelocity` | 0x2073AB | loose | emitted, **correct** |
| `IDirectSoundBuffer_GetStatus` | 0x2073E0 | inverted | not emitted, no pick: its nearest anchors are in opposite order in the target (a moved block); the alignment leaves it unmatched |
| `IDirectSoundBuffer_GetCurrentPosition` | 0x2073FC | tight | emitted, **correct** |
| `IDirectSoundBuffer_SetCurrentPosition` | 0x20741C | tight | emitted, **correct** |
| `IDirectSoundBuffer_Lock` | 0x207438 | loose | emitted, **correct** |
| `DirectSoundDoWork` | 0x207468 | loose | not emitted, no pick: the alignment leaves it unmatched |
| `IDirectSoundBuffer_SetFrequency` | 0x207953 | inverted | not emitted, no pick: its nearest anchors are in opposite order in the target (a moved block); the alignment leaves it unmatched |
| `IDirectSoundBuffer_Play` | 0x20796F | loose | not emitted, no pick: the alignment leaves it unmatched |
| `IDirectSoundBuffer_Stop` | 0x207993 | inverted | not emitted, pick 0x207993 (right): its nearest anchors are in opposite order in the target (a moved block) |
| `IDirectSoundBuffer_SetBufferData` | 0x2079AB | inverted | not emitted, no pick: its nearest anchors are in opposite order in the target (a moved block); no target function in the bracket |
| `IDirectSound_CreateSoundBuffer` | 0x207D05 | inverted | not emitted, no pick: its nearest anchors are in opposite order in the target (a moved block); the alignment leaves it unmatched |
| `DirectSoundCreate` | 0x207E35 | forward | not emitted, no pick: anchored on one side only: no anchor above it in 5558 order; no target function in the bracket |
| `XInitDevices` | 0x24CA33 | backward | not emitted, no pick: anchored on one side only: no anchor below it in 5558 order; the alignment leaves it unmatched |
| `XInputOpen` | 0x24D22D | tight | not emitted, no pick: the alignment leaves it unmatched |
| `XInputClose` | 0x24D2A2 | loose | not emitted, pick 0x24D2A2 (right): loose bracket and not exclusive in XPP: 0x24CC0B also passes, scoring 6.00 against 6.00 |
| `XInputGetState` | 0x24D2AE | loose | emitted, **correct** |
| `XInputSetState` | 0x24D31F | forward | not emitted, pick 0x24F2DC (wrong): anchored on one side only: no anchor above it in 5558 order; ambiguous alignment: lead 1.75 < 2.0 |

## Validation against the other agents' results

The other `config/found-*.toml` files hold names other agents proved by reading the code. They are never an input to the decisions above; here they serve as a larger set of known answers.

- **Held out against the union.** Taking `config/game.toml` and those files together (101 library names, 61 of them not yet in `config/game.toml`) and removing each name in turn: 51 re-emitted at the proven address, **0 at a different address**, 50 held back.
- **Projection.** With every name those files give merged in as anchors, the alignment emits 1 further name: `D3DDevice_SetRenderStateNotInline` = 0x1F66A0.

## Names outside this tool's scope

Target names whose section is not D3D, DSOUND or XPP (game code and statically linked CRT/XAPI in `.text`, where 5558 neighbour order says nothing about this game's link order), or that have no 5558 address:

- `XLaunchNewImageA` (.text): not a library section (= 0x30603 in game.toml)
- `XMountUtilityDrive` (.text): not a library section (= 0x30E25 in game.toml)
- `XNetGetEthernetLinkStatus` (.text): not a library section
- `_CIacos` (.text): not a library section (= 0x1E05B0 in game.toml)
- `_CIasin` (.text): not a library section (= 0x1E067C in game.toml)
- `crt_acos` (.text): not a library section (= 0x1E05CD in game.toml)
- `crt_asin` (.text): not a library section (= 0x1E0699 in game.toml)
- `crt_atan` (.text): not a library section
- `crt_cos` (.text): not a library section (= 0x1E1129 in game.toml)
- `crt_log` (.text): not a library section
- `crt_log10` (.text): not a library section
- `crt_pow` (.text): not a library section (= 0x1DF6AC in game.toml)
- `crt_sin` (.text): not a library section (= 0x1E1065 in game.toml)
- `crt_sqrt` (.text): not a library section (= 0x1E11D9 in game.toml)
- `crt_tan` (.text): not a library section

## Before merging

This tool identifies addresses; it does not check that the SDK's host body for a name reads this build's data. XDK 3925 numbers its render states and texture-stage states differently from the 5849 tables the SDK runtime uses, and its CDevice layout differs (the wiring preconditions in `docs/d3d8-state-symbols.md`; `docs/d3d8-core-symbols.md` holds back `D3DDevice_SetRenderStateNotInline` for the same reason). Merged before the runtime reads those per build, the complex render-state setters (every `SetRenderState_*` but `_Simple`) make the host write 5849 indices into the 3925 state array and land inside CDevice, the two `NotInline` dispatchers route by the wrong numbering, and the device-state setters (`SetTexture`, `SetIndices`, `SetStreamSource`, `SetViewport`, `SetTransform`, `SetMaterial`) read and write the wrong CDevice fields. `SetRenderState_Simple` and the `SetTextureState_*` leaves have no such dependency.

13 of the emitted names fall in those families, so `config/found-align.toml` declares `staged = true`: `tools/merge_symbols.py` skips it unless it is named explicitly. The names: `D3DDevice_SetIndices`, `D3DDevice_SetRenderStateNotInline`, `D3DDevice_SetRenderState_EdgeAntiAlias`, `D3DDevice_SetRenderState_ShadowFunc`, `D3DDevice_SetRenderState_FrontFace`, `D3DDevice_SetRenderState_NormalizeNormals`, `D3DDevice_SetRenderState_TextureFactor`, `D3DDevice_SetRenderState_Dxt1NoiseEnable`, `D3DDevice_SetRenderState_LogicOp`, `D3DDevice_SetRenderState_VertexBlend`, `D3DDevice_SetRenderState_ZEnable`, `D3DDevice_SetRenderState_StencilEnable`, `D3DDevice_SetStreamSource`.

## Assessment

Of the 185 library-section target names, 40 were already named in `config/game.toml` when this ran, leaving 145. The alignment proves 20 of those (14%), 1 of which no other agent's file names.

Precision, measured rather than assumed: held out one at a time, the 40 library anchors were re-emitted at a wrong address 0 times (correctly 12 times, the rest held back), and the independent manual results in other agents' files agree with 19 of the 19 emitted names they also give.

Of the 125 names left after this run, 16 have a pick that passes every invariant and is held back only by position or ambiguity (*Strongest leads*), 93 have no pick in their bracket, and the rest have a pick that fails at least one invariant. The automated alignment therefore closes the part of the gap that sits in agreeing brackets with a distinguishing shape; the rest needs a human to read the code, starting from the leads.

Measured against what the other agents found by hand: of this run's 20 names, 1 is in none of their files (`D3DDevice_SetRenderStateNotInline`); with their files merged in as anchors, the alignment emits 1 name beyond them (`D3DDevice_SetRenderStateNotInline`). Held out one at a time, it independently re-derives 51 of the 101 library names known so far. So with the anchors available now, the alignment adds little beyond the manual work; its value is confirmation by an independent method and the ranked leads above.

Why the yield is bounded, from the rejection table:

- **Block moves.** Link order holds inside an object, not across a section, so a name is only placed between anchors that agree with each other. Names whose nearest anchors disagree, or that have an anchor on one side only, cannot be emitted however good their candidate looks.
- **Identical-looking runs.** Runs of setters with the same shape (same ret, length, call count and no distinguishing constant) are told apart only by order; the tool refuses to emit those, by design.
- **Absent or rewritten functions.** A title links only what it calls, and 3925 lacks APIs 5558 has. A name the alignment leaves unmatched has nothing in its bracket with the same ret that scores positively: either it is not in this image (a correct zero), or it moved out of its bracket or was rewritten past what these invariants recognise. The *Others* column is where to look for the latter.
- **Length drift.** 3925 is two years older than 5558; functions rewritten in between fail the length band, and without a distinctive hardware constant they stay unproven.

Each run uses whatever `config/game.toml` holds. Every name merged into it becomes an anchor that narrows the brackets around its neighbours, so rerun the tool after each merge; the held-out-anchor table above is the running check that the rule stays safe as the anchor set grows.
