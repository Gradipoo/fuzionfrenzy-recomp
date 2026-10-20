# DSOUND 3925 gaps in Fuzion Frenzy: complete wrapper census and the missing host bodies

Target image: `out/ff.exe`, DSOUND section `0x206980-0x221840` (code to `0x20EA7D`).
Builds on `docs/dsound-symbols.md` (the 23 proven names); this file covers the four
wrappers that document identified but did not emit (its section 6), proves the census
complete, and records the host bodies added for the three of them that needed one.

## 1. How the census was made (and why it is complete)

- **Wrapper inventory.** Every interface wrapper in this library adjusts `this` by
  `-8` (`IDirectSound*` = `CDirectSound + 8`) or `-0x1C` (`IDirectSoundBuffer*` =
  `CDirectSoundBuffer + 0x1C`). A byte-pattern scan of all DSOUND code
  (`0x206980-0x20EA7D`) for `add eax,-8`, `add eax,-0x1C`, `lea ecx,[eax-8]` and
  `lea ecx,[eax-0x1C]` finds exactly 8 sites of the first kind and 15 of the second.
  That is the whole wrapper population: **23 interface wrappers, no more exist.**
  The three remaining entry points (`DirectSoundUseFullHRTF`, `DirectSoundDoWork`,
  `DirectSoundCreate`) take no interface `this` and are found by the call scan below.
- **Game callers.** A raw scan of every `E8`/`E9 rel32` in the image whose target
  lands in DSOUND code, restricted to sources below `0x1F5340` (the game's own code;
  DSOUND itself starts at `0x206980`), finds **28 distinct targets**. One of them,
  `0x208D02`, is reached only from the `.text` adjustor thunk at `0x34711`
  (`sub ecx,8; jmp`) — the CMcpxAPU secondary-base destructor, not a game API call.
  The other **27 are exactly the game's DSOUND entry points**, and every one of the
  23 wrappers is among them: **there is no wrapper in this image that the game does
  not call.** Each distant `GetCaps` site was disassembled and checked to be a real
  call (section 3), not a coincidental `E8` byte in data.
- **Host bodies.** "Host body" means an `HLE_STDCALL` (directly or via the
  `BUFFER(...)`/`STREAM(...)` macros) in `sdk/runtime/xdk/dsound.cpp`.

## 2. The complete census: every wrapper the game calls

| Wrapper | retn | Guest method it calls | Name | Host body before this change | Game calls it |
|---|---|---|---|---|---|
| `0x206A55` | 4 | vtable slot 2 (`[vtbl+8]`) of CDirectSound | `IDirectSound_Release` | yes | yes, 3 sites (`0x55536` loop) |
| `0x206A6B` | 20 | none (`xor eax,eax; retn 14h`) | `IDirectSoundBuffer_Unlock` | yes (`BUFFER(Unlock,5)`) | yes, 2 sites |
| `0x206A70` | 4 | vtable slot 2 of CDirectSoundBuffer | `IDirectSoundBuffer_Release` | yes | yes, 9 sites |
| `0x206A86` | 0 | DSOUND lock, installs full-HRTF lookup `0x20B291` | `DirectSoundUseFullHRTF` | yes | yes, `0x554DD` |
| `0x207253` | 8 | `0x206B38` → `0x2069EB` (APU voice/SGE counts) | `IDirectSound_GetCaps` | **NO — fixed here** | yes, 13 sites |
| `0x20726F` | 20 | `0x206B88` (effects-image load) | `IDirectSound_DownloadEffectsImage` | yes | yes, `0x554A8` |
| `0x207296` | 12 | `0x206BCC` → `0x208BE9` (16-dword copy to APU+0x144) | `IDirectSound_SetAllParameters` | **NO — fixed here** | yes, `0x55AC2` |
| `0x2072B6` | 12 | `0x206C07` → APU+0x178 (`flDistanceFactor`) | `IDirectSound_SetDistanceFactor` | yes | yes, `0x559B5` |
| `0x2072DA` | 4 | `0x206C91` → APU commit `0x20851A` | `IDirectSound_CommitDeferredSettings` | yes | yes, 2 sites |
| `0x2072F2` | 12 | `0x206C46` → 12-dword copy to APU+0x184 | `IDirectSound_SetI3DL2Listener` | yes | yes, `0x554FA` |
| `0x207312` | 8 | `0x206D45` (volume minus headroom → PIO voice volumes) | `IDirectSoundBuffer_SetVolume` | yes | yes, 10 sites |
| `0x20732E` | 12 | `0x2071D1` → voice+0x54 (`flMaxDistance`) | `IDirectSoundBuffer_SetMaxDistance` | yes | yes, `0x558FC` |
| `0x207352` | 12 | `0x2071E9` → voice+0x50 (`flMinDistance`) | `IDirectSoundBuffer_SetMinDistance` | yes | yes, `0x558ED` |
| `0x207376` | 20 | `0x207201` → voice+0x20 (`vPosition`) | `IDirectSoundBuffer_SetPosition` | yes | yes, 2 sites |
| `0x2073AB` | 20 | `0x20722A` → voice+0x2C (`vVelocity`) | `IDirectSoundBuffer_SetVelocity` | yes | yes, 2 sites |
| `0x2073E0` | 8 | `0x206F34` → CMcpxBuffer status | `IDirectSoundBuffer_GetStatus` | yes | yes, 2 sites |
| `0x2073FC` | 12 | `0x206F65` → play/write cursors | `IDirectSoundBuffer_GetCurrentPosition` | yes | yes, 3 sites |
| `0x20741C` | 8 | `0x207040` → PIO SET_VOICE_BUF_CBO / stored position | `IDirectSoundBuffer_SetCurrentPosition` | yes | yes, `0x6A311` |
| `0x207438` | 32 | `0x206F9A` (both DSBLOCK flags) | `IDirectSoundBuffer_Lock` | yes | yes, 2 sites |
| `0x207468` | 0 | `0x206CBC` on the global CDirectSound | `DirectSoundDoWork` | yes | yes, 2 sites (one tail-jump) |
| `0x207953` | 8 | `0x207633` (Hz → pitch → PIO SET_VOICE_TAR_PITCH) | `IDirectSoundBuffer_SetFrequency` | yes | yes, 9 sites |
| `0x20796F` | 16 | `0x2077AB` → PlayEx `0x206EA1` | `IDirectSoundBuffer_Play` | yes | yes, 7 sites |
| `0x207993` | 4 | `0x2077DD` → StopEx `0x206EDE` | `IDirectSoundBuffer_Stop` | yes | yes, 8 sites |
| `0x2079AB` | 12 | `0x207736` (attach data, reset loop region) | `IDirectSoundBuffer_SetBufferData` | yes | yes, 5 sites |
| `0x2079CB` | 12 | `0x20780D` → `0x2070E7` (copy + sort notify entries) | `IDirectSoundBuffer_SetNotificationPositions` | **NO — fixed here** | yes, 2 sites |
| `0x207D05` | 16 | `0x207C3D` (allocates CDirectSoundBuffer, returns `this+0x1C`) | `IDirectSound_CreateSoundBuffer` | yes | yes, 5 sites |
| `0x207E35` | 12 | constructor `0x2074F3` + init `0x207D29`, returns `this+8` | `DirectSoundCreate` | yes | yes, `0x55456` (plus its address stored at `0x6A68C`) |

Per-name proof for the 23 rows already in `config/found-dsound.toml` is in
`docs/dsound-symbols.md` section 4; the four section-6 names are proven below.

## 3. The risk list: "game calls it, host lacks it"

Before this change there were exactly three such rows. Each is a future fault of
exactly the reported kind: the guest wrapper walks a host-allocated object
(`0xB01xxxxx` range) that has no console layout, and dereferences null.

| Name | Wrapper | Why it faults without a body | Status |
|---|---|---|---|
| `IDirectSound_GetCaps` | `0x207253` | Guest `0x206B38` reads `[this+0xC]` (the APU pointer) off the host object. This is the reported boot fault at `0x55414`. | **Fixed**: host body added |
| `IDirectSound_SetAllParameters` | `0x207296` | Guest `0x206BCC` likewise reads `[this+0xC]` and copies through the APU. Next fault after GetCaps, on the deferred-listener path at `0x55AC2`. | **Fixed**: host body added |
| `IDirectSoundBuffer_SetNotificationPositions` | `0x2079CB` | Guest `0x20780D` dereferences the buffer's inner voice (`[this+0x24]` after adjust). Would fault when the game's double-buffered stream arms its first notification (`0x57521`). | **Fixed**: host body added |

**Remaining risk after this change: none in DSOUND.** Every game-called wrapper now
has a host body, and no uncalled wrapper exists that could become a caller later
without new guest code. The only way this list grows is a different subsystem.

## 4. Evidence for the three new addresses

### `IDirectSound_GetCaps = 0x207253` (2 args, `retn 8`)

- Shape: `this-8` adjust, pushes `(this, pCaps)`, calls `0x206B38`. That callee takes
  the DSOUND lock, then calls `0x2069EB(APU=[this+0xC], pCaps, &pCaps[1], &pCaps[2])`,
  which writes `movzx [APU+0x114]`, `movzx [APU+0x116]` and `movzx [APU+0x134]` into
  the three out-pointers; the callee then writes `[0x22178C]+[0x221790]` (pool plus
  contiguous bytes allocated, bumped in the allocator at `0x20CB7E`/`0x20CBBE` region)
  to `pCaps[3]` on success. So DSCAPS in 3925 is **four dwords**:
  `dwFree2DHwBuffers, dwFree3DHwBuffers, dwFreeBufferSGEs, dwMemoryAllocated`.
  **There is no `dwSize` field and the XDK never reads one** — it overwrites offset 0.
- The APU constructor (`0x2088D2`) initialises `[APU+0x114]=0xC0` (192) and
  `[APU+0x116]=0x40` (64), the MCPX 2D/3D voice split. Nothing in the image writes
  `[APU+0x134]`.
- Game side: all 13 call sites pass the global DirectSound handle (`mov ecx,
  [0x2DB808]`) and a 16-byte stack buffer, and only log the result. Two sites
  (`0x5540F` in `bInitSpecificWave`, `0x5559D`) print all four fields with
  `"Free2DBuffers = %d, Free3DBuffers = %d, FreeBufferSGEs = %d, MemoryAllocated =
  %d Kb\n"` (`0x253A20`, last field `>>10`); the other eleven print one field
  (`"Free3DBuffers = %d\n"`, `0x266880`, and equivalents). No site sets `dwSize`
  first and no site branches on any field. This is the faulting call: the return
  address on the reported stack is `0x55414`, the instruction after the `0x5540F`
  call.
- Not `GetSpeakerConfig`/`EnableHeadphones` (the other ret-8 candidates): those
  write/need one dword or none; this writes four. See `docs/dsound-symbols.md` 5c.

### `IDirectSound_SetAllParameters = 0x207296` (3 args, `retn 0xC`)

- Shape: `this-8` adjust, calls `0x206BCC`, which forwards `(pParams, !apply)` to
  `0x208BE9`; that does `rep movsl` of **16 dwords** into `APU+0x144` — the
  DS3DLISTENER block whose layout the APU constructor proves (docs section 1).
- Game side: the single caller at `0x55AC2` builds a 0x40-byte DS3DLISTENER on the
  stack with `dwSize=0x40` (`0x559EB`), position/velocity zero, front/top unit,
  `flDistanceFactor` from its global `0x285A58`, rolloff 1.0, doppler 8.0, passes
  `DS3D_DEFERRED` (1), then calls `CommitDeferredSettings` at `0x55ACE`. The game
  sets the whole listener this way because 3925 has no individual listener
  position/velocity/orientation wrappers (docs 5c).

### `IDirectSoundBuffer_SetNotificationPositions = 0x2079CB` (3 args, `retn 0xC`)

- Shape: `this-0x1C` adjust, calls `0x20780D`, which frees/allocates a per-voice
  array (`count*8` bytes via `0x20CA6F`), stores the count at `voice+0x28`, and
  bubble-sorts the `(dwOffset, hEventNotify)` entries by offset (`0x2070E7` with
  swap helper `0x2069C8`). DSBPOSITIONNOTIFY is therefore 8 bytes: dword offset,
  dword event handle.
- Game side: two call sites, both `(buffer, 1, &notify)` with a single entry whose
  offset alternates 0 / `0x1F800` (the two halves of its 0x3F000-byte streaming
  buffer) and whose event is the handle at stream-object `+0x44`: play start at
  `0x57521`, re-arm in the update path at `0x575CF` (counter incremented at
  `0x575D4`). The refill logic in `0x57220` keys off that counter, so the event
  must actually be set as the play cursor passes the offset, or the stream stalls.

### Why none of the three is in the 5849/5558 sets

`grep` over `sdk/xdk/5849/signatures.toml` and `sdk/xdk/5558/signatures.toml`:
neither contains `IDirectSound_GetCaps`, `IDirectSound_SetAllParameters` or
`IDirectSoundBuffer_SetNotificationPositions` (the buffer-wide
`IDirectSoundBuffer_SetAllParameters` at xtlid `0xB005F` is a different function and
already has a host body). The 3925 `.XTLID` seed in `sdk/xdk/3925/signatures.toml`
likewise had no rows for them, so new hand-added rows were required (section 6).

## 5. What each new host body does, and why

All three are in `sdk/runtime/xdk/dsound.cpp`, additive only; no existing body
changed behaviour. Fallback `SYM_` defines (`FFFFFF0E/0F/10`) follow the file's
existing pattern so titles whose config has no such row still compile, and the
names are added to the `AuditExports` handled list like every other fallback name.

- **`IDirectSound_GetCaps(this, pDSCAPS)`** — writes the four dwords and returns 0.
  Free 2D voices 192 and free 3D voices 64 are the APU constructor's initial counts
  (what the guest body would report on an idle console). The SGE count has no
  writer anywhere in this image and the host mixer keeps no allocation counter, so
  SGEs and memory-allocated honestly read as zero; the title only logs all four
  (memory as `bytes>>10` Kb), so zero cannot mislead a branch — there is none.
  It deliberately does **not** preserve a `dwSize` at offset 0: 3925 has no such
  field there, and preserving it would print stack garbage as "Free2DBuffers".
- **`IDirectSound_SetAllParameters(this, pDS3DLISTENER, apply)`** — copies the
  listener position (`+4`), orientation front (`+0x1C`) and top (`+0x28`) into the
  same host globals `IDirectSound_SetPosition`/`SetOrientation` maintain, which the
  mixer's 3D `Levels` math reads. Velocity and the distance/rolloff/doppler factors
  are not stored, exactly as in the existing single setters (`SetVelocity` and the
  factor setters are success no-ops). This is real data the title reads back
  through audible panning/attenuation, so it is kept; the `apply` flag needs no
  work because the host applies immediately, as the existing deferred-commit body
  already assumes. It is not a DSP effect and pretends to none.
- **`IDirectSoundBuffer_SetNotificationPositions(this, count, pNotify)`** — a
  `BUFFER(...)` body: stores the `(offset, event)` pairs (capped at 64) on the
  voice. The mixer (`Render`) tracks the byte cursor before/after each render pass
  and `SetEvent`s every notification whose offset the cursor passed, wrap included,
  using the same `FromHandle`/`SetEvent` pair stream completion already uses.
  `DSBPN_OFFSETSTOP` (`0xFFFFFFFF`) entries are stored but never fired — the game
  never uses them [INFERENCE: only offsets 0 and `0x1F800` occur at its two call
  sites]. A plain success no-op was rejected here: the game's refill counter only
  advances when these events fire, so silence would stall its streaming audio.
  Offsets are compared against the play-region byte cursor (`play_start` is 0 for
  every buffer this title makes; it never calls `SetPlayRegion`).

## 6. Name registration: which list each emitted name is on

| Name | In `config/game.toml` before | Added to `sdk/xdk/3925/signatures.toml` | Address in `config/found-dsound-3925.toml` |
|---|---|---|---|
| `IDirectSound_GetCaps` | no row | **yes**: `{sec = "DSOUND", ret = 8, regs = []}` | `0x207253` |
| `IDirectSound_SetAllParameters` | no row | **yes**: `{sec = "DSOUND", ret = 12, regs = []}` | `0x207296` |
| `IDirectSoundBuffer_SetNotificationPositions` | no row | **yes**: `{sec = "DSOUND", ret = 12, regs = []}` | `0x2079CB` |

The signature rows carry no `len`, `k` or `xtlid` (no byte signature exists for
3925), the documented hand-added form already used by `D3DDevice_Present`,
`D3DDevice_GetBackBuffer` and `D3DDevice_GetDepthStencilSurface` in the same file;
the byte matcher skips them and the merged config supplies the address. `ret` is
the wrapper's pop size in bytes (8/12/12), i.e. 2/3/3 stack arguments including
`this`, matching the `HLE_STDCALL` argument counts. `config/found-dsound-3925.toml`
has **no `staged` marker**: every name in it already has its host body in
`sdk/runtime/xdk/dsound.cpp`, so merging it cannot bind a name to nothing. It is
safe for the orchestrator to merge into `config/game.toml` as-is.
(`IDirectSound_DownloadEffectsImage = 0x20726F`, the fourth section-6 name, is
already a `config/game.toml` row with an existing host body; it is not re-emitted.)

## 7. Entries the game does not call (need nothing)

- **Wrappers present but uncalled: none.** All 23 interface wrappers and all three
  free-function entry points are game-called (section 2).
- **Names with no code in this image** (stay `0` in the config; the guest can never
  call them, so a missing body is unreachable). From `docs/dsound-symbols.md`
  section 5, unchanged by this work: the 23 `CDirectSoundStream_*` /
  `IDirectSoundStream_SetEG` / `DirectSoundCreateStream` names (class not linked:
  no vtable, no factory, no allocation), `DirectSoundCreateBuffer`,
  `XAudioDownloadEffectsImage` (the title uses `DownloadEffectsImage` with a static
  array instead), and the wrapper-less `IDirectSound_*`/`IDirectSoundBuffer_*`
  names `GetSpeakerConfig`, `EnableHeadphones`, `SetEffectData`,
  `SetMixBinHeadroom`, `SetDopplerFactor`, `SetRolloffFactor`, listener
  `SetPosition`/`SetVelocity`/`SetOrientation`, buffer `SetFilter`, `SetHeadroom`,
  `SetMixBinVolumes`, `SetMixBins`, `SetLoopRegion`, `SetI3DL2Source`. Most of
  these already have host bodies for other titles; on this title they are dead
  either way and need nothing.
- Effects/DSP behaviour remains deliberately unreproduced, as elsewhere in
  `dsound.cpp`: nothing above changes that.
