# D3D8 state setters and texture-stage states in Fuzion Frenzy (XDK 3925)

Image: `out/ff.exe`, section `D3D` 0x1F5340-0x206970. Reference: `out/sims5558.exe` (XDK 5558, names applied).
Every address below was read in IDA from these two images. "5558 twin" is the same-named function in the reference.

**Result: 41 of the 52 adopted names are proven** and listed in `config/found-d3d8-state.toml`. The other 11 are not
in this image, or exist only as an older API with a different value domain. See "Not proven".

Two findings change how the SDK has to treat this build. Read them before merging:

- **The render-state numbering differs from 5849.** The deferred block starts at 82, the complex block at 116, and there
  are 144 states. The complex block has no MultiSampleRenderTargetMode and no SampleAlpha. See
  [Render-state indices for XDK 3925](#render-state-indices-for-xdk-3925).
- **Deferred texture-stage states 0..21 are rotated against 5849.** In 3925, COLOROP = 0 and ADDRESSU = 10. Types 22..30
  have the same numbers in both builds. See [Texture-stage-state indices for XDK 3925](#texture-stage-state-indices-for-xdk-3925).

## Adopted name set

These are all `D3D` rows of `docs/symbol-targets.json` whose names start with one of the prefixes in the brief. There
are 52 of them. The JSON has no rows for `D3DDevice_GetTransform`, `D3DDevice_SetPalette`, `D3DDevice_GetViewport` or
`D3DDevice_SetVertexData`. `D3DDevice_SetTextureState_*` matches the `D3DDevice_SetTexture` prefix, so those names are
included.

| Name | Result |
|---|---|
| D3DDevice_LightEnable | 0x1F5EB0 |
| D3DDevice_SetFlickerFilter | not linked |
| D3DDevice_SetGammaRamp | not linked |
| D3DDevice_SetIndices | 0x1F6170 |
| D3DDevice_SetLight | 0x1F5CD0 |
| D3DDevice_SetMaterial | 0x1F54A0 |
| D3DDevice_SetRenderState_BackFillMode | 0x1F6B00 |
| D3DDevice_SetRenderState_CullMode | 0x1F67E0 (anchor, re-proven) |
| D3DDevice_SetRenderState_DoNotCullUncompressed | 0x1F7590 |
| D3DDevice_SetRenderState_Dxt1NoiseEnable | 0x1F6970 |
| D3DDevice_SetRenderState_EdgeAntiAlias | 0x1F6730 |
| D3DDevice_SetRenderState_FillMode | 0x1F6AC0 |
| D3DDevice_SetRenderState_FogColor | 0x1F6790 (anchor, re-proven) |
| D3DDevice_SetRenderState_FrontFace | 0x1F6850 |
| D3DDevice_SetRenderState_LineWidth | 0x1F6910 (anchor, re-proven) |
| D3DDevice_SetRenderState_LogicOp | 0x1F6A70 |
| D3DDevice_SetRenderState_MultiSampleAntiAlias | 0x1F75B0 |
| D3DDevice_SetRenderState_MultiSampleMask | 0x1F7600 |
| D3DDevice_SetRenderState_MultiSampleMode | not emitted (3925 has MultiSampleType at 0x1F65F0) |
| D3DDevice_SetRenderState_MultiSampleRenderTargetMode | absent from XDK 3925 |
| D3DDevice_SetRenderState_NormalizeNormals | 0x1F6890 |
| D3DDevice_SetRenderState_OcclusionCullEnable | 0x1F7490 |
| D3DDevice_SetRenderState_PSTextureModes | 0x1F6620 |
| D3DDevice_SetRenderState_RopZCmpAlwaysRead | 0x1F7550 |
| D3DDevice_SetRenderState_RopZRead | 0x1F7570 |
| D3DDevice_SetRenderState_SampleAlpha | absent from XDK 3925 |
| D3DDevice_SetRenderState_ShadowFunc | 0x1F6760 |
| D3DDevice_SetRenderState_Simple | 0x1F6650 |
| D3DDevice_SetRenderState_StencilCullEnable | 0x1F74F0 |
| D3DDevice_SetRenderState_StencilEnable | 0x1F7370 |
| D3DDevice_SetRenderState_StencilFail | 0x1F73F0 (anchor, re-proven) |
| D3DDevice_SetRenderState_TextureFactor | 0x1F68C0 |
| D3DDevice_SetRenderState_TwoSidedLighting | 0x1F6B50 |
| D3DDevice_SetRenderState_VertexBlend | 0x1F6BB0 |
| D3DDevice_SetRenderState_YuvEnable | 0x1F7460 |
| D3DDevice_SetRenderState_ZEnable | 0x1F7300 |
| D3DDevice_SetScissors | 0x1F6300 |
| D3DDevice_SetScreenSpaceOffset | not linked |
| D3DDevice_SetSoftDisplayFilter | not linked |
| D3DDevice_SetStreamSource | 0x1F8000 |
| D3DDevice_SetTexture | 0x1F5F90 |
| D3DDevice_SetTextureStageStateNotInline | 0x1F6DB0 |
| D3DDevice_SetTextureState_BorderColor | 0x1F6D30 |
| D3DDevice_SetTextureState_BumpEnv | 0x1F6CD0 |
| D3DDevice_SetTextureState_ColorKeyColor | 0x1F6D70 |
| D3DDevice_SetTextureState_TexCoordIndex | 0x1F6BF0 |
| D3DDevice_SetTransform | 0x1F5360 |
| D3DDevice_SetVertexShaderConstant1 | not present (3925 has only the stdcall SetVertexShaderConstant) |
| D3DDevice_SetVertexShaderConstant1Fast | not present |
| D3DDevice_SetVertexShaderConstant4 | not present |
| D3DDevice_SetVertexShaderConstantNotInlineFast | not present |
| D3DDevice_SetViewport | 0x1F5B70 |

## Facts every proof below relies on

- **Device pointer and static device.** `D3D_pDevice` = 0x206720 points at the static `D3D_Device` = 0x203B90:
  `Direct3D_CreateDevice` 0x1F7690 executes `mov dword_206720, offset dword_203B90`. In `CDevice`, +0 is the push
  cursor, +4 the push threshold, +8 the **dirty flags**, and +0xC the device flags.
- **Push reservation.** 3925 makes it a call. Each setter executes `push esi` (pDevice) and `call 0x1FB3B0`.
  0x1FB3B0 executes `mov ecx,[esp+4]; mov eax,[ecx]; cmp eax,[ecx+4]; jb; call 0x1FB110` (MakeSpace), then `retn 4`.
  5558 inlines the same sequence as `mov eax,[esi]; cmp eax,[esi+4]; jb; call D3DDevice_MakeSpace`. That accounts for
  most per-function length differences: a 3925 setter that pushes is usually exactly 6 bytes shorter than its twin
  (for example EdgeAntiAlias 44/50, FogColor 71/77, CullMode 98/104 and ColorKeyColor 51/57).
- **Method tokens** are `(dwords << 18) | method` in both builds. Every NV2A token quoted below is byte-identical to the
  one in the 5558 twin.
- **Arrays.** `D3D_DeferredTextureState` [4][32] is at 0x203750. `D3D_RenderState` [144] is at 0x203950 and ends
  exactly at `D3D_Device` 0x203B90. Each complex setter stores its argument into its own slot,
  `D3D_RenderState + 4*index`. That slot is quoted in each section below.
- **Library order.** 3925 links the same objects in the same relative order as 5558. The render-state functions run
  from 0x1F65F0 to 0x1F7649, and their order matches 5558's 0x2498B0-0x24B33B exactly, apart from the three states 3925
  lacks.

## Render-state indices for XDK 3925

### How each index was established

1. **Bounds.** `D3DDevice_SetRenderStateNotInline` is at 0x1F66A0 (`retn 8`; D3DCoreFns emits it). Its body:
   - 0x1F66A5 `cmp esi, 52h`. Simple states, 0..81, go to `Simple(dword_253520[esi*4], value)` and
     `mov dword_203950[esi*4], edi`.
   - 0x1F66C9 `cmp esi, 74h`. Deferred states, 82..115, do `or dword_203B98, dword_2024E8[esi*4]` and
     `mov dword_203950[esi*4], ecx`.
   - Everything else goes to 0x1F66EF `call dword ptr [esi*4+2023F0h]`. These are the complex states, 116 upward.

   The 5558 twin at 0x249910 has the same three branches with bounds `5Ch`/`88h`, D3D__RenderState at 0x2646B0, and
   the table at 0x259BB0.
2. **Complex block: a table of per-state handler functions.** 0x2023F0 + 116*4 = 0x2025C0 holds 28 function
   pointers, for states 116..143. Slot 144 (0x202630) already holds data (0x40), the first deferred dirty flag.
   **The table order is the index order.** The four anchors occupy slots 118 (FogColor 0x1F6790), 125 (StencilFail
   0x1F73F0), 127 (CullMode 0x1F67E0) and 136 (LineWidth 0x1F6910). Each handler also stores its argument into
   `0x203950 + 4*slot`, for example FogColor `mov dword_203B28, ecx` and LineWidth `mov dword_203B70, ebx`.
   5558's table at 0x259DD0 holds the named 5558 functions in the same order, with two extra entries. The full
   side-by-side listing is in the table below.
3. **Gaps placed by the handlers' own NV2A tokens.** Between CullMode (127) and LineWidth (136) 3925 has 8 handlers
   where 5558 has 9. The tokens identify each one: 128 `400A60h`, 129 the ZBias shape, 130 `417BCh`/`817BCh`,
   131 `80320h`, 132 and 133 `41D7Ch`, 134 the RT re-apply, and 135 `41E6Ch`. The missing 5558 entry is therefore
   MultiSampleRenderTargetMode. After LineWidth, slot 137 is Dxt1NoiseEnable (`40110h`/`40100h` with the RT-format
   test), so SampleAlpha is missing.
4. **D3D_InitDefaultState 0x1FB580** (D3DCoreFns' name) confirms the bounds and two slots:
   - It loops `edi` over states 57..143 (`mov edi, 39h`, `cmp esi, 15Ch`), skipping only `cmp esi, 134h`, which is
     state 57+77 = 134.
   - 5558 loops 57..165 (`cmp esi, 0A6h`), skipping 154 (`9Ah`, MultiSampleMode) and 126/127 (SwapFilter,
     PresentationInterval).
   - It then sets ZEnable from the auto-depth surface. 3925 does this at 0x1FB5DE with `push 7Bh` (123); 5558 does it
     at 0x24F9A5 with `push 8Fh` (143).
5. **Deferred block.** The bounds give exactly 34 slots, 82..115, and 5849's FOGENABLE..PATCHSEGMENTS is 34 states
   (92..125). Two independent sources pin the order, with 3925 index = 5849 index - 10:
   - **Dirty-flag tables.** 3925 `dword_2024E8[state]` and 5558 `dword_2E1DB0[state]` have the same groups, in the
     same sizes and order: fog ×6 (0x40 / 0x2000), wrap ×4 (0x10 / 0xF), Lighting (unique 0x22 / 0x1200), SpecularEnable
     (unique 0x60 / 0x3000), light ×12 (0x20 / 0x1000), point ×2 (0x1 / 0x100), PointSpriteEnable (unique 0x9 / 0x900),
     point ×5 (0x1 / 0x100), and patch ×2 (0 / 0). 5558's 126..135 have no 3925 slot.
   - **Reader functions.** Each lazy-state reader reads the same set in both builds, 10 lower in 3925:
     - Fog, 0x1F8E20 ↔ 0x2541D0: reads 82..87 and 93, against 92..97 and 103. Tokens `802A0h`/`4029Ch`/`0C09C0h`.
     - Constants, 0x1F82C0 ↔ 0x24C300: reads only 83, against 93. Identical tokens `41EA4h`/`200B80h`/`41E9Ch`.
     - Wrap, 0x1F8C20 ↔ 0x253FE0: 88 against 98.
     - Lighting, 0x1F9790 ↔ 0x254B60: 92/93/94 against 102/103/104. The second Lighting reader, 0x1F9AB0 ↔ 0x254EB0,
       reads 92 against 102.
     - Material sources, 0x1F8540 ↔ 0x253850: 95..103 against 105..113.
     - Ambient, 0x1F94B0 ↔ 0x254860: 104/105 against 114/115. Tokens `0C0A10h` and `0C17A0h`.
     - Point, 0x1F8730 ↔ 0x253AF0: 106..113 against 116..123. The second PointSpriteEnable reader, 0x1F8A20 ↔ 0x258F80,
       reads 108 against 118.

     Patch (114/115 against 124/125) has no reader in either build. Those two indices come from position and from
     their zero dirty flags, not from a reader.

### Name → index table (state.h names)

The address of a state is `D3D_RenderState` 0x203950 + 4 × the 3925 index. All handler addresses are in `ff.exe`.

| state.h name | 5849 | 3925 | Address | Evidence |
|---|---|---|---|---|
| (kRsDeferredFirst) | 92 | **82** | 0x203A98 | `cmp esi, 52h` at 0x1F66A5 |
| FogEnable | 92 | 82 | 0x203A98 | fog reader 0x1F8E20; dirty 0x40 |
| FogTableMode | 93 | 83 | 0x203A9C | fog reader; 0x1F82C0 ↔ 0x24C300 read only this slot |
| FogStart | 94 | 84 | 0x203AA0 | fog reader; dirty 0x40 |
| FogEnd | 95 | 85 | 0x203AA4 | fog reader; dirty 0x40 |
| FogDensity | 96 | 86 | 0x203AA8 | fog reader; dirty 0x40 |
| RangeFogEnable | 97 | 87 | 0x203AAC | fog reader; dirty 0x40 |
| Wrap0 | 98 | 88 | 0x203AB0 | wrap reader 0x1F8C20; dirty 0x10 |
| Wrap1 | 99 | 89 | 0x203AB4 | dirty 0x10 (wrap group) |
| Wrap2 | 100 | 90 | 0x203AB8 | dirty 0x10 |
| Wrap3 | 101 | 91 | 0x203ABC | dirty 0x10 |
| Lighting | 102 | 92 | 0x203AC0 | 0x1F9790 and 0x1F9AB0; unique dirty 0x22 |
| SpecularEnable | 103 | 93 | 0x203AC4 | fog and light readers; unique dirty 0x60 |
| LocalViewer | 104 | 94 | 0x203AC8 | light reader 0x1F9790 |
| ColorVertex | 105 | 95 | 0x203ACC | material-source reader 0x1F8540 |
| BackSpecularMaterialSource | 106 | 96 | 0x203AD0 | 0x1F8540 |
| BackDiffuseMaterialSource | 107 | 97 | 0x203AD4 | 0x1F8540 |
| BackAmbientMaterialSource | 108 | 98 | 0x203AD8 | 0x1F8540 |
| BackEmissiveMaterialSource | 109 | 99 | 0x203ADC | 0x1F8540 |
| SpecularMaterialSource | 110 | 100 | 0x203AE0 | 0x1F8540 |
| DiffuseMaterialSource | 111 | 101 | 0x203AE4 | 0x1F8540 |
| AmbientMaterialSource | 112 | 102 | 0x203AE8 | 0x1F8540 |
| EmissiveMaterialSource | 113 | 103 | 0x203AEC | 0x1F8540 |
| BackAmbient | 114 | 104 | 0x203AF0 | 0x1F94B0, back scene ambient `0C17A0h` |
| Ambient | 115 | 105 | 0x203AF4 | 0x1F94B0, scene ambient `0C0A10h` |
| PointSize | 116 | 106 | 0x203AF8 | point reader 0x1F8730 (`4043Ch`) |
| PointSizeMin | 117 | 107 | 0x203AFC | 0x1F8730 |
| PointSpriteEnable | 118 | 108 | 0x203B00 | 0x1F8730 and 0x1F8A20; unique dirty 0x9 |
| PointScaleEnable | 119 | 109 | 0x203B04 | 0x1F8730 |
| PointScaleA | 120 | 110 | 0x203B08 | 0x1F8730 |
| PointScaleB | 121 | 111 | 0x203B0C | 0x1F8730 |
| PointScaleC | 122 | 112 | 0x203B10 | 0x1F8730 |
| PointSizeMax | 123 | 113 | 0x203B14 | 0x1F8730 |
| PatchEdgeStyle | 124 | 114 | 0x203B18 | position; dirty 0 in both builds |
| PatchSegments | 125 | 115 | 0x203B1C | position; dirty 0 in both builds |
| SwapFilter | 126 | **absent** | — | the deferred block ends at 115 |
| PresentationInterval | 127 | **absent** | — | the deferred block ends at 115 |
| (deferred unused 8..1) | 128-135 | **absent** | — | — |
| (kRsComplexFirst) | 136 | **116** | 0x203B20 | `cmp esi, 74h` at 0x1F66C9; table 0x2025C0 |
| PsTextureModes | 136 | 116 | 0x203B20 | table → 0x1F6620 |
| VertexBlend | 137 | 117 | 0x203B24 | table → 0x1F6BB0 |
| FogColor | 138 | 118 | 0x203B28 | table → 0x1F6790 (anchor) |
| FillMode | 139 | 119 | 0x203B2C | table → 0x1F6AC0 |
| BackFillMode | 140 | 120 | 0x203B30 | table → 0x1F6B00 |
| TwoSidedLighting | 141 | 121 | 0x203B34 | table → 0x1F6B50 |
| NormalizeNormals | 142 | 122 | 0x203B38 | table → 0x1F6890 |
| ZEnable | 143 | 123 | 0x203B3C | table → 0x1F7300; InitDefaultState `push 7Bh` |
| StencilEnable | 144 | 124 | 0x203B40 | table → 0x1F7370 |
| StencilFail | 145 | 125 | 0x203B44 | table → 0x1F73F0 (anchor) |
| FrontFace | 146 | 126 | 0x203B48 | table → 0x1F6850 |
| CullMode | 147 | 127 | 0x203B4C | table → 0x1F67E0 (anchor) |
| TextureFactor | 148 | 128 | 0x203B50 | table → 0x1F68C0 |
| ZBias | 149 | 129 | 0x203B54 | table → 0x1F69F0 (sets states 77..81) |
| LogicOp | 150 | 130 | 0x203B58 | table → 0x1F6A70 |
| EdgeAntiAlias | 151 | 131 | 0x203B5C | table → 0x1F6730 |
| MultiSampleAntiAlias | 152 | 132 | 0x203B60 | table → 0x1F75B0 |
| MultiSampleMask | 153 | 133 | 0x203B64 | table → 0x1F7600 |
| MultiSampleMode | 154 | 134 (as **MULTISAMPLETYPE**) | 0x203B68 | table → 0x1F65F0; same slot, different value domain (below) |
| MultiSampleRenderTargetMode | 155 | **absent** | — | no handler between 134 and ShadowFunc |
| ShadowFunc | 156 | 135 | 0x203B6C | table → 0x1F6760 |
| LineWidth | 157 | 136 | 0x203B70 | table → 0x1F6910 (anchor) |
| SampleAlpha | 158 | **absent** | — | LineWidth is followed directly by Dxt1NoiseEnable |
| Dxt1NoiseEnable | 159 | 137 | 0x203B74 | table → 0x1F6970 |
| YuvEnable | 160 | 138 | 0x203B78 | table → 0x1F7460 |
| OcclusionCullEnable | 161 | 139 | 0x203B7C | table → 0x1F7490 |
| StencilCullEnable | 162 | 140 | 0x203B80 | table → 0x1F74F0 |
| RopZCmpAlwaysRead | 163 | 141 | 0x203B84 | table → 0x1F7550 |
| RopZRead | 164 | 142 | 0x203B88 | table → 0x1F7570 |
| DoNotCullUncompressed | 165 | 143 | 0x203B8C | table → 0x1F7590 |
| (kRsMax) | 166 | **144** | 0x203B90 = D3D_Device | 28-entry table; InitDefaultState loops to `cmp esi, 15Ch` |

The offset between the builds is -10 for the deferred states and -20 for the complex block up to slot 134. It becomes
-21 from ShadowFunc to LineWidth, and -22 from Dxt1NoiseEnable onward. No single subtraction works across the whole
range.

**Slot 134** holds D3DRS_MULTISAMPLETYPE in this build. Its handler 0x1F65F0 executes `mov dword_203B68, ecx`, then
`mov edx,[eax+410h]; mov eax,[eax+40Ch]; push edx; push eax; call 0x1F5640`, which is SetRenderTarget(current RT,
current Z), and returns with `retn 4`. Its readers decode a D3DMULTISAMPLE_TYPE bit-field: 0x1F70E0 does
`test ah, 30h`, `shr ecx, 4; and ecx, 0Fh` and `test ah, 10h`, and 0x1F6EFC does `test esi, 1000h`. That is not the
5849 D3DMULTISAMPLEMODE value.

### Simple block and `D3D_SimpleRenderStateEncode`

- `D3D_SimpleRenderStateEncode` = **0x253520**, read by SetRenderStateNotInline 0x1F66AA
  `mov ecx, dword_253520[esi*4]`.
- It has 82 entries (0..81) followed by 0. Entries 0..81 are identical to 5558's table at 0x2E1FE0, checked one by
  one. For example [57] = 0x40354 (ZFUNC), [64] = 0x4035C (ZWRITEENABLE), [70] = 0x40364 (STENCILFUNC) and
  [81] = 0x40338 (SOLIDOFFSETENABLE).
- 5849's [82] DepthClipControl (0x41D78), [83] StippleEnable (0x4147C) and [84..91] unused do not exist in 3925.
- So the simple block is still indexed 0..DeferredFirst-1, with DeferredFirst = 82. Each entry is `(1<<18) | method`
  with method < 0x2000, so `R32(kSimpleEncode + rs*4) & 0x1FFC` is still the right decode.
- The game's own inlined SetRenderState uses the same numbering. For example 0x39FDE executes `mov ecx, 4035Ch` and
  `call 0x1F6650`, and 0x39FF2 then executes `mov dword_203A50, 0`, which is 0x203950 + 64*4 (ZWRITEENABLE).

## Texture-stage-state indices for XDK 3925

`D3DDevice_SetTextureStageStateNotInline` (0x1F6DB0) handles the types in four ranges:

| 3925 types | What it does |
|---|---|
| 0..21 (`16h`) | stores to the deferred array |
| 22..27 | goes to BumpEnv |
| 28 | goes to TexCoordIndex |
| 29 | goes to BorderColor |
| 30 | goes to ColorKeyColor |

These bounds are the same as in 5849. **The order inside 0..21 is not the same.** 3925 index = (5849 index + 10) mod 22:

| state.h | 5849 | 3925 | | state.h | 5849 | 3925 |
|---|---|---|---|---|---|---|
| AddressU | 0 | 10 | | ColorOp | 12 | 0 |
| AddressV | 1 | 11 | | ColorArg0 | 13 | 1 |
| AddressW | 2 | 12 | | ColorArg1 | 14 | 2 |
| MagFilter | 3 | 13 | | ColorArg2 | 15 | 3 |
| MinFilter | 4 | 14 | | AlphaOp | 16 | 4 |
| MipFilter | 5 | 15 | | AlphaArg0 | 17 | 5 |
| MipLodBias | 6 | 16 | | AlphaArg1 | 18 | 6 |
| MaxMipLevel | 7 | 17 | | AlphaArg2 | 19 | 7 |
| MaxAnisotropy | 8 | 18 | | ResultArg | 20 | 8 |
| ColorKeyOp | 9 | 19 | | TextureTransformFlags | 21 | 9 |
| ColorSign | 10 | 20 | | BumpEnvMat00..LOffset | 22..27 | 22..27 |
| AlphaKill | 11 | 21 | | TexCoordIndex / BorderColor / ColorKeyColor | 28 / 29 / 30 | 28 / 29 / 30 |

Evidence:

1. D3D_InitDefaultState ends with `TSS(0, 0, 4)` and `TSS(0, 4, 2)` at 0x1FB616-0x1FB627. 5558 ends with
   `TSS(0, 0Ch, 4)` and `TSS(0, 10h, 2)` at 0x24F9DF-0x24F9F0, which are COLOROP = MODULATE and ALPHAOP = SELECTARG1.
2. The 32-byte default tables are 3925 `byte_202AB8` = 1 1 2 1 1 1 2 1 1 0 1 1 1 1 1 0 0 0 1 0 0 0 and 5558
   `byte_2632A4` = 1 1 1 1 1 0 0 0 1 0 0 0 1 1 2 1 1 1 2 1 1 0. Entries 22..31 are 0 in both.
3. The per-type dirty tables are 3925 `dword_2036B0` = 0x418, 0x8 ×8, 0x4, 0x10 ×12 and 5558 `dword_2E1EC8` =
   0xF ×12, 0x800 ×9, 0x400. A cyclic shift of 12 is the only one that aligns both the default and the dirty-class
   sequences; this was checked over all 22 shifts.
4. The fixed-function combiner compiler at 0x1FEAD0-0x1FF9D0 reads only types 0..8 of `D3D_DeferredTextureState`. In
   3925 those are the op, arg and result states.

[INFERENCE] Inside runs whose defaults and dirty classes are equal (10..14, 15..17, 19..21), the order rests on the
rotation being a plain rotation. Nothing contradicts it, but no reader isolates each slot.

## Wiring preconditions

Every emitted name has the ABI of the SDK's HLE declaration: the same calling convention and the same stack-argument
count as its `retn N`. The listed host bodies also read build-specific data, which must be configured for 3925 before
the names are merged:

| Host bodies | They need |
|---|---|
| the 26 complex `SetRenderState_*` (`SetComplex` writes `kRenderState + rs*4`) | the 3925 index table above. With `D3D_RenderState` = 0x203950 and 5849 indices, every complex write lands at 0x203B90 or above, inside `D3D_Device`; for example CullMode (147) writes 0x203B9C, which is `CDevice_Flags`. The four anchors already in `config/game.toml` would do the same once `D3D_RenderState` is set; it is 0 there today. |
| `SetTextureStageStateNotInline`, plus `SyncDeferred`'s texture-stage refresh | the 3925 texture-stage rotation |
| `SetRenderState_Simple`, `SetTextureState_*`, `SetTransform` | no numbering change: method tokens, texture-stage types 22..30 and transform types 0..9 are the same as in 5849. SetTransform still needs `CDevice_Transforms`, below. |
| `SetTexture`, `SetIndices`, `SetStreamSource`, `SetViewport`, `SetTransform`, `SetMaterial` | the 3925 data symbols and CDevice offsets in the last two sections |

## Evidence per emitted name

The complex render states are all stdcall with one argument and end in `retn 4`, except FrontFace (below). "Slot" is
the entry in the complex handler table at 0x2025C0, and equals the `D3D_RenderState` index.

### D3DDevice_SetRenderState_PSTextureModes = 0x1F6620

- **Slot:** 116, the first complex state.
- **Body:** `mov [eax+420h], ecx`, then `or edx, 400h` into `[eax+8]`, then `mov dword_203B20, ecx` (RS[116]),
  `retn 4`. It pushes nothing.
- **5558 twin** 0x2498B0: `mov [ecx+790h], eax`, `or dword_2644A8, 4000h`, `mov dword_2648D0, eax` (RS[136]),
  `retn 4`.
- **Dirty bit:** 0x400 is the bit SetTexture sets on a format change (0x1F60C6), matching 5558's 0x4000 in both
  functions.
- **Neighbours:** it immediately precedes Simple in both builds.
- **Length:** 36 bytes against 34.
- **Confidence:** high.

### D3DDevice_SetRenderState_Simple = 0x1F6650

- **Convention:** fastcall. ecx is the method token, edx the value, and it ends in a plain `retn`. That matches 5849
  `ret 0`, regs [ecx, edx].
- **Body:**
  ```
  mov eax, dword_203B90        ; D3D_Device.Push
  add eax, 8
  cmp eax, dword_203B94        ; D3D_Device.Threshold
  jnb grow
  mov dword_203B90, eax
  mov [eax-8], ecx
  mov [eax-4], edx
  retn
  grow:
  push edx
  push ecx
  mov ecx, dword_206720
  call 0x1FB110                ; MakeSpace
  pop ecx
  pop edx
  jmp 0x1F6650
  ```
- **5558 twin** 0x2498E0 is the same code without the `mov ecx`. That is the length difference, 45 bytes against 39.
- **Uniqueness:** this is the only function in the image that reads or writes D3D_Device+0 by absolute address. The
  xrefs to 0x203B90 as a memory operand are 0x1F6650 and 0x1F6660.
- **Callers:** SetRenderStateNotInline (0x1F66B8) calls it with `ecx = D3D_SimpleRenderStateEncode[state]`. It has 84
  call sites in total, mostly the game's inlined SetRenderState; see the example in the simple-block section.
- **Confidence:** high.

### D3DDevice_SetRenderState_EdgeAntiAlias = 0x1F6730

- **Slot:** 131.
- **Body:** token `80320h` (line smooth and polygon smooth), then the argument twice (`mov [eax+4], ecx` and
  `mov [eax+8], ecx`), then `mov dword_203B5C, ecx` (RS[131]), `retn 4`.
- **5558 twin** 0x249E20: the same token `80320h`, the same two stores, RS[151], `retn 4`.
- **Length:** 44 bytes against 50.
- **Confidence:** high.

### D3DDevice_SetRenderState_ShadowFunc = 0x1F6760

- **Slot:** 135.
- **Body:** token `41E6Ch` with `lea edx, [ecx-200h]` (D3DCMP to 0..7), then `mov dword_203B6C, ecx`, `retn 4`.
- **5558 twin** 0x249E60: the same token `41E6Ch`, the same `lea edx, [ecx-200h]`, RS[156]. The JSON k list for
  5849 is [0x41E6C, 0xFFFFFE00].
- **Length:** 47 bytes against 53.
- **Confidence:** high.

### D3DDevice_SetRenderState_FogColor = 0x1F6790 (anchor)

- **Slot:** 118.
- **Body:** token `402A8h` with the R/B swap (`and edi, 0FFh; shl edi, 10h` and `and edi, 0FF00FF00h`), then
  `mov dword_203B28, ecx` (RS[118]), `retn 4`.
- **5558 twin** 0x249EA0: identical, RS[138].
- **Length:** 71 bytes against 77.
- **Confidence:** high.

### D3DDevice_SetRenderState_CullMode = 0x1F67E0 (anchor)

- **Slot:** 127.
- **Body:** token `40308h`. A zero argument pushes 0. A non-zero argument pushes 1, then `4039Ch` with
  `404h + (cull != dword_203B48)`, which compares against the FrontFace slot (`setnz dl; add edx, 404h`). It then
  executes `mov dword_203B4C, ecx` (RS[127]); both paths end in `retn 4`.
- **5558 twin** 0x249EF0: identical, comparing against 0x2648F8 and storing RS[147].
- **Length:** 98 bytes against 104.
- **Confidence:** high.

### D3DDevice_SetRenderState_FrontFace = 0x1F6850

- **Slot:** 126.
- **Body:** token `403A0h` with the value, then `mov dword_203B48, ecx` (RS[126]). It then loads
  `mov eax, dword_203B4C` (CullMode), stores it over its own argument with `mov [esp+arg_0], eax`, and leaves through
  `jmp D3DDevice_SetRenderState_CullMode`. Since CullMode ends in `retn 4`, the stack is balanced.
- **No `retn`:** this tail call is why the JSON lists `ret` = none for 5849.
- **5558 twin** 0x249F60: the same `403A0h` and the same tail `jmp D3DDevice_SetRenderState_CullMode`.
- **Length:** 52 bytes against 58.
- **Confidence:** high.

### D3DDevice_SetRenderState_NormalizeNormals = 0x1F6890

- **Slot:** 122.
- **Body:** token `403A4h` with the value, `or dword ptr [esi+8], 2`, then `mov dword_203B38, ecx`, `retn 4`.
- **5558 twin** 0x249FA0: `403A4h`, `or dword_2644A8, 200h`, RS[142]. The JSON k list is [0x200, 0x403A4].
- **Length:** 45 bytes against 57.
- **Confidence:** high.

### D3DDevice_SetRenderState_TextureFactor = 0x1F68C0

- **Slot:** 128.
- **Body:** it tests `[esi+414h]`, the pixel shader. If one is set it only stores. Otherwise it pushes token
  `400A60h` followed by 16 copies of the value (`mov ecx, 10h; rep stosd`). Both paths store RS[128] with
  `mov dword_203B50, eax` and end in `retn 4`.
- **5558 twin** 0x249FE0: tests `[esi+784h]`, then `400A60h` and a 16-iteration loop, RS[148]. The JSON k list is
  [0x784, 0x400A60].
- **Length:** 76 bytes against 81.
- **Confidence:** high.

### D3DDevice_SetRenderState_LineWidth = 0x1F6910 (anchor)

- **Slot:** 136.
- **Body:** `fmul [esi+508h]` and `fmul flt_25552C`, then float to int via 0x1FC980, then `cmp edi, 1FFh` clamping
  to 0x1FF. It pushes token `40380h` with the result, stores `mov dword_203B70, ebx`, and returns with `retn 4`.
- **5558 twin** 0x24A040: identical, using `[esi+964h]`, RS[157].
- **Length:** 95 bytes against 101.
- **Confidence:** high.

### D3DDevice_SetRenderState_Dxt1NoiseEnable = 0x1F6970

- **Slot:** 137.
- **Format test:** `mov eax, [esi+40Ch]; movzx ecx, byte ptr [eax+0Dh]`, then the table byte
  `and dl, 3Ch; cmp dl, 20h`. This is the render target's format.
- **Body:** it toggles bit 0 of `[esi+0Ch]`, then pushes `40110h` 0, `41D8Ch` v, and `40100h` `31Ch`. It stores
  `mov dword_203B74, ebx` and returns with `retn 4`.
- **5558 twin** 0x24A0B0 has the same format test on `[edi+1A04h]` (the render target), toggles bit 0 of `[edi+8]`,
  and pushes `40110h` and `40100h`. RS[159]. The JSON k list is [0x1A04, 0x40100, 0x40110].
- **Length:** 119 bytes against 119.
- **Confidence:** high.

### D3DDevice_SetRenderState_LogicOp = 0x1F6A70

- **Slot:** 130.
- **Body:** a zero argument pushes `417BCh`, 0. A non-zero argument pushes `817BCh`, 1, value. Both paths store
  `mov dword_203B58, ecx` and return with `retn 4`.
- **5558 twin** 0x24A1B0: identical, RS[150]. The JSON k list is [0x417BC, 0x817BC].
- **Length:** 76 bytes against 82.
- **Confidence:** high.

### D3DDevice_SetRenderState_FillMode = 0x1F6AC0

- **Slot:** 119.
- **Body:** token `8038Ch` with front = argument and back = (`dword_203B34` ? `dword_203B30` : argument), which is
  (TwoSidedLighting ? BackFillMode : argument). It stores `mov dword_203B2C, ecx` and returns with `retn 4`.
- **5558 twin** 0x24A210: identical, reading 0x2648E4/0x2648E0 and storing RS[139].
- **Length:** 62 bytes against 68.
- **Confidence:** high.

### D3DDevice_SetRenderState_BackFillMode = 0x1F6B00

- **Slot:** 120.
- **Body:** `mov dword_203B30, eax` stores the argument first. It then pushes `8038Ch` with front = FillMode
  (`dword_203B2C`) and back = (TwoSidedLighting ? argument : FillMode), and rewrites FillMode. `retn 4`.
- **5558 twin** 0x24A260: identical, storing RS[140] first.
- **Length:** 75 bytes against 81.
- **Separation:** the three `8038Ch` setters are told apart by the slot the argument goes into (119, 120, 121) and by
  the polygon-mode formula. Each matches its 5558 twin.
- **Confidence:** high.

### D3DDevice_SetRenderState_TwoSidedLighting = 0x1F6B50

- **Slot:** 121.
- **Body:** `or dword ptr [eax+8], 20h`, then `mov dword_203B34, eax`, then the same `8038Ch` re-emit as BackFillMode.
  `retn 4`.
- **5558 twin** 0x24A2C0: additionally pushes the two-side-light enable `417C4h` inline and executes
  `or dword_2644A8, 9000h`. It stores RS[141] and re-emits `8038Ch` the same way.
- **Where 3925 emits 0x17C4:** the lazy light update 0x1F9790 does. It reads RS[121] (0x203B34), contains token
  `417C4h`, and is triggered by dirty bit 0x20. SetMaterial and SetLight set that same bit. In 5558, TwoSidedLighting
  and SetMaterial likewise share 0x9000.
- **Length:** 84 bytes against 128.
- **Confidence:** high.

### D3DDevice_SetRenderState_VertexBlend = 0x1F6BB0

- **Slot:** 117.
- **Body:** `or ecx, 2` into `[esi+8]`, then token `40328h` (skin mode) with the value, then
  `mov dword_203B24, ecx`, `retn 4`.
- **5558 twin** 0x24A340: `or edx, 200h`, then `40328h`, RS[137]. The JSON k list is [0x200, 0x40328].
- **Length:** 50 bytes against 65.
- **Confidence:** high.

### D3DDevice_SetRenderState_ZEnable = 0x1F7300

- **Slot:** 123.
- **Body:** token `4030Ch` with (argument != 0 && `[esi+410h]` != 0), where +0x410 is the depth surface. It stores
  `mov dword_203B3C, ecx`. If the old or the new value is 2 (`cmp eax, 2` / `cmp ecx, 2`), it calls 0x1F85E0 and
  0x1F82C0, then the control0 builder 0x1F7080. `retn 4`.
- **5558 twin** 0x24AEC0: `4030Ch` with `[esi+1A08h]` (the depth surface), plus the 5849-only `41D78h`. It stores
  RS[143] and runs the same `cmp eax, 2` / `cmp edx, 2` re-setup through the same builder.
- **Default state:** D3D_InitDefaultState passes `7Bh` (123) where 5558 passes `8Fh` (143).
- **Length:** 108 bytes against 143.
- **Confidence:** high.

### D3DDevice_SetRenderState_StencilEnable = 0x1F7370

- **Slot:** 124.
- **Body:** token `41D84h` with the occlusion value (see OcclusionCullEnable), then `4032Ch` with
  (argument && `[esi+410h]`). It stores `mov dword_203B40, edx` and returns with `retn 4`.
- **5558 twin** 0x24AF50: identical, `[esi+1A08h]`, RS[144]. The JSON k list includes 0x4032C and 0x41D84.
- **Length:** 128 bytes against 132.
- **Confidence:** high.

### D3DDevice_SetRenderState_StencilFail = 0x1F73F0 (anchor)

- **Slot:** 125.
- **Body:** `41D84h` with the occlusion value, then `40370h` with the argument. It stores `mov dword_203B44, ecx` and
  returns with `retn 4`.
- **5558 twin** 0x24AFE0: identical, RS[145].
- **Length:** 105 bytes against 109.
- **Confidence:** high.

### D3DDevice_SetRenderState_YuvEnable = 0x1F7460

- **Slot:** 138.
- **Body:** `mov dword_203B78, eax`, then the push reservation, then `call 0x1F7080`, then `mov [esi], eax`,
  `retn 4`.
- **Helper 0x1F7080:** chooses `10100001h` or `100001h` by RS[138], adds `or esi, 10000h` when ZEnable is 2
  (`cmp dword_203B3C, 2`), applies a depth-format test, and emits `40290h`.
- **5558 twin** 0x24B050: stores RS[160], then calls `_sub_0024A8F0`, the same builder (`10100001h`,
  `cmp dword_2648EC, 2`, `40290h`).
- **Length:** 37 bytes against 43.
- **Confidence:** high.

### D3DDevice_SetRenderState_OcclusionCullEnable = 0x1F7490 / D3DDevice_SetRenderState_StencilCullEnable = 0x1F74F0

- **Slots:** 139 and 140. Each is 92 bytes against 98, and each ends in `retn 4`.
- **Body:** each stores its argument first, 0x1F7490 with `mov dword_203B7C, eax` and 0x1F74F0 with
  `mov dword_203B80, eax`. Both then push `41D84h` with

  ```
  (RS[140] ? 2 : 0) | (RS[139] && (!RS[124] || RS[125] == 1E00h) ? 1 : 0)
  ```
- **5558 twins:** 0x24B080 stores 0x264934 (RS[161]) and 0x24B0F0 stores 0x264938 (RS[162]), with the same formula:
  RS[162] gives 2, and RS[161], gated by StencilEnable/StencilFail, gives 1.
- **Separation:** data flow, independent of slot order. The function whose slot feeds the gated bit 0 is
  OcclusionCullEnable, and the one feeding bit 1 is StencilCullEnable. That holds in both builds.
- **Confidence:** high.

### D3DDevice_SetRenderState_RopZCmpAlwaysRead = 0x1F7550 / RopZRead = 0x1F7570 / DoNotCullUncompressed = 0x1F7590

- **Slots:** 141, 142 and 143.
- **Body:** each is 17 bytes, the same length as its 5558 twin:
  `mov eax,[esp+4]; mov dword_203B84 / 203B88 / 203B8C, eax; call 0x1F7230; retn 4`.
- **Separation, by data flow in the shared helper.** 0x1F7230 and 5558's `_sub_0024A9F0` route each slot to the same
  bit:

  | 3925 slot | 3925 destination | 5558 slot | 5558 destination | Name |
  |---|---|---|---|---|
  | RS[143] 0x203B8C | bit 8 of `[dev+2B44h]` | 0x264944 | bit 8 of `[dev+2428h]` | DoNotCullUncompressed |
  | RS[141] 0x203B84 | `or eax, 100000h` in `[dev+2B48h]` | 0x26493C | `or eax, 100000h` | RopZCmpAlwaysRead |
  | RS[142] 0x203B88 | `8000000h` in `[dev+2B48h]` | 0x264940 | `8000000h` | RopZRead |

- **Push:** both helpers then push the same `40110h` / `81D8Ch` `400094h` / `40100h` / `81D8Ch` `400B80h` / `40100h`
  sequence.
- **Confidence:** high.

### D3DDevice_SetRenderState_MultiSampleAntiAlias = 0x1F75B0 / D3DDevice_SetRenderState_MultiSampleMask = 0x1F7600

- **Slots:** 132 and 133. Each is 76 bytes (5558: 114 and 80), and each ends in `retn 4`.
- **Body:** 0x1F75B0 stores `mov dword_203B60, eax` and 0x1F7600 stores `mov dword_203B64, eax`. Both then run
  0x1F82C0 and 0x1F6EB0 and push `41D7Ch` with `(RS[133] << 16) | (RS[132] & 0Fh)`.
- **5558 twins:** MultiSampleAntiAlias 0x24B220 stores 0x264910 (RS[152]), which feeds the low enable bit.
  MultiSampleMask 0x24B2A0 stores 0x264914 (RS[153]) and executes `shl eax, 10h`.
- **Separation:** whose argument lands in the high 16 bits (the mask) and whose lands in the low bits (the enable).
  That is the same in both builds.
- **Confidence:** high.

### D3DDevice_SetTextureState_TexCoordIndex = 0x1F6BF0

- **Convention:** `retn 8`.
- **Store:** `shl eax, 7; mov dword_2037C0[eax], ecx`, which is TextureState[stage][28].
- **Decode:** it decodes the high word into texgen modes `8511h`, `2400h`, `8512h`, `2401h` and `2402h`, keeps a
  per-stage mask in `[esi+4FCh]`, and pushes `(stage + 0C03Ch) << 4` (3 texgen dwords at 0x3C0 + 16·stage). It then
  ORs `194h` into the dirty flags.
- **5558 twin** 0x24A390 uses the same constants (`8511h`, `8512h`, `2400h`, `2401h`, `0C03Ch`), stores to
  dword_264520 ([stage][28]), keeps its mask in `[esi+950h]`, and ends in `retn 8`. The JSON k list includes 0x950,
  0x2400, 0x2401, 0x8511, 0x8512 and 0xC03C.
- **Callers:** NotInline calls it for type `1Ch` (0x1F6DF1).
- **Confidence:** high.

### D3DDevice_SetTextureState_BumpEnv = 0x1F6CD0

- **Convention:** `retn 0Ch`.
- **Body:** `lea ecx, ds:41AD0h[ebx*4]` with ebx = stage'·16 + type, then the value, then
  `shl esi, 5; add esi, eax; mov dword_203750[esi*4], ebp`. The stage adjustment depends on `[edi+414h]`.
- **5558 twin** 0x24A4A0: identical (`41AD0h`, `[edi+784h]`, dword_2644B0[stage*32+type]). The JSON k list is
  [0x784, 0x41AD0].
- **Callers:** NotInline reaches it through a tail `jmp` for types up to `1Bh` (0x1F6E30).
- **Length:** 93 bytes against 99.
- **Confidence:** high.

### D3DDevice_SetTextureState_BorderColor = 0x1F6D30

- **Convention:** `retn 8`.
- **Body:** `shl edx, 6; add edx, 41B24h` and the value, then `shl ecx, 7; mov dword_2037C4[ecx], edx`
  ([stage][29]).
- **5558 twin** 0x24A510: identical (`41B24h`, dword_264524).
- **Length:** 55 bytes against 61.
- **Confidence:** high.

### D3DDevice_SetTextureState_ColorKeyColor = 0x1F6D70

- **Convention:** `retn 8`.
- **Body:** `lea edx, ds:40AE0h[ecx*4]` and the value, then `mov dword_2037C8[ecx], edx` ([stage][30]).
- **5558 twin** 0x24A550: identical (`40AE0h`, dword_264528).
- **Length:** 51 bytes against 57.
- **Confidence:** high.

### D3DDevice_SetTextureStageStateNotInline = 0x1F6DB0

- **Convention:** `retn 0Ch`.
- **Dispatch:**
  - type < `16h`: `shl edx, 5; add edx, eax; mov dword_203750[edx*4], esi` and `or [ecx+8], dword_2036B0[eax*4]`.
  - `1Ch`: call 0x1F6BF0. `1Dh`: call 0x1F6D30. `1Eh`: call 0x1F6D70.
  - `cmp eax, 1Bh; jg` returns; otherwise `jmp 0x1F6CD0`.
- **5558 twin** 0x24A5A0 dispatches on the same constants to the named 5558 leaves. It has extra 5849 cases for
  types below 0xC and equal to 0xC.
- **Callers:** D3D_InitDefaultState calls it at 0x1FB5FC, 0x1FB60B, 0x1FB61C and 0x1FB627. 5558 calls the named
  SetTextureStageStateNotInline at the matching sites 0x24F9BD, 0x24F9CC, 0x24F9E5 and 0x24F9F0. D3DCoreFns
  independently reported the same pairing.
- **Multiplier:** the **stage stride is 32 dwords** (`shl edx, 5` here, and 0x80 bytes via `shl ecx, 7` in
  BorderColor). That is the same as 5849's `[stage*32 + type]`.
- **Confidence:** high.

### D3DDevice_SetTransform = 0x1F5360

- **Convention:** `retn 8`.
- **Copy:** `lea edi, [eax+22h]; shl edi, 6; add edi, ebx`, which is dev + 0x880 + State·64, then
  `mov ecx, 10h; rep movsd`. It then executes `or [ebx+8], dword_202578[State]`.
- **Projection (`cmp eax, 1`):** it derives `[ebx+4ECh]`, `[ebx+4F0h]` and `[ebx+4F4h]` from _33/_34/_43/_44 at
  `[edx+28h/2Ch/38h/3Ch]`. It sets the ortho bit 1 of `[ebx+0Ch]` when _14, _24 and _34 are 0 and
  `cmp [edx+3Ch], esi` succeeds with esi = `3F800000h`. It then calls 0x1F85E0 and 0x1F82C0.
- **5558 twin** 0x244FE0 has the same skeleton: `lea edi, [ecx+ebx+0C60h]`, `mov ecx, 10h; rep movsd`,
  dword_259C70[State], `cmp eax, 1`, `[ebx+93Ch/940h/944h]`, the same matrix offsets and `3F800000h`, and bit 1 of
  `[ebx+8]`.
- **Callers:** D3D_InitDefaultState calls it for States 0..9 with an identity matrix (0x1FB596), as 5558 does at
  0x24F956.
- **Neighbours:** it follows the 0x35-dword copy helper 0x1F5340, which is the twin of 5558 0x244E60.
- **Length:** 260 bytes against 280.
- **Confidence:** high.

### D3DDevice_SetMaterial = 0x1F54A0

- **Convention:** `retn 4`.
- **Body:** `lea edi, [eax+0B18h]; mov ecx, 11h; rep movsd` (17 dwords is sizeof(D3DMATERIAL8)), then `or ecx, 20h`
  into `[eax+8]`.
- **5558 twin** 0x2451D0: `add edi, 0F00h; mov ecx, 11h; rep movsd; or ecx, 9000h; retn 4`.
- **Front, not back, material:** the light update 0x1F94B0 sends `[esi+0B18h]` to the front-material methods
  (`0C03A8h`, `403B4h`) and `[esi+0B5Ch]` (= +0x44) to the back-material methods (`0C17B0h`, `417ACh`). 5558's
  SetBackMaterial (0x245220) likewise writes 0xF00 + 0x44.
- **Neighbours:** it follows GetTransform 0x1F5470 directly in 3925. In 5558 it comes after GetTransform 0x245100 and
  GetViewport 0x245190; 3925 does not link GetViewport.
- **Length:** 38 bytes against 48.
- **Confidence:** high.

### D3DDevice_SetViewport = 0x1F5B70

- **Convention:** `retn 4`.
- **Clamp:** the same render-target clamp as 5558. It takes the size from the surface Size field (`and 0FFFh; inc`)
  or from the format log2 fields (`shr 14h; and 0Fh` and byte `[+0Fh] & 0Fh`).
- **Stores:** X, Y, W, H, MinZ and MaxZ at `[esi+0B00h]..[esi+0B14h]`.
- **Calls:** 0x1F85E0, then `push 0; push 0; push 0; call 0x1F6300`, which is SetScissors(0, 0, NULL), then the push
  reservation and the viewport push 0x1F6EB0. It then executes `or eax, 1`.
- **5558 twin** 0x246B40 has the same clamp code, stores to `[esi+0EE0h..0EF4h]`, and executes
  `push 0 ×3; call D3DDevice_SetScissors` before its own viewport push.
- **Length:** 347 bytes against 546. 5558 adds supersample scaling.
- **Confidence:** high.

### D3DDevice_SetLight = 0x1F5CD0

- **Convention:** `retn 8`.
- **Growth:** it grows the array at `[ebp+480h]` when the index is at least `[ebp+484h]`: `and eax, 0FFFFFFF0h`,
  `lea eax, [eax+eax*8]; shl eax, 4` (0x90 bytes per light). It returns `8007000Eh` when the allocation fails.
- **Copy:** `mov ecx, 1Ah; rep movsd` (D3DLIGHT8), then `or ecx, 3` at +68h.
- **Type tests:** `cmp eax, 1` skips the direction for a point light. `cmp dword ptr [ebx], 2` computes the spot cone
  terms at +78h..+88h from +50h/+60h/+64h. It then executes `or eax, 20h` and returns 0.
- **5558 twin** 0x246D70 has the same growth (`and eax, 0FFFFFFF0h`, `8007000Eh`), the same
  `mov ecx, 1Ah; rep movsd` and `or ecx, 3`, the same type tests and the same record offsets. It ends in `retn 8`.
- **Length:** 472 bytes against 691.
- **Confidence:** high.

### D3DDevice_LightEnable = 0x1F5EB0

- **Convention:** `retn 8`.
- **Prologue:** the same as 5558: `sub esp, 68h`, then a bounds test against `[esi+484h]`, then
  `test byte ptr [ecx+eax+68h], 1`.
- **Default light:** it builds the D3D default light (`rep stosd` of 1Ah dwords, Type = 3, and four `3F800000h`) and
  calls **0x1F5CD0**. That is the same call to D3DDevice_SetLight that 5558 0x247030 makes.
- **Enable:** `or ebx, 4`, then list maintenance through +8Ch and `[esi+488h]`, then
  `or dword ptr [esi+8], 20h`.
- **Difference:** 5558 assigns hardware slots instead of keeping a list.
- **Length:** 222 bytes against 337.
- **Confidence:** high.

### D3DDevice_SetTexture = 0x1F5F90

- **Convention:** `retn 8`.
- **Release:** the old texture is at `[edi+ebx*4+0BA0h]`. It stamps it with `mov ecx, [edi+1Ch]; mov [eax+8], ecx`,
  decrements it with `add edx, 0FFF80000h; test ecx, 78FFFFh`, and frees it through 0x1FC6A0 when it reaches zero. It
  then stores the new texture.
- **NULL texture:** it pushes `41B0Ch + 64·stage`, 0, then executes `or dword ptr [edi+8], 408h`.
- **Non-NULL texture:** it calls AddRef, then pushes `(stage + 206Ch) << 6` (offset and format). When the texture's
  Size is set, it also pushes `41B10h` and `41B1Ch`. On a format change it pushes the `41B0Ch` control0 and executes
  `or edx, 400h`. With no old texture it also executes `or eax, 8`.
- **5558 twin** 0x2471C0 has the same structure: `[ebp+esi*4+0F88h]`, the stamp `[ebp+2Ch]`, `0FFF80000h` and
  `78FFFFh`, `41B0Ch`, `206Ch`, `41B10h` and `41B1Ch`, then `or eax, 4800h` / `or ecx, 4000h` / `or eax, 800h`.
- **Length:** 344 bytes against 420.
- **Confidence:** high.

### D3DDevice_SetIndices = 0x1F6170

- **Convention:** `retn 8`.
- **Body:** AddRef the new buffer, then `mov eax, [edi+4]; mov dword_20374C, eax`, writing 0 for NULL. It releases
  the old `[esi+47Ch]` (`0FFF80000h`, `78FFFFh`), executes `mov [esi+47Ch], edi`, and stores BaseVertexIndex in
  `[esi+478h]`.
- **5558 twin** 0x247430: identical, using `dword_2644A4`, `[esi+7A0h]` and `[esi+1Ch]`. The JSON k list is
  [0x7A0, 0x80000, 0xFFF80000].
- **Length:** 141 bytes against 132.
- **Confidence:** high.

### D3DDevice_SetScissors = 0x1F6300

- **Convention:** `retn 0Ch`.
- **Count 0:** it builds one rectangle from the viewport, `[ebp+0B00h..0B0Ch]`.
- **Body:** `402B4h` (clip type) with Exclusive. Then, for each rectangle, it loads `esi = 402E0h` and uses
  `lea edx, [esi-20h]` for horizontal (0x2C0 + 4i) and `esi` for vertical (0x2E0 + 4i). Each edge value is
  `((hi*s + 0.5) - 1) << 16 | (lo*s + 0.5)`, with s from `[ebp+500h]` and `[ebp+504h]`. It saves the rectangles to
  `[ebp+227Ch]`, the count to `[ebp+22FCh]`, and Exclusive to `[ebp+2300h]`.
- **5558 twin** 0x247C40: `402B4h`, the same loop (`mov esi, offset 402E0h; lea ecx, [esi-20h]`), scales
  `[ebp+95Ch]`/`[ebp+960h]`, `retn 0Ch`.
- **Callers:** SetViewport calls it with (0, 0, 0) in both builds.
- **Length:** 392 bytes against 768.
- **Confidence:** high.

### D3DDevice_SetStreamSource = 0x1F8000

- **Convention:** `retn 0Ch`.
- **Body:** AddRef the new buffer, then compute n·12 with `lea esi, [eax+eax*2]; shl esi, 2`. It releases the old
  `dword_206730[esi]` (stamped with `[edi+1Ch]`). It compares the stride with `dword_206728[esi]` and ORs `200h` if
  unchanged or `280h` if changed into `[edi+8]`. It then executes `mov dword_206730[esi], ebx` and
  `mov dword_206728[esi], eax`.
- **5558 twin** 0x24BE90: the same `lea/shl` n·12, buffer `dword_266DF8`, stride `dword_266DF0`, `or ecx, 40h` /
  `70h`, `retn 0Ch`.
- **Length:** 133 bytes against 140.
- **Confidence:** high.

## Not proven

| Name | Reason |
|---|---|
| D3DDevice_SetRenderState_MultiSampleMode | Not emitted. 3925's slot 134 is D3DRS_MULTISAMPLETYPE, handler **0x1F65F0**. It is stdcall with 1 argument (`retn 4`), stores RS[134], and calls SetRenderTarget(`[dev+40Ch]`, `[dev+410h]`). The position equals 5849's MultiSampleMode: the same table position, and InitDefaultState skips it as 5558 skips 154. But the slot holds a D3DMULTISAMPLE_TYPE bit-field (see the render-state section), not a D3DMULTISAMPLEMODE, and the 5849 API does not exist in this XDK. Main can choose to bind it knowingly. |
| D3DDevice_SetRenderState_MultiSampleRenderTargetMode | Not in XDK 3925. The 28-entry handler table goes from 134 straight to ShadowFunc (135), and no function stores to a spare `D3D_RenderState` slot. |
| D3DDevice_SetRenderState_SampleAlpha | Not in XDK 3925. LineWidth (136) is followed directly by Dxt1NoiseEnable (137). The `41D7Ch` value is built from MultiSampleAntiAlias and MultiSampleMask only (`and edx, 0Fh`, `shl ecx, 10h`). |
| D3DDevice_SetGammaRamp | Not linked. The only `mov ecx, 0C0h` (a 768-byte ramp copy) in the image is in game code, at 0x28651 and 0x3092F. In 5558, SetGammaRamp (0x244EC0) lies between the 0x35-dword helper 0x244E60 and SetTransform 0x244FE0. In 3925 the twin helper 0x1F5340 is followed directly by SetTransform 0x1F5360. |
| D3DDevice_SetFlickerFilter | Not linked. AvSendTVEncoderOption (thunk 0x253450, kernel ordinal 2) is called only at 0x1FB65E (option 9), 0x1FD423 (6), 0x1FE947 (9), 0x202254 (`push 0Bh` with value 0, inside the mode-set routine 0x202150, `retn 10h`) and 0x202266 (`0Fh`). No `retn 4` wrapper exists like 5558's `push 0Bh; call dword_2E1E48`. In 5558 the filter setters sit between BlockUntilVerticalBlank and InsertFence; in 3925 the shape twins 0x1F5520 (36 bytes) and 0x1F5550 (8 bytes, `push 0; call 0x1FAF80`) are adjacent. |
| D3DDevice_SetSoftDisplayFilter | Not linked. Same evidence as SetFlickerFilter; no call site passes option 0x0E. |
| D3DDevice_SetScreenSpaceOffset | Not linked, and 3925 has no per-device offset. The viewport push 0x1F6EB0 adds the constant `flt_2025A4` = 0.53125 (`fadd flt_2025A4` at 0x1F6EDA and 0x1F6F02, its only two xrefs, neither a write). 5558's `_sub_0024A700` adds `[esi+0EF8h]`/`[esi+0EFCh]` at the same point. No two-float `retn 8` function follows the PersistDisplay-shaped 0x1F6490. |
| D3DDevice_SetVertexShaderConstant1 | Not present. All four 5849 forms are fastcall (ecx = register, edx = data) and write D3D_Device.Push by absolute address. Only Simple does that in this image. No code contains the 4-dword constant header `100B80h`; a byte search found 0 hits. The only users of `41EA4h` (constant load) are 0x1F84A0 (stdcall, `retn 0Ch`: the public SetVertexShaderConstant(Register, pData, Count), with `800B80h` chunks), 0x1F7DC0 (shader-program parser, `retn 8`), 0x1F8160 (`retn 4`) and 0x1F82C0 (internal, ecx = device). |
| D3DDevice_SetVertexShaderConstant1Fast | Not present; same evidence. |
| D3DDevice_SetVertexShaderConstant4 | Not present; same evidence. |
| D3DDevice_SetVertexShaderConstantNotInlineFast | Not present; same evidence. 5558's version also starts from the global push cursor (`mov edi, dword_264950`). |

## Other functions identified in passing (outside the adopted set, not emitted)

| Address | Identity | Proof |
|---|---|---|
| 0x1F5470 | D3DDevice_GetTransform | `retn 8`; `esi = dev + (State + 22h) << 6`, `mov ecx, 10h; rep movsd` into pMatrix; twin 5558 0x245100. |
| 0x1F60F0 | D3DDevice_SetPalette | `retn 8`; releases and stores `[edi+esi*4+0BB0h]`, pushes `(stage << 6) + 41B20h` with `[pal+4] | ([pal] >> 1Ch)`; twin 5558 0x2473A0 (`[esi+edi*4+0F98h]`, `41B20h`). |
| 0x1F65F0 | D3DDevice_SetRenderState_MultiSampleType | see "Not proven" |
| 0x1F69F0 | D3DDevice_SetRenderState_ZBias | table slot 129; turns the integer bias into slope and offset and calls SetRenderStateNotInline for states `4Dh`..`51h` (77..81), storing RS[129]. |
| 0x1F66A0 | D3DDevice_SetRenderStateNotInline | D3DCoreFns' set; proof in the render-state section |
| 0x1F6680 / 0x1F6700 | inline helpers for deferred render states and deferred texture-stage states | fastcall; `or dword_203B98` and `mov dword_203950[ecx*4]` / `mov dword_203750[(stage*32+type)*4]` |
| 0x1F84A0 | D3DDevice_SetVertexShaderConstant (stdcall) | D3DCoreFns reported the same |

## D3D data symbols for XDK 3925

`D3D_Device` = 0x203B90 and `D3D_pDevice` = 0x206720 were given by Main and are consistent with every body above.

| Symbol | Value | Proof |
|---|---|---|
| `D3D_RenderState` | **0x203950** | SetRenderStateNotInline 0x1F66BD and 0x1F66DF `mov dword_203950[esi*4], …`. Pairs: FogColor 118 → 0x203B28 (0x1F67CD); CullMode 127 → 0x203B4C (0x1F6838); LineWidth 136 → 0x203B70 (0x1F6965); StencilFail 125 → 0x203B44 (0x1F744F); ZWRITEENABLE 64 → 0x203A50 (game 0x39FF2). All fit one base **with the 3925 numbering** (144 states, ends at D3D_Device). With 5849 numbering they do not: FogColor 138 gives 0x203900, but LineWidth 157 gives 0x2038FC. |
| `D3D_DirtyFlags` | **0x203B98** (= D3D_Device + 8) | 0x1F66D5 and 0x1F6687 `or dword_203B98, eax` where 5558 0x249948 executes `or dword_2644A8, eax` (D3D__DirtyFlags). SetTexture `or dword ptr [edi+8], 408h` where 5558 executes `or dword_2644A8, 4800h`. SetStreamSource `or ecx, 200h/280h` into `[edi+8]` where 5558 ORs `40h/70h` into dword_2644A8. **This differs from 5849**: there it is a separate global and CDevice+8 is CDevice_Flags. |
| `D3D_DeferredTextureState` | **0x203750** ([4][32]) | NotInline 0x1F6DD4 `mov dword_203750[edx*4], esi` with edx = stage·32 + type; BumpEnv 0x1F6D20; BorderColor writes 0x2037C4 = [0][29]; ColorKeyColor writes 0x2037C8 = [0][30]; TexCoordIndex writes 0x2037C0 = [0][28]. It is exactly 0x200 bytes below D3D_RenderState. |
| `D3D_Streams` | **0x206728** | SetStreamSource 0x1F8051 `mov ecx, dword_206728[esi]` (stride), 0x1F8021/0x1F8074 `dword_206730[esi]` (buffer), with esi = n·12; the DrawVertices helper 0x1F9E4F reads `dword_206728[n*12]` too. |
| `D3D_IndexData` | **0x20374C** | SetIndices 0x1F6189 `mov dword_20374C, eax` with eax = `[pIndexData+4]`, and 0x1F6190 stores 0 for NULL, where 5558 0x247449 stores `dword_2644A4`. |
| `D3D_SimpleRenderStateEncode` | **0x253520** | SetRenderStateNotInline 0x1F66AA `mov ecx, dword_253520[esi*4]` for esi < 82; 82 entries identical to 5558's 0..81 (see the simple-block section). |

Layout values:

| Key | Value | Proof |
|---|---|---|
| `DirtyFlags_Texture` | **0x408** | SetTexture 0x1F6002 `or dword ptr [edi+8], 408h` on the NULL-texture path, where 5558 0x247247 executes `or eax, 4800h`. The parts map one to one: 0x400 ↔ 0x4000 (format change, 0x1F60C6 ↔ 0x247320) and 0x8 ↔ 0x800 (enable change, 0x1F60D8 ↔ 0x24734D). |
| `TextureState_Stage` | **32** | NotInline 0x1F6DC3 `shl edx, 5; add edx, eax`; deferred TSS helper 0x1F670E `shl ecx, 5`; BumpEnv 0x1F6D1A `shl esi, 5`; byte form `shl ecx, 7` in BorderColor/ColorKeyColor/TexCoordIndex. |
| `Stream_Size` | **12** | SetStreamSource 0x1F801B `lea esi, [eax+eax*2]; shl esi, 2` |
| `Stream_Stride` | **0x0** | SetStreamSource 0x1F807A `mov dword_206728[esi], eax` (Stride) |
| `Stream_Buffer` | **0x8** | SetStreamSource 0x1F8074 `mov dword_206730[esi], ebx` (pStreamData) |

## CDevice layout for XDK 3925 (from state setters)

```toml
CDevice_Transforms        = 0x880   # D3DDevice_SetTransform 0x1F5371 lea edi,[eax+22h]; shl edi,6; add edi,ebx -> +0x880 + type*64, then 16-dword rep movsd
CDevice_Textures          = 0xBA0   # D3DDevice_SetTexture 0x1F5F9E reads, 0x1F5FD3 stores [edi+ebx*4+0BA0h] ([stage])
CDevice_Palettes          = 0xBB0   # D3DDevice_SetPalette 0x1F60FD reads, 0x1F6135 stores [edi+esi*4+0BB0h] ([stage])
CDevice_Viewport          = 0xB00   # D3DDevice_SetViewport 0x1F5C6D-0x1F5C91 stores X, Y, Width, Height, MinZ, MaxZ at +0xB00..+0xB14
CDevice_ScreenSpaceOffset = none    # no field: SetScreenSpaceOffset not linked; viewport push 0x1F6EDA/0x1F6F02 adds the constant flt_2025A4 (0.53125)
CDevice_GammaRamps        = 0x251C  # derived, NOT setter-proven: miniport at CDevice+0x2308 (device init 0x1FBBDC add esi,2308h -> call 0x1FE540); 0x1FDF83 computes this+0x214+i*0x300
CDevice_GammaPending      = 0x2B1C  # derived, NOT setter-proven: 0x1FD90E cmp [esi+edi*4+814h],1 then uploads this+0x214+i*0x300 (0x1FD918); 0x1FD77A same test
```

`CDevice_GammaRamps` and `CDevice_GammaPending` are recommended **not** to be wired for this title. Nothing in the image
writes those ramps (SetGammaRamp is not linked), the derivation goes through the miniport's own code rather than a
setter, and the 3925 miniport selects the slot with its own counter `[miniport+210h] & 1`, not CDevice_SwapCount.

Other CDevice fields proven by the same bodies. `Direct3D_CreateDevice` requires several of them:

```toml
CDevice_Push          = 0x0     # 0x1FB3B4 mov eax,[ecx] (StartPush), Simple 0x1F6650 mov eax,dword_203B90
CDevice_PushThreshold = 0x4     # 0x1FB3B6 cmp eax,[ecx+4]; Simple 0x1F6658 cmp eax,dword_203B94
CDevice_Flags         = 0xC     # Direct3D_CreateDevice 0x1F767F/0x1F76AE: dword_203B9C |= BehaviorFlags & 0x10; SetVertexShaderConstant 0x1F84B1 mov cl,[ebx+0Ch] / 0x1F84BA test cl,10h; Dxt1NoiseEnable toggles [esi+0Ch] bit 0 where 5558 toggles [edi+8]
CDevice_Time          = 0x1C    # SetTexture 0x1F5FAD / SetPalette 0x1F6108 / SetStreamSource 0x1F802B stamp the released resource with [dev+1Ch] where 5558 uses [dev+2Ch] (5849 CDevice_Time)
CDevice_RenderTarget  = 0x40C   # MultiSampleType 0x1F6605 passes [eax+40Ch] as SetRenderTarget's first argument; Dxt1NoiseEnable 0x1F697C reads its format where 5558 reads [edi+1A04h]
CDevice_Depth         = 0x410   # MultiSampleType 0x1F65FF passes [eax+410h] as the Z argument; ZEnable 0x1F7315 / StencilEnable 0x1F73C5 test it where 5558 tests [esi+1A08h]
CDevice_PixelShader   = 0x414   # TextureFactor 0x1F68C7 / BumpEnv 0x1F6CDE test [dev+414h] where 5558 tests [dev+784h] (5849 CDevice_PixelShader)
CDevice_BaseVertex    = 0x478   # D3DDevice_SetIndices 0x1F61C9/0x1F61DE/0x1F61F3 store BaseVertexIndex where 5558 stores [esi+1Ch]
CDevice_IndexBuffer   = 0x47C   # D3DDevice_SetIndices 0x1F619A reads the old one, 0x1F61C2 stores the new one where 5558 uses [esi+7A0h]
CDevice_ZScale        = 0x4F8   # viewport push 0x1F6FB4 fmul [ecx+4F8h] on MaxZ-MinZ, MinZ, where 5558 _sub_0024A700 uses [esi+948h]
CDevice_SuperSampleX  = 0x500   # viewport push 0x1F6ECC fmul [ecx+500h] on X; SetScissors 0x1F636E, where 5558 uses [esi+95Ch]
CDevice_SuperSampleY  = 0x504   # viewport push 0x1F6EEE fmul [ecx+504h] on Y; SetScissors 0x1F6374, where 5558 uses [esi+960h]
CDevice_Material      = 0xB18   # SetMaterial 0x1F54AB (front material); the back material follows at 0xB5C (light update 0x1F94B0: 0x1F94D1 lea edx,[esi+0B18h] front, 0x1F9624 lea edx,[esi+0B5Ch] back)
CDevice_AutoDepth     = 0x2198  # D3D_InitDefaultState 0x1FB5D0 mov esi,[ebp+2198h] -> ZEnable, where 5558 0x24F997 reads [ebx+1A20h] (5849 CDevice_AutoDepth)
```
