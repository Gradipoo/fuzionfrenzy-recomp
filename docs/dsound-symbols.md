# DSOUND symbols in Fuzion Frenzy (XDK 3925)

Target image: `out/ff.exe`, DSOUND section `0x206980-0x221840`.
Reference image: `out/sims5558.exe` (XDK 5558, XDK names already applied).
Target rows: the 63 rows of `docs/symbol-targets.json` with `sec = "DSOUND"`.

**Result: 23 of 63 proven.** All 23 are in `config/found-dsound.toml`.
The other 40 have **no code in this image**. The `CDirectSoundStream` class is not linked into the XBE, and neither is any DSOUND API the game never calls. None of the 40 was attached to a near-miss.

## 1. Facts about the 3925 DSOUND section used below

- **Extent.** Code occupies `0x206980-0x20EA7D`: 343 IDA functions plus 6 unreferenced fragments (`0x206AB7`, `0x2078F5`, `0x20793E`, `0x208BB3`, `0x208F0F`, `0x20E695`). The fragments are shared tails and callbacks; none is an API wrapper. `0x20EA80-0x221840` is data: formats, the silence buffer, HRTF tables, the DSOUND lock and globals. A raw scan of every `E8`/`E9 rel32` in the image finds no branch into DSOUND past `0x20EA7D`.
- **Lock.** Every API method is bracketed by `0x2069AA`, which calls `RtlEnterCriticalSection(0x22111C)` unless IRQL is already raised. The matching unlock goes through import thunk `0x2533EC`, xboxkrnl ordinal 294 `RtlLeaveCriticalSection`. 5558 uses the same bracket, `_sub_0028B973` and `dword_2E1CD4`.
- **Interface handles.** An `IDirectSound*` handle is `CDirectSound + 0x8`; an `IDirectSoundBuffer*` handle is `CDirectSoundBuffer + 0x1C`. DirectSoundCreate (`0x207E35`) returns `this+8`, and CDirectSound::CreateSoundBuffer (`0x207C3D`) returns `this+0x1C`. The 5558 wrappers use the same -8 and -0x1C adjusts.
- **CMcpxAPU** is `[CDirectSound+0xC]`: 0xE78 bytes, constructor `0x2088D2`. Its constructor builds a **DS3DLISTENER at +0x144** and a **DSI3DL2LISTENER at +0x184**. These are the DS3D default values, so the field offsets below are ABI facts.

  | APU offset | Constructor writes | DS3DLISTENER / DSI3DL2LISTENER field |
  |---|---|---|
  | +0x144 | `0x40` | `dwSize` |
  | +0x148 / +0x154 | 0 | `vPosition` / `vVelocity` |
  | +0x160 | (0,0,1) | `vOrientFront` |
  | +0x16C | (0,1,0) | `vOrientTop` |
  | **+0x178** / +0x17C / +0x180 | 1.0 | **`flDistanceFactor`** / `flRolloffFactor` / `flDopplerFactor` |
  | +0x184 | -10000 | `lRoom`, first field of the I3DL2 block |
  | +0x1B0 | 5000.0 | `flHFReference`, last field of the I3DL2 block |

- **CMcpxVoiceClient** has base constructor `0x20A726` and vtable `0x257C48`. It is reached as `[CDirectSoundVoice+0x14]`, which is the same pointer as `[CDirectSoundBuffer+0x20]`. It holds a **DS3DBUFFER at +0x1C**. Again the constructor writes exactly the DS3D defaults:

  | Voice offset | Constructor writes | DS3DBUFFER field |
  |---|---|---|
  | +0x1C | `0x40` | `dwSize` |
  | **+0x20** | 0 | **`vPosition`** |
  | **+0x2C** | 0 | **`vVelocity`** |
  | +0x38 / +0x3C | 360 | inside / outside cone angle |
  | +0x40 | (0,0,1) | cone orientation |
  | +0x4C | 0 | cone outside volume |
  | **+0x50** | 1.0 | **`flMinDistance`** |
  | **+0x54** | `0x4E6E6B28` (1e9) | **`flMaxDistance`** |
  | +0x58 | 0 | `dwMode` |

- **MCPX APU front-end PIO** is the window at `0xFE820000`, used through these offsets:

  | Offset | Method |
  |---|---|
  | +0x10 | free-slot count |
  | +0x2F8 | SET_CURRENT_VOICE |
  | +0x360 / +0x364 / +0x368 | SET_VOICE_TAR_VOLA / VOLB / VOLC |
  | +0x37C | SET_VOICE_TAR_PITCH |
  | +0x3D8 | SET_VOICE_BUF_CBO |

  These are hardware method numbers, so they do not move between XDK builds.
- **The game calls exactly 27 DSOUND API entry points.** This list comes from a raw scan of every `call`/`jmp rel32` and every absolute pointer in the whole image. It contains the 23 names proven here plus four API wrappers that are not in the target list (section 6).

## 2. Vtable slot order: sims5558.exe vs ff.exe

### 2a. CDirectSoundStream, the requested family

In 5558 the constructor `0x29048D` stores vtable `0x2F9038` at `+0x0` and `0x2F9028` at `+0x4`. The destructor `0x28CD7E` stores the same pair. This is the two-vtable layout the assignment describes for 5849; no 5849 image was available to check it here.

| slot | +off | 5558 `0x2F9038` (object +0x0) | ret | ff.exe |
|---|---|---|---|---|
| 0 | 0x00 | `0x28CDBC` CDirectSoundStream_AddRef | 4 | **no vtable exists** |
| 1 | 0x04 | `0x28CE03` CDirectSoundStream_Release | 4 | none |
| 2 | 0x08 | `0x28CE51` CDirectSoundStream_GetInfo | 8 | none |
| 3 | 0x0C | `0x28CF50` CDirectSoundStream_GetStatus | 8 | none |
| 4 | 0x10 | `0x28CFA1` CDirectSoundStream_Process | 12 | none |
| 5 | 0x14 | `0x28CEB8` CDirectSoundStream_Discontinuity | 4 | none |
| 6 | 0x18 | `0x28CF05` CDirectSoundStream_Flush | 4 | none |

| slot | 5558 `0x2F9028` (object +0x4) | ff.exe |
|---|---|---|
| 0 | `0x231947`: `sub ecx,4; jmp` to the scalar-deleting destructor `0x28F1AA` | none |
| 1 | `0x28D67A`: `sub [esp+4],4; jmp CDirectSoundStream_AddRef` | none |
| 2 | `0x28D684`: `sub [esp+4],4; jmp CDirectSoundStream_Release` | none |
| 3 | `0x28B7D4`: CRefCount final release, shared by every 5558 DSOUND class | none |

Only 7 of the 21 `CDirectSoundStream_*` target names are vtable slots, even in 5558. A dword search of the 5558 image finds no data reference to the other 14:

- Pause
- FlushEx
- SetVolume
- SetFilter
- SetHeadroom
- SetMixBinVolumes
- SetFrequency
- SetMixBins
- SetMaxDistance
- SetMinDistance
- SetPosition
- SetVelocity
- SetI3DL2Source
- SetFormat

Each of these is reached only through a 5-byte `IDirectSoundStream_*` `jmp` thunk, for example `0x28E17E` to SetVolume, `0x28E1A1` to Pause and `0x2907CE` to SetFormat.

### 2b. The refcounted classes present in both images

"sdtor" below means the MSVC scalar-deleting destructor: `test byte [esp+4],1`, then free.

| class | 5558 vtable: slots | ff.exe vtable: slots |
|---|---|---|
| CDirectSound | `0x2F8FE8`: [0 sdtor `0x28E8C6`, 1 AddRef `0x28BE28`, 2 Release `0x290148`, 3 final-release `0x28B7D4`] | `0x257BDC`: [0 sdtor `0x2079EB`, 1 AddRef `0x20772C` (jmp `0x206ABE`), 2 Release `0x207A0F`] |
| CDirectSoundBuffer | `0x2F9018`: [0 sdtor `0x28F10F`, 1 AddRef `0x28C82B`, 2 Release `0x28C872`, 3 `0x28B7D4`] | `0x257BF4`: [0 sdtor `0x207B59`, 1 AddRef `0x20772C`, 2 Release `0x207731` (jmp `0x206AE3`)] |
| CDirectSoundStream | `0x2F9038` + `0x2F9028` (section 2a) | **none** |

The 3925 refcount base has 3 slots (dtor, AddRef, Release); the 5558 base has 4. **Release is slot 2 (`call [vtbl+8]`) in both images**, and slot 2 is what `IDirectSound_Release` and `IDirectSoundBuffer_Release` call.

Both CDirectSound::Release implementations special-case the count at which only DSOUND's own silence buffers, stored from `[this+0x18]`, still hold references. Every CDirectSoundVoice AddRefs its CDirectSound. At that count Release frees those buffers:

- 3925 `0x207A0F`: count 1, one buffer.
- 5558 `0x290148`: count 5, four buffers, each stopped first.

### 2c. Complete inventory of ff.exe DSOUND vtables

Two scans establish completeness:

- Every `mov [reg+disp], offset <.rdata/.data>` in every DSOUND function stores one of these 14 vtables and nothing else.
- Every 4-byte-aligned run of two or more dwords pointing at DSOUND **code** belongs to this table, which sits in `.rdata` `0x257BB8-0x257CDC`. The scan covered the whole image. Its other hits are not vtables: they point into DSOUND's data area or into the middle of instructions. They are coincidental table values in WMADEC and `.data`, plus the self-referencing wait-list head of the DSOUND lock at `0x221124`.

The class names in the right-hand column are descriptive labels taken from the code. They are not symbol claims.

| vtable | slots (ret) | stored by (object size) | role |
|---|---|---|---|
| `0x257BB8` | `0x2074CE` sdtor(4), `0x206ABE` AddRef(4), `0x206AE3` Release(4) | `0x206AA5`, every derived dtor | refcount base |
| `0x257BC4` | `0x207CBD`, AddRef, Release | `0x207A4C` | voice-settings base |
| `0x257BD0` | `0x2074CE`, AddRef, Release | ctor `0x207490` (0x14) | CDirectSound helper: speaker config, effect-image indices |
| `0x257BDC` | `0x2079EB`, `0x20772C`, `0x207A0F` | ctor `0x2074F3` (0x1C) | CDirectSound |
| `0x257BE8` | `0x207AB0`, AddRef, Release | ctor `0x207A85` | CDirectSoundVoice |
| `0x257BF4` | `0x207B59`, `0x20772C`, `0x207731` | ctor `0x207B41` (0x30) | CDirectSoundBuffer |
| `0x257C00` | `0x207CE1`, AddRef, Release | `0x207B7D` (0x5C) | buffer settings |
| `0x257C0C` | `0x34711` (.text adjustor `sub ecx,8; jmp 0x208D02`) | `0x2088D2`, at object +8 | CMcpxAPU secondary base |
| `0x257C10` | `0x208D02`, AddRef, Release | `0x2088D2` (0xE78) | CMcpxAPU |
| `0x257C48` | `0x20AF01`, AddRef, Release, `0x20AFB9`(0), `0x20AD15`(8) | `0x20A726` | CMcpxVoiceClient |
| `0x257C90` | `0x20C875`, AddRef, Release, `0x20AFB9`(0), `0x20AD15`(8), `0x20C0C1`(0), `0x20C834`(8) | `0x20BE1B` (0x230) | CMcpxBuffer |
| `0x257CB0` | `0x20D781` | `0x20CBB4`, `0x20D655` | heap base |
| `0x257CB4` | `0x20D863`, `0x20D8D2`, `0x20D95E`, `0x20D9FE`, `0x20DA65` | `0x20D7A5`, `0x20D846` | heap allocator |
| `0x257CC8` | `0x20D8A8`, `0x20DA94`, `0x20D95E`, `0x20DB11`, `0x20DA65`, `0x20DA99` | `0x20D83B`, `0x20D896`, `0x20D8A8` | heap allocator |

None of these can be the stream vtable:

- Every one of them starts with a scalar-deleting destructor (`test byte [esp+4],1` then free). The XMediaObject table starts with AddRef and has GetInfo, GetStatus and Process (ret 8/8/12) in slots 2-4.
- The only 7-slot table, `0x257C90`, has rets 4,4,4,0,8,0,8. Its single constructor call is from CMcpxAPU::CreateBuffer (`0x208AF6`), which is called only from CDirectSoundBuffer init (`0x207B7D`).
- The only DSOUND constructor that stores two vtables is CMcpxAPU's (`0x257C10` at +0, `0x257C0C` at +8), not a stream's.

**Conclusion: ff.exe has no CDirectSoundStream vtable, so no stream slot order can be read off it.** Section 5 gives the absence proof.

## 3. Neighbour order (corroboration only)

| ff.exe (3925) | name | sims5558.exe |
|---|---|---|
| `0x206A55` | IDirectSound_Release | `0x28BAE1` |
| `0x206A6B` | IDirectSoundBuffer_Unlock | `0x28BAF7` |
| `0x206A70` | IDirectSoundBuffer_Release | `0x28BB17` |
| `0x206A86` | DirectSoundUseFullHRTF | `0x28BB69` |
| `0x2072B6` | IDirectSound_SetDistanceFactor | `0x2912F4` |
| `0x2072DA` | IDirectSound_CommitDeferredSettings | `0x290CAF` |
| `0x2072F2` | IDirectSound_SetI3DL2Listener | `0x291414` |
| `0x207312` | IDirectSoundBuffer_SetVolume | `0x28DEDA` |
| `0x20732E` | IDirectSoundBuffer_SetMaxDistance | `0x28FE83` |
| `0x207352` | IDirectSoundBuffer_SetMinDistance | `0x28FEA7` |
| `0x207376` | IDirectSoundBuffer_SetPosition | `0x28FEEB` |
| `0x2073AB` | IDirectSoundBuffer_SetVelocity | `0x28FF20` |
| `0x2073E0` | IDirectSoundBuffer_GetStatus | `0x28E082` |
| `0x2073FC` | IDirectSoundBuffer_GetCurrentPosition | `0x28E09E` |
| `0x20741C` | IDirectSoundBuffer_SetCurrentPosition | `0x28E0BE` |
| `0x207438` | IDirectSoundBuffer_Lock | `0x28E0DA` |
| `0x207468` | DirectSoundDoWork | `0x28E214` |
| `0x207953` | IDirectSoundBuffer_SetFrequency | `0x28FD96` |
| `0x20796F` | IDirectSoundBuffer_Play | `0x28DF9E` |
| `0x207993` | IDirectSoundBuffer_Stop | `0x28DFE6` |
| `0x2079AB` | IDirectSoundBuffer_SetBufferData | `0x2907AE` |
| `0x207D05` | IDirectSound_CreateSoundBuffer | `0x29128C` |
| `0x207E35` | DirectSoundCreate | `0x2915DA` |

These runs keep their relative order in both builds:

- Release < Unlock < Release < UseFullHRTF (the stub file at the start of the library)
- MaxDistance < MinDistance < Position < Velocity
- GetStatus < GetCurrentPosition < SetCurrentPosition < Lock < DoWork
- Play < Stop
- SetFrequency < SetBufferData
- CreateSoundBuffer < DirectSoundCreate

The two builds interleave those runs differently. In 3925 each block of C wrappers sits right after the C++ implementations it calls. The block `0x207253-0x207468` follows the implementations at `0x206B38-0x207071` in nearly the same order. The differences are that Commit and SetI3DL2Listener are swapped, SetCurrentPosition and Lock are swapped, and DoWork comes last. So neighbour order is never the deciding evidence here.

## 4. Per-name evidence (23 proven)

In this section:

- "**twin**" means the 3925 body is **byte-identical to the named 5558 function, except for the `call rel32` displacement**. This was checked byte-by-byte for every name marked that way.
- "ret" is the `retn N` immediate. It equals the `docs/symbol-targets.json` value for all 23 names.
- `regs` is `[]` for every row, and every body reads its parameters from the stack only.

Identical wrappers do not distinguish names with the same shape. For example SetVolume, GetStatus, SetCurrentPosition and SetFrequency are all 28-byte wrappers with ret 8. **The callee chain decides each name**, and the decision is shown below.

### IDirectSound_Release = 0x206A55
- ret 4. 22 bytes (5558: 22). Twin of `0x28BAE1`.
- The body is `lea ecx,[h-8]` then `call [vtbl+8]`, i.e. slot 2 of CDirectSound vtable `0x257BDC`, which is Release `0x207A0F`. AddRef would be `[vtbl+4]`.
- The game shutdown loop at `0x55530` calls it on the IDirectSound handle until it returns 0, which matches Release returning the new reference count.
- Confidence: **high**.

### IDirectSoundBuffer_Unlock = 0x206A6B
- ret 20. 5 bytes (5558: 5). Twin of `0x28BAF7`: `xor eax,eax; retn 14h`.
- No other DSOUND function starts with that body.
- The game pairs it with Lock:
  - `0x69EC2` calls Lock(buf, off, n, &p1, &n1, &p2, &n2, 0) and fills the locked memory with silence. `0x69F38` then calls Unlock(buf, p1, n1, p2, n2).
  - `0x6A190` (in `0x6A150`) passes back the same object fields `+0x88..+0x94` that the Lock at `0x6A0DF` (in `0x6A0B0`) filled in.
- Confidence: **high**.

### IDirectSoundBuffer_Release = 0x206A70
- ret 4. 22 bytes (5558: 22). Twin of `0x28BB17`.
- The body is `lea ecx,[h-0x1C]` then `call [vtbl+8]`, i.e. slot 2 Release of CDirectSoundBuffer vtable `0x257BF4`.
- The game calls it at `0x55862` to free the buffer when SetBufferData fails ("bPlaySample: Could not set data for sound buffer"). CDirectSound::Release uses it, through `0x207928`, to drop the silence buffer.
- Confidence: **high**.

### DirectSoundUseFullHRTF = 0x206A86
- ret 0 (plain `retn`), no parameters. 31 bytes (5558: 31). The body is **not** a twin.
- 3925 takes the DSOUND lock and stores `0x20B291` into `0x22180C`, then unlocks. 5558 takes the same lock and calls `0x293D0A`, which installs 10 HRTF routine pointers.
- `0x22180C` is 0 in the image. Its only writer is this function. Its only reader is `0x209673`, the HRTF-coefficient thunk called from the voice 3D update `0x20A239`.
- **Full, not Light.** `0x20B291` is the filter-pair lookup of the **full** HRTF algorithm. It is the same code as the 5558 routine that only DirectSoundUseFullHRTF installs: `0x293932`, slot `dword_2B1864`, set by `0x293D0A`.
  - Both quantize elevation with `idiv 6`, `idiv 0Ch` and `idiv 3`, using thresholds 90/60/30 and the 180 mirror.
  - Both bound the grid at 61 (`0x3D`) x 31 (`0x1F`) and index a word table by `e*31+a`: 3925 `0x220200`, 5558 `0x2AE940`.
  - Both read two 32-byte filter tables 0x20 apart: 3925 `0x20EC40`/`0x20EC60`, 5558 `0x29D380`/`0x29D3A0`.
  - The only difference is the out-parameter shape: two pointers (`retn 14h`) in 3925 against a 2-element array (`retn 10h`) in 5558.
  - DirectSoundUseLightHRTF installs `0x293C90` into the same slot instead. That routine is an azimuth-only lookup at 3-degree steps, with no 61x31 grid and no 6/12 quantization.
  - 3925 has no second installer: `0x206A86` is the only writer of `0x22180C`.
- Position matches 5558: directly after IDirectSoundBuffer_Release in both images (section 3).
- Game usage: called once, from DirectSound init at `0x554DD`. The call comes right after "Downloaded default DSP program from static array OK" and right before SetI3DL2Listener.
- The ABI is `void(void)` and matches the target.
- Confidence: **high**.

### IDirectSound_SetDistanceFactor = 0x2072B6
- ret 12. 36 bytes (5558: 36). Twin of `0x2912F4`. This shape is shared with SetDoppler/RolloffFactor; the callee decides.
- Chain: `0x206C07` then CMcpxAPU `0x208C1A`. That function writes the float to **APU+0x178**, sets dirty bits 0x60, and commits through `0x20851A` when not deferred.
- APU+0x178 = DS3DLISTENER base +0x144 plus **0x34**, which is `offsetof(DS3DLISTENER, flDistanceFactor)` (section 1). SetAllParameters (`0x208BE9`) copies the user's 0x40-byte DS3DLISTENER to the same +0x144 base.
- Game cross-check: `0x559A0` stores its argument in `0x285A58` and passes it to this function. Later `0x559C0` writes `0x285A58` into the `flDistanceFactor` slot of the DS3DLISTENER it builds; rolloff 1.0 and doppler 8.0 are separate constants there.
- Confidence: **high**.

### IDirectSound_CommitDeferredSettings = 0x2072DA
- ret 4. 24 bytes (5558: 24). Twin of `0x290CAF`.
- Chain: `0x206C91` calls the APU 3D commit routine `0x20851A(0)` under the lock, then returns 0.
- `0x20851A` is the deferred-3D commit:
  1. It atomically takes the listener dirty mask at APU+0x1B4.
  2. It rebuilds the listener basis from front/top.
  3. If the I3DL2 bit is set, it re-uploads the DSI3DL2LISTENER (APU+0x184) to the DSP through `0x20BA33`.
  4. It calls every 3D voice's own commit `0x20AF25`. That is the routine the voice setters (`0x20B1A7`, `0x20B1C8`, `0x20B1E9`, `0x20B211`) call when DS3D_IMMEDIATE.
- The listener setters `0x208BE9`, `0x208C1A` and `0x208C3E` call `0x20851A(0)` on their immediate path. In 5558 every setter likewise calls CDirectSound::CommitDeferredSettings (`0x290883`).
- Game usage:
  - `0x55908`: called right after deferred (`dwApply=1`) SetPosition, SetVelocity, SetMinDistance and SetMaxDistance on a new 3D buffer.
  - `0x55ACE`: called right after a deferred SetAllParameters on the listener.
- Confidence: **high**.

### IDirectSound_SetI3DL2Listener = 0x2072F2
- ret 12. 32 bytes (5558: 32). Twin of `0x291414`.
- The callee `0x206C46` has the same guard as 5558 `0x291169`. If the effect image's I3DL2 reverb index (`[[this+8]+0xC]`, set from `DSEFFECTIMAGELOC` by DownloadEffectsImage) is -1, it returns `0x88780032` (DSERR_INVALIDCALL).
- Otherwise CMcpxAPU `0x208C3E` copies **12 dwords** (sizeof DSI3DL2LISTENER) to APU+0x184 and sets dirty bits 0x180.
- Game usage: at `0x554FA` the game passes `0x285A5C` with DS3D_IMMEDIATE. The 12 values there are -10000, -10000, 0.0, 1.0, 1.0, -10000, 0.0, -10000, 0.0, 0.0, 0.0, 5000.0. That is `DSI3DL2_ENVIRONMENT_PRESET_NOREVERB`.
- Confidence: **high**.

### IDirectSoundBuffer_SetVolume = 0x207312
- ret 8. 28 bytes (5558: 28). Twin of `0x28DEDA`.
- The callee `0x206D45` stores `settings[+0x18] = lVolume - settings[+0x1C]` (volume minus headroom), then calls `0x20AB7C`, which writes PIO SET_VOICE_TAR_VOLA/B/C (`0xFE820360/364/368`).
- This is the same logic as 5558 CDirectSoundVoice::SetVolume `0x28C505` (`[+0x1C] = lVolume - [+0x20]`). 5558 SetHeadroom `0x28C55A` has a different shape.
- The game calls it 10 times. One example is `0x5589C`, which passes a volume-table value right after SetBufferData.
- Confidence: **high**.

### IDirectSoundBuffer_SetMaxDistance = 0x20732E
- ret 12. 36 bytes (5558: 36). Twin of `0x28FE83`.
- Chain: `0x2071D1` then `0x206D7F` then `0x20B1A7`, which writes the float to **voice+0x54** = DS3DBUFFER+0x38 = `flMaxDistance`. That is the field the constructor initializes to 1e9. Dirty bit 4 routes to the volume update.
- Game: `0x558FC` passes the max-distance parameter with DS3D_DEFERRED.
- Confidence: **high**.

### IDirectSoundBuffer_SetMinDistance = 0x207352
- ret 12. 36 bytes (5558: 36). Twin of `0x28FEA7`.
- Chain: `0x2071E9` then `0x206DBE` then `0x20B1C8`, which writes **voice+0x50** = DS3DBUFFER+0x34 = `flMinDistance` (constructor default 1.0).
- Game: `0x558ED`.
- Confidence: **high**.

### IDirectSoundBuffer_SetPosition = 0x207376
- ret 20. 53 bytes (5558: 53). Twin of `0x28FEEB`.
- Chain: `0x207201` then `0x206DFD` then `0x20B1E9`, which copies 3 floats to **voice+0x20** = DS3DBUFFER+0x04 = `vPosition` and marks all dirty bits (0xFF).
- Game: `0x558CE` passes the 3-float vector argument of its `bPlaySample3D` routine.
- Confidence: **high**.

### IDirectSoundBuffer_SetVelocity = 0x2073AB
- ret 20. 53 bytes (5558: 53). Twin of `0x28FF20`.
- Chain: `0x20722A` then `0x206E4F` then `0x20B211`, which copies 3 floats to **voice+0x2C** = DS3DBUFFER+0x10 = `vVelocity`. It sets only dirty bit 0x40, which `0x20AF25` routes to the pitch (Doppler) update `0x20AC5D`.
- Game: `0x558DE` passes (0,0,0) right after SetPosition.
- Confidence: **high**.

### IDirectSoundBuffer_GetStatus = 0x2073E0
- ret 8. 28 bytes (5558: 28). Twin of `0x28E082`.
- Chain: `0x206F34` then CMcpxBuffer `0x20BF56`, which sets `*p = 0`, 1 (DSBSTATUS_PLAYING), or 5 (PLAYING|LOOPING).
- DSOUND's own notification code (`0x207862`) calls `0x206F34` and tests mask 4, DSBSTATUS_LOOPING.
- Game: `0x55741`, `0x6A302`.
- Confidence: **high**.

### IDirectSoundBuffer_GetCurrentPosition = 0x2073FC
- ret 12. 32 bytes (5558: 32). Twin of `0x28E09E`.
- Chain: `0x206F65` then `0x20BFA1`, which writes a play cursor and a write cursor. The cursors wrap on the loop region or on the buffer size; when the voice is idle both get the stored position `+0x228`.
- Lock (`0x206F9A`) calls `0x206F65(this, NULL, &write)` for DSBLOCK_FROMWRITECURSOR (flag 1).
- Confidence: **high**.

### IDirectSoundBuffer_SetCurrentPosition = 0x20741C
- ret 8. 28 bytes (5558: 28). Twin of `0x28E0BE`.
- Chain: `0x207040` then `0x20C74F`.
  - When the voice is active, it programs PIO **SET_VOICE_BUF_CBO** (`0xFE8203D8`), the current buffer offset.
  - Otherwise it stores the value at `+0x228`, which GetCurrentPosition returns for an idle voice.
- Game: `0x6A311`.
- Confidence: **high**.

### IDirectSoundBuffer_Lock = 0x207438
- ret 32. 48 bytes (5558: 48). Twin of `0x28E0DA`.
- The callee `0x206F9A` handles both lock flags:
  - `DSBLOCK_FROMWRITECURSOR` (1) uses GetCurrentPosition.
  - `DSBLOCK_ENTIREBUFFER` (2) uses the buffer size `settings[+0x4C]`.
- It returns the two lock regions relative to the buffer base `settings[+0x48]`, which are the same fields SetBufferData writes.
- Game: Lock, fill, Unlock at `0x69EC2`/`0x69F38` (see Unlock).
- Confidence: **high**.

### DirectSoundDoWork = 0x207468
- ret 0. 40 bytes (5558: 41). Same structure as `0x28E214` but not a twin: 3925 tests the lock result with `mov esi,eax` where 5558 uses `movzx esi,al`, one byte longer.
- Body: lock; if the global CDirectSound `0x221788` is set, call `0x206CBC` on it; unlock. `0x206CBC` walks the 0x200 voice slots at APU+0x664 through `0x208CB8`. 5558 does the same with `dword_2B1810` and `0x28C291`, which calls the APU DoWork.
- `0x221788` is set by the CDirectSound constructor `0x2074F3` (reached from DirectSoundCreate) and cleared by its destructor `0x207510`.
- Game: `0x41F2D`, inside the game's main-loop function `0x41EE0`, between other subsystem update calls. There is also a tail-jump at `0x1BB4F9`.
- Confidence: **high**.

### IDirectSoundBuffer_SetFrequency = 0x207953
- ret 8. 28 bytes (5558: 28). Twin of `0x28FD96`.
- The callee `0x207633` converts Hz to pitch:
  1. A value of 0 means "original rate"; it substitutes `wfx->nSamplesPerSec`.
  2. `0x20C9F8` maps Hz to pitch: 48000 (`0xBB80`) becomes 0, otherwise it uses the log formula.
  3. `0x206D0E` stores the pitch, and `0x20AC5D` writes PIO **SET_VOICE_TAR_PITCH** (`0xFE82037C`).
- Game: called 9 times. For example `0x558AD` passes its frequency argument, and skips the call when that argument is 0.
- Confidence: **high**.

### IDirectSoundBuffer_Play = 0x20796F
- ret 16. 36 bytes (5558: 36). Twin of `0x28DF9E`.
- Chain: `0x2077AB` calls PlayEx `0x206EA1(this, 0, 0, dwFlags)`, which calls CMcpxBuffer `0x20C91C(rt=0, dwFlags & DSBPLAY_LOOPING)`. The two reserved arguments are ignored.
- DSOUND's own init (`0x207E0A`) calls it on the silence buffer with flags 1 (DSBPLAY_LOOPING).
- Game: `0x55921` passes `(flags>>4)&1` as the looping flag.
- Confidence: **high**.

### IDirectSoundBuffer_Stop = 0x207993
- ret 4. 24 bytes (5558: 24). Twin of `0x28DFE6`.
- Chain: `0x2077DD` calls StopEx `0x206EDE(this, 0, 0, 0)`. With flags & 3 == 0 that goes to CMcpxBuffer stop `0x20C6EB`; flags & 3 would select DSBSTOPEX_ENVELOPE/RELEASEWAVEFORM handling instead.
- Game: called 8 times.
- Confidence: **high**.

### IDirectSoundBuffer_SetBufferData = 0x2079AB
- ret 12. 32 bytes (5558: 32). Twin of `0x2907AE`.
- The callee `0x207736` takes (this, pvData, dwBytes):
  1. If the values differ from `settings[+0x48]`/`[+0x4C]`, `0x20C302` detaches the old data from the hardware voice. It goes through `0x20BF04`/`0x20C0FE`, unlocks the old pages with MmLockUnlockBufferPages, and resets the stored position `+0x228` to 0.
  2. `0x207676` stores the pointer and size and resets the loop region (`[+0x50]=0`, `[+0x54]=size`).
  3. `0x20C908` maps the new data into the voice through `0x20C328`.
- DSOUND's own init (`0x207DF6`) calls it with the 0x40-byte silence block `0x20EAB0`.
- Game: `0x55854` calls it, followed by the error string "bPlaySample: Could not set data for sound buffer '%s'".
- Confidence: **high**.

### IDirectSound_CreateSoundBuffer = 0x207D05
- ret 16. 36 bytes (5558: 36). Twin of `0x29128C`.
- The callee `0x207C3D` allocates 0x30 bytes, constructs CDirectSoundBuffer (`0x207B41`, vtable `0x257BF4`) and initialises it from the DSBUFFERDESC (`0x207B7D`). It returns `buffer+0x1C` through `ppBuffer`.
- Game: `0x55818` calls it, followed by the error string "bPlaySample: Could not create sound buffer '%s'".
- Confidence: **high**.

### DirectSoundCreate = 0x207E35
- ret 12. 158 bytes (5558: 71). The body is **not** a twin: 3925 inlines what 5558 factored into `0x29156D`.
- The contract matches 5558:
  - It ignores argument 1 (`pguid`) and argument 3 (`pUnkOuter`).
  - It writes `*arg2 = CDirectSound + 8`, i.e. `ppDS`.
  - It is a singleton through global `0x221788`: if that is set, it AddRefs and returns the existing object. Otherwise it allocates 0x1C bytes, runs constructor `0x2074F3` and initialises through `0x207D29`.
- Game: `0x55456` calls it as `DirectSoundCreate(0, &g_pDS, 0)`, followed by the error string "bInitSpecificWave: Could not create DirectSound object". `0x6A68C` also stores the function's address as a function pointer.
- Confidence: **high**.

## 5. Not proven (40 names)

There is no code for any of these names in this image, so no address can be correct. They stay `0`. The guest never calls them, so nothing is lost.

### 5a. CDirectSoundStream class not linked (23 names)

Names:

- `CDirectSoundStream_AddRef`, `_Release`, `_GetInfo`, `_GetStatus`, `_Process`, `_Discontinuity`, `_Flush`. These are the 7 vtable slots in 5558.
- `CDirectSoundStream_Pause`, `_FlushEx`, `_SetVolume`, `_SetFilter`, `_SetHeadroom`, `_SetMixBinVolumes`, `_SetFrequency`, `_SetMixBins`, `_SetMaxDistance`, `_SetMinDistance`, `_SetPosition`, `_SetVelocity`, `_SetI3DL2Source`, `_SetFormat`. These are the 14 thunk targets in 5558.
- `IDirectSoundStream_SetEG`, the 5558 `jmp` thunk to CDirectSoundStream's SetEG.
- `DirectSoundCreateStream`.

Reasons:

1. **No vtable.** Section 2c is a complete inventory, and none of its entries has the XMediaObject layout or the 5558 stream secondary layout. With MSVC the vtable is emitted wherever the constructor is, so a linked stream class would need one.
2. **No factory.** CDirectSound in 3925 has exactly one object factory, CreateSoundBuffer `0x207C3D`. Its only callers are `0x207D21` (IDirectSound_CreateSoundBuffer) and `0x207DE1` (DSOUND init).
3. **No stream-sized allocation.** DSOUND has 16 allocation sites through `0x20CA6F` and all are accounted for: buffer settings 0x5C, buffer 0x30, helper 0x14, APU 0xE78, CDirectSound 0x1C, CMcpxBuffer 0x230, plus heap and notification arrays.
4. **The game creates no stream.** None of the game's 27 entry points (section 1) creates or uses a stream.
5. **Stripped by the linker** (inferred mechanism). With `/OPT:REF`, an unreferenced class's vtable and its methods are discarded together. That explains why even the 7 slot names are absent.

### 5b. Other entry points the image does not contain

| name | reason |
|---|---|
| DirectSoundCreateBuffer | The 5558 body creates the global CDirectSound, calls CreateSoundBuffer, then Releases. In ff.exe CreateSoundBuffer `0x207C3D` has only the two callers above, so no such function is linked. |
| XAudioDownloadEffectsImage | The 5558 body loads the image from a file or XBE section (CreateFile/GetFileSize/ReadFile or the section-load path, pool tag `'DSda'`) before calling DownloadEffectsImage. 3925 DSOUND calls no file or section API. Its outward calls are 29 xboxkrnl imports by ordinal, none of them a file or section API: no IoCreateFile (66), NtCreateFile (190), NtOpenFile (202), NtReadFile (219) or XeLoadSection (327). They cover pool, contiguous memory, DPC, timer, interrupt, NtSetEvent, critical section, IRQL, FPU state, MmLockUnlockBufferPages and MmGetPhysicalAddress. The only other outward calls go to four `.text` routines; the roles in parentheses are inferred from their code. They are `0x30A4D` (XGetAudioFlags: ExQueryNonVolatileSetting(9) plus AV-pack check), `0x11700`/`0x11720` (the game's tagged alloc/free) and `0x1DE998` (CRT float-to-int64). The game embeds the DSP image in `.data` (`0x285A98`, 0x60F8 bytes) and calls IDirectSound_DownloadEffectsImage `0x20726F` itself. |

### 5c. IDirectSound_* wrappers not present

The complete inventory of DSOUND functions with the `this-8` adjust has 8 entries:

| Address | Wrapper |
|---|---|
| `0x206A55` | Release |
| `0x207253` | GetCaps |
| `0x20726F` | DownloadEffectsImage |
| `0x207296` | SetAllParameters |
| `0x2072B6` | SetDistanceFactor |
| `0x2072DA` | CommitDeferredSettings |
| `0x2072F2` | SetI3DL2Listener |
| `0x207D05` | CreateSoundBuffer |

That inventory comes from a byte-pattern scan of all 343 functions for `add eax,-8` and `lea ecx,[eax-8]`.

| name | ret | reason |
|---|---|---|
| IDirectSound_GetSpeakerConfig | 8 | The only ret-8 IDirectSound wrapper, `0x207253`, writes **four** DWORDs. Those are DSCAPS: free 2D voices from APU+0x114, free 3D voices from +0x116 (the APU constructor initialises them to 0xC0 = 192 and 0x40 = 64, the MCPX 2D/3D voice split), free SGEs from +0x134, and pool plus contiguous bytes allocated. That makes it GetCaps; GetSpeakerConfig writes one DWORD. No candidate is left. |
| IDirectSound_EnableHeadphones | 8 | Same: the only ret-8 IDirectSound wrapper is GetCaps, so no candidate is left. |
| IDirectSound_SetEffectData | 24 | No IDirectSound wrapper has ret 24. |
| IDirectSound_SetMixBinHeadroom | 12 | The three ret-12 wrappers are SetAllParameters (0x40-byte DS3DLISTENER copy), SetDistanceFactor (float) and SetI3DL2Listener (0x30-byte copy). All take a DS3D_DEFERRED flag; none takes (mix-bin mask, headroom). |
| IDirectSound_SetDopplerFactor | 12 | The only float listener setter writes DS3DLISTENER+0x34, the distance factor. APU+0x180 (`flDopplerFactor`) is written only by the APU constructor (`0x208A10`) and by SetAllParameters' block copy. No setter exists. |
| IDirectSound_SetRolloffFactor | 12 | Same pattern: APU+0x17C (`flRolloffFactor`) is written only by the constructor (`0x208A08`) and by the block copy. |
| IDirectSound_SetPosition | 20 | The only ret-20 `this-8` wrapper, `0x20726F`, is a twin of 5558 IDirectSound_DownloadEffectsImage `0x28DD4C` and is called with the DSP image. It has no float marshalling, while 5558 SetPosition has `fld`/`fstp`. Listener position APU+0x148 is written only by the constructor and the block copy. |
| IDirectSound_SetVelocity | 20 | Same as SetPosition. The velocity field APU+0x154 is written only by the constructor and the block copy. |
| IDirectSound_SetOrientation | 32 | No wrapper has ret 32. The orientation fields APU+0x160/+0x16C are written only by the constructor and the block copy. The game sets the listener through SetAllParameters (`0x55AC2`). |

### 5d. IDirectSoundBuffer_* wrappers not present

The complete inventory of `this-0x1C` wrappers is Release plus 14 others. Each is identified in section 4 except SetNotificationPositions (section 6).

| name | ret | reason |
|---|---|---|
| IDirectSoundBuffer_SetFilter | 8 | The four ret-8 buffer wrappers are SetVolume, GetStatus, SetCurrentPosition and SetFrequency. Each is fixed by its callee (section 4), so none is left. |
| IDirectSoundBuffer_SetHeadroom | 8 | Same. SetVolume's callee also differs in shape from the 5558 SetHeadroom implementation. |
| IDirectSoundBuffer_SetMixBinVolumes | 8 | Same: no ret-8 wrapper is left. |
| IDirectSoundBuffer_SetMixBins | 8 | Same: no ret-8 wrapper is left. |
| IDirectSoundBuffer_SetLoopRegion | 12 | The five ret-12 buffer wrappers are SetMaxDistance, SetMinDistance, GetCurrentPosition, SetBufferData and SetNotificationPositions. All are identified, so none is left. |
| IDirectSoundBuffer_SetI3DL2Source | 12 | Same ret-12 inventory: none is left. |

## 6. Identified but not emitted (not in the target list)

These are proven by the same method but are not among the 63 names. They are therefore left out of `config/found-dsound.toml`; an entry without a host body could break the merge. They are renamed in the ff.exe IDB.

| Address | Name | Evidence |
|---|---|---|
| `0x207253` | IDirectSound_GetCaps | `this-8`, ret 8, fills DSCAPS. Called from 13 game sites. |
| `0x20726F` | IDirectSound_DownloadEffectsImage | Twin of 5558 `0x28DD4C`. The game passes the DSP image and `DSEFFECTIMAGELOC {0,1}`. |
| `0x207296` | IDirectSound_SetAllParameters | 16-dword DS3DLISTENER copy to APU+0x144. The game builds `dwSize=0x40` before calling it. |
| `0x2079CB` | IDirectSoundBuffer_SetNotificationPositions | (count, DSBPOSITIONNOTIFY[]) copied into 8-byte entries and bubble-sorted. Signalled with NtSetEvent. |
