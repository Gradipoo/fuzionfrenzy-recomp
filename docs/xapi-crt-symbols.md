# XAPI / XPP / CRT-math symbols in Fuzion Frenzy (XDK 3925)

Scope: the 37 rows of `docs/symbol-targets.json` whose `sec` is `XPP` (5), `.text` (15) or absent
(`(none)`, 17). Target image: `out/ff.exe` (flat PE of `default.xbe`, guest addresses 1:1). Named
reference: `out/sims5558.exe` (XDK 5558).

**Result: 17 of 37 proven** and written to `config/found-xapi-crt.toml`: 12 new names plus the 5 anchors
already in `config/game.toml` (crt_pow, crt_sqrt, XInputGetState, XInputSetState, XMountUtilityDrive),
each re-confirmed with independent evidence below. The other 20 are not emitted: 19 have no
counterpart in this image, and 1 (`DirectSoundUseLightHRTF`) cannot be separated from its sibling.
The `(none)` rows are not socket names. They are 3 D3D and 14 DSOUND names that have no 5849
signature. The socket/XNet/XOnline names in `config/game.toml` have no row in the JSON; they are covered
in their own section at the end.

## Method

- **Masked byte hash, reimplemented and validated.** `sdk/tools/symbols/main.cpp` computes a rolling FNV-64
  hash over code bytes (`h = h * 0x100000001B3 + m + 1`). It sweeps each code section linearly, follows
  calls, and masks two kinds of 4-byte field to `0x100`: displacements and immediates whose value lies in
  `0x10000-0x4000000`, and rel32 branch targets. I reimplemented it with capstone. The reimplementation
  reproduces the 5849 hashes of `crt_pow` and `crt_sqrt` in ff.exe, and of `crt_pow`, `crt_acos`,
  `crt_asin` and `_CIacos` in sims5558.exe. I then hashed every byte offset of every code section of
  ff.exe against each target's 5849 and 5558 `(len, hash)`. Callee trees were checked two levels deep,
  the way `CallsMatch` does.
- **Kernel thunk table.** It sits at `0x253360` with 111 ordinals, read with `tools/xbe_info.py`
  (`Xbe.imports`). The slots cited below:

  | slot | ordinal | export | | slot | ordinal | export |
  |---|---|---|---|---|---|---|
  | `0x253360` | 178 | MmPersistContiguousMemory | | `0x2533E0` | 67 | IoCreateSymbolicLink |
  | `0x253364` | 165 | MmAllocateContiguousMemory | | `0x2533EC` | 294 | RtlLeaveCriticalSection |
  | `0x253368` | 164 | LaunchDataPage | | `0x2533F0` | 277 | RtlEnterCriticalSection |
  | `0x25336C` | 171 | MmFreeContiguousMemory | | `0x253448` | 159 | KeWaitForSingleObject |
  | `0x253370` | 301 | RtlNtStatusToDosError | | `0x253494` | 17 | ExFreePool |
  | `0x253374` | 49 | HalReturnToFirmware | | `0x25349C` | 161 | KfLowerIrql |
  | `0x253378` | 187 | NtClose | | `0x2534B4` | 175 | MmLockUnlockBufferPages |
  | `0x25337C` | 215 | NtQuerySymbolicLinkObject | | `0x2534B8` | 173 | MmGetPhysicalAddress |
  | `0x253380` | 203 | NtOpenSymbolicLinkObject | | `0x2534D0` | 14 | ExAllocatePool |
  | `0x25338C` | 289 | RtlInitAnsiString | | `0x2534D4` | 129 | KeRaiseIrqlToDpcLevel |

- **XBE facts.** Library table: XAPILIB 3911, D3D8 3925, DSOUND 3925, XBOXKRNL 3911, LIBC 3911,
  XGRAPHC 3911. Sections: .text, D3D, DSOUND, XGRPH, WMADECXM, WMADEC, XPP, .rdata, .data, DOLBY,
  $$XTIMAGE.

## Summary

| Name | Address | Strongest evidence | Confidence |
|---|---|---|---|
| `_CIacos` | `0x1E05B0` | 5849 byte hash + callee is crt_acos | certain |
| `_CIasin` | `0x1E067C` | 5849 byte hash + callee is crt_asin | certain |
| `crt_acos` | `0x1E05CD` | unique 5849/5558 byte hash, `fpatan`, OP 0x0D, "acos" | certain |
| `crt_asin` | `0x1E0699` | unique 5849/5558 byte hash, `fpatan`, OP 0x0E, "asin" | certain |
| `crt_sin` | `0x1E1065` | unique 5849 byte hash, `fsin`, OP 0x1E, "sin" | certain |
| `crt_cos` | `0x1E1129` | unique 5849 byte hash, `fcos`, OP 0x12, "cos" | certain |
| `crt_pow` | `0x1DF6AC` | anchor; unique 5849/5558 byte hash, `fyl2x`, OP 0x1D, "pow" | certain |
| `crt_sqrt` | `0x1E11D9` | anchor; unique 5849 byte hash + callee tree, `fsqrt`, OP 5, "sqrt" | certain |
| `XInitDevices` | `0x24CA33` | 5558 body field for field (0xB4 struct, +0xA0/+0xA1/+0xA4), ret 8 | high |
| `XInputOpen` | `0x24D22D` | 5558 body statement for statement, ret 0x10, game opens gamepad with it | high |
| `XInputClose` | `0x24D2A2` | 5849 byte hash; callee is the XID close routine; ret 4 | high |
| `XInputGetState` | `0x24D2AE` | anchor; 5558 body, 0x48F, ret 8 | high |
| `XInputSetState` | `0x24D31F` | anchor; 5558 body, k 0x40/0x41/0xA3 exact, ret 8 | high |
| `XLaunchNewImageA` | `0x030603` | game prints "XLaunchNewImage returned %d" after calling it; ords 164/165/178/49 | high |
| `XMountUtilityDrive` | `0x030E25` | anchor; ord 67 on `\??\Z:`, k 0xFFFFFEEC x3, ret 4 | high |
| `D3DDevice_SetPalette` | `0x1F60F0` | only push of NV2A method `0x41B20` (SET_TEXTURE_PALETTE) in the image; 5558 body | high |
| `IDirectSound_DownloadEffectsImage` | `0x20726F` | 5558 wrapper hash; callee matches 5558 method; game logs "Downloaded default DSP program" | high |

## CRT math (LIBC 3911)

These are the x87 workers of the VC7-era CRT. A worker takes `x` in `st(0)`. A loader (`0x1E3988` for
`_CI` callers, `0x1E3945` for cdecl callers) sets `eax` to the high dword of `x` masked to the exponent,
or the whole high dword when the exponent is 0x7FF, and sets ZF in exactly that case. The worker reads
that `eax` before writing it, which is why the 5849 `regs` field is `["eax"]`. Each worker reports errors
with `mov edx, OP_<fn>` plus `lea ecx, "<fn>"`, then leaves through shared tails:

- `0x1E399E`: fast exit, `__fastflag`-like global at `0x315A94`.
- `0x1E39AB`: precision check.
- `0x1E38C3`: error path, which calls `0x1E6FE1`.

Helpers: `0x1E3915` loads the default control word `0x27F`, `0x1E392C` handles NaN.

Every function has the same three-part layout:

- `_CI<fn>` (20 bytes): `sub esp,0Ch; fst qword [esp]; call 0x1E3988; call crt_<fn>; add esp,0Ch; ret`.
  The `_CI` form takes its argument in `st(0)`; `fst` spills it for the loader `0x1E3988`.
- The cdecl entry `<fn>`: `lea edx,[esp+4]; call 0x1E3945`, which falls through into `crt_<fn>`.
  The entries are acos `0x1E05C4`, asin `0x1E0690`, sin `0x1E105C`, cos `0x1E1120` and sqrt `0x1E11D0`.
  None is a target. IDA's function starts at sin and cos are these entries, not the workers.
- `crt_<fn>`: the worker.

LIBC math code is in the same order as its `.data` name strings: pow, acos, asin, fmod, sin, cos, sqrt.
The strings are at `0x2B74D8`, `0x2B74E0`, `0x2B74E8`, `0x2B74F1`, `0x2B7550`, `0x2B7554` and `0x2B7558`.

### `_CIacos = 0x1E05B0`
- Masked hash over 0x14 bytes = `0x242CEA3CB8F35082`. This equals 5849 `_CIacos`, 5849 `_CIasin` and the
  5558-signed `_CIacos`. It hits 5 places: the acos, asin, sin, cos and sqrt wrappers at `0x1E05B0`,
  `0x1E067C`, `0x1E1048`, `0x1E110C` and `0x1E11BC`.
- The callee separates them. `0x1E05B0` calls `0x1E3988` (0x16 bytes, matches 5849 callee hash
  `0xC97C95373548D0C1`) and then `0x1E05CD`, which is `crt_acos` below and matches the 0xAE-byte callee
  hash. The 5849 `calls` tree matches in full.
- ret 0, regs []. These match 5849.

### `_CIasin = 0x1E067C`
- The same 20 bytes as `_CIacos`. Its second callee is `0x1E0699` (`crt_asin`). The 5849 `_CIasin` callee
  tree matches in full (`0xAE`, hash `0x67B152AC9353F8E6`).

### `crt_acos = 0x1E05CD` (0x1E05CD-0x1E067A, 0xAE bytes)
- Masked hash `0xECA88B2666FCEC33` equals 5849 and 5558-signed `crt_acos`. That hash occurs nowhere else
  in the image.
- In IDA, all 50 instructions are identical to `out/sims5558.exe` `crt_acos` `0x21EAF1` once addresses
  are masked. Branch offsets inside the body are identical too.
- Body: `fsqrt` at `0x1E05F2`, `fpatan` at `0x1E05F6`. That is `atan2(sqrt((1+x)(1-x)), x)`.
- `cmp eax, 3FF00000h` at `0x1E05E1` and `and ecx, 80000000h` at `0x1E0628` give exactly the 5849 `k`.
- `mov edx, 0Dh` (OP_ACOS) at `0x1E0605` and `0x1E0669`; `lea ecx, "acos"` at `0x1E060A` and `0x1E066E`.
- Callee tree matches 5849: `0x1E3915` 0x17, `0x1E399E` 0xD, `0x1E39AB` 0x2A (calling `0x1E38C3`),
  `0x1E392C` 0x19, `0x1E38C3` 0x3C (calling `0x1E6FE1` 0xDD).
- ret 0, regs [eax].

### `crt_asin = 0x1E0699` (0x1E0699-0x1E0746, 0xAE bytes)
- Masked hash `0x67B152AC9353F8E6` equals 5849 and 5558-signed `crt_asin`; it is unique in the image.
- Body: `fsqrt` at `0x1E06BE`, then `fpatan` at `0x1E06C0` with no `fxch`, giving
  `atan2(x, sqrt((1+x)(1-x)))`. The ±1 case loads ±pi/2 (`fld tbyte` plus `fchs`), where acos uses
  `fldpi`/`fldz`.
- `mov edx, 0Eh` (OP_ASIN) at `0x1E06CF` and `0x1E0735`; "asin" at `0x1E06D4` and `0x1E073A`.
  k `0x3FF00000`/`0x80000000`.
- Callee tree matches 5849. ret 0, regs [eax].

### `crt_sin = 0x1E1065` (0x1E1065-0x1E10F5, 0x91 bytes)
- Masked hash `0x2C375C152C50978F` equals 5849 `crt_sin` (5558 has no copy); it is unique in the image.
  The whole 5849 callee tree matches.
- Body: `fsin` at `0x1E107A`. If C2 is set, it reduces the argument with an `fprem1` loop at `0x1E10A7`,
  then runs `fsin` again at `0x1E10B1`.
- `mov edx, 1Eh` (OP_SIN) at `0x1E108F` and `0x1E10E4`; `lea ecx, "sin"` (`0x2B7550`) at `0x1E1094` and
  `0x1E10E9`.
- ret 0, regs [eax]; 5849 `k` is empty, which matches.

### `crt_cos = 0x1E1129` (0x1E1129-0x1E11B9, 0x91 bytes)
- Masked hash `0xCD444740A2B4593` equals 5849 `crt_cos`; it is unique in the image. The whole callee tree
  matches.
- Body: `fcos` at `0x1E113E`, `fprem1` reduction at `0x1E116B`, `fcos` again at `0x1E1175`.
- `mov edx, 12h` (OP_COS) at `0x1E1153` and `0x1E11A8`; "cos" (`0x2B7554`) at `0x1E1158` and `0x1E11AD`.
- ret 0, regs [eax].

### `crt_pow = 0x1DF6AC` (re-confirmed anchor)
- Masked hash over 0x1C5 bytes = `0x1FBDF1CEC7DD4478`. That equals 5849 and 5558-signed `crt_pow`, and is
  unique in the image.
- Body: `fyl2x` at `0x1DF6F9`; `lea ecx, "pow"` (`0x2B74D8`) plus `mov edx, 1Dh` (OP_POW) at
  `0x1DF714`/`0x1DF71A` and `0x1DF731`/`0x1DF737`.
- All 19 first-level callees match 5849. One second-level callee does not: `0x1E3A8C`, 0x65 bytes, below
  `0x1E3AED`. That callee is not part of this function's bytes.

### `crt_sqrt = 0x1E11D9` (re-confirmed anchor)
- Masked hash `0x1C63370E6C4EDE26` equals 5849; it is unique. The full callee tree matches.
- Body: `fsqrt` at `0x1E11F8`; `mov edx, 5` (OP_SQRT) at `0x1E1207`/`0x1E1264`; "sqrt" at
  `0x1E120C`/`0x1E1269`.
- It loads `eax` itself (`mov eax,[esp+arg_4]`), which is why the 5849 `regs` is `[]`.

## XPP (XAPILIB 3911, section 0x24C220-0x253360)

The order is the same in both images: XInitDevices, then XInputOpen, XInputClose, XInputGetState,
XInputSetState. In 5558 those are `0x2DA1B3`, `0x2DAC65`, `0x2DACBB`, `0x2DAE9F` and `0x2DAF12`; here
they are `0x24CA33`, `0x24D22D`, `0x24D2A2`, `0x24D2AE` and `0x24D31F`. 3911 keeps the device type of
an open handle as a byte index at handle `+0xA3`. The index points into a 24-byte-stride per-type table
at `0x24C388`. 5558 keeps a pointer at `+0xA3` instead.

### `XInitDevices = 0x24CA33` (0x24CA33-0x24CAC0)
5558 `XInitDevices` builds a `0xB4`-byte resource structure on its stack (`sub esp,0B4h`). 3911 does
exactly the same with heap memory:

- `ExAllocatePool(0xB4)`: ordinal 14 at `0x24CA3B`.
- `0x25070F(this, dwPreallocTypeCount, PreallocTypes)` fills the structure, like 5558 `0x2DCADF`.
- It loops over the 4-entry class-driver table `0x24C224-0x24C234`, calling `[entry+4](struct)`.
  The 5558 table is `0x2D9CF4-0x2D9D04`.
- It then calls `0x24C871(struct)`, then the 12-byte wrapper `0x24CC0B(&struct+0xA4)`, then
  `0x24C994(ecx=0x318D50, struct+0xA0 byte, struct+0xA1 byte)`.
- `ExFreePool` (ordinal 17) at `0x24CAA9`, then the byte flag `0x318E38` is cleared, then `0x24CC17()`
  is called, then `ret 8`.

The struct offsets are the evidence. The 5849 `k` is `0x5C, 0x5D, 0x60, 0x70`: the frame displacements
`[ebp+0x5C]`, `[ebp+0x5D]`, `[ebp+0x60]` with `ebp` = struct + 0x44 (`lea ebp,[esp-70h]`). Those are
struct+`0xA0`, `0xA1` and `0xA4`, the same three fields 3911 uses. The 5849 third callee is a 12-byte
wrapper whose hash `0xE51B784F4912F43` equals `0x24CC0B`. Its callee `0x24CD83` sizes and allocates the
USB host-controller memory (ordinals 165, 175, 173).

ret 8, regs [] match. The game calls it once, through the 5-byte thunk `0x24D379` (`jmp 0x24CA33`), from
`0x49D76`. The arguments are `(2, {{0x24C2BC, 4}, {0x24C238, 4}})`. Nothing else references `0x24CA33`.
The thunk has the same stdcall ABI. Naming the body is enough: `sdk/runtime/xdk/hle.h` replaces the
recompiled weak `sub_<addr>` of a `[symbols]` address with the host's strong definition. The thunk's
`jmp` is compiled as a tail call to `sub_24CA33`, so the game's call reaches the host implementation.

### `XInputOpen = 0x24D22D` (0x24D22D-0x24D2A1)
- It compares `DeviceType` with the three XID type records `0x24C2BC`, `0x24C2C8` and `0x24C2D4`, giving
  index 0, 1 or 2. 5558 calls a lookup (`0x2DAFD5`) for the same purpose.
- An unknown type pushes `0x57` (ERROR_INVALID_PARAMETER) to `0x31127`, returns NULL and does `ret 10h`.
  `0x31127` is SetLastError. Its body matches 5558 `0x100F6B` instruction for instruction, except that it
  stores the error at TLS block `+4` (5558: `+8`). It finds the block through TLS index `0x2BB130`.
- A NULL `pPollingParameters` takes the type's default from `[idx*24+0x24C394]`.
- The 5558 sequence `cmp [ebp+10h],1; mov edx,[ebp+0Ch]; jne; add edx,10h` appears here byte for byte:
  bottom slot means port + 16.
- It opens through `0x24DC96(&handle, poll)`, with ecx = index and edx = port. A NULL handle sets the
  last error.
- ret 0x10 matches 5849 `ret 16`.
- The game opens the gamepad with it: `push 0x24C2BC; call 0x24D22D` at `0x49A12` and `0x49B50`.

### `XInputClose = 0x24D2A2` (12 bytes)
- `mov ecx,[esp+4]; call 0x24DA0E; ret 4`. The masked hash is `0xE51B784F4912F43`, the 5849 `XInputClose`
  hash. The same 12-byte shape also occurs at `0x24CC0B`, so the callee decides between them.
- `0x24DA0E` is the XID close routine, statement for statement 5558 `0x2DBD74`:
  - `KeRaiseIrqlToDpcLevel` (129).
  - If the device is still attached, `or byte [esi+0A2h],1`, then build a close request
    (`[ebp-14h]=0`, `[ebp-12h]=4`, `[esi+9Eh]=&req`), then `call 0x24D8A2`.
  - `KfLowerIrql` (161), then `KeWaitForSingleObject` (159) on the request event with 4 zeros.
  - It bumps the type's free count and links the instance onto the free list `0x318CB8`.
- The other twin, `0x24CC0B`, calls the USB host-controller allocator. Only XInitDevices calls it, so it
  is the 12-byte call already accounted for above.
- The game calls `0x24D2A2` at `0x49AFF` and `0x49DCB`. It sits between XInputOpen and XInputGetState, as
  in 5558.

### `XInputGetState = 0x24D2AE` (re-confirmed anchor)
5558 `0x2DAE9F`, line for line:

- `KeRaiseIrqlToDpcLevel`.
- Wrong type (`[h+0A3h]==1`) returns `0x57`. A detached device returns `0x48F` (ERROR_DEVICE_NOT_CONNECTED,
  the 5849 `k`).
- It copies `dwPacketNumber` from `[h+8]`, then the report from `h+0x14` with `rep movsd/movsb`, using the
  per-type size byte.
- `KfLowerIrql`, then `ret 8`.

Game call: `0x49E2F`.

### `XInputSetState = 0x24D31F` (re-confirmed anchor)
5558 `0x2DAF12`:

- Wrong type returns `0x57`.
- `and byte [feedback+40h],0`, then `[feedback+41h]` = type report size + 2, then the send call
  `0x24DAB3`, then `ret 8`.

The constants `0x40`, `0x41` and `0xA3` are exactly the 5849 `k`. Game call: `0x4A1ED`.

### XPP device-type record: observed 3925 shape (layout.toml values, not written by me)
The shape matches 5849: three dwords, as `XGetDevices` and `XGetDeviceChanges` show. Neither is a target
row, so they are not emitted.

- `XGetDevices` = `0x24D37E`: raises IRQL, then:
  - `mov esi,[edx]` at `0x24D389`: Current.
  - `and dword [edx+4],0` at `0x24D38B`: clears Changed.
  - `mov [edx+8],esi` at `0x24D391`: Previous = Current.
  - Lowers IRQL, returns Current, `ret 4`.
- `XGetDeviceChanges` = `0x24D3A0`:
  - `cmp [esi+4],0` at `0x24D3A9` returns FALSE with both outputs zero when nothing changed.
  - Otherwise insertions = Current & ~Previous and removals = Previous & ~Current. Ports that were
    replaced (Changed & Previous & Current) are reported in both.
  - Changed = 0 at `0x24D3EA`, Previous = Current at `0x24D3EE`, `ret 0Ch`.
- The insertion/removal reporter `0x24D357(type, port, fInserted)` does `or [type+4],bit` at `0x24D364`,
  then `or [type],bit` or `and [type],~bit` at `0x24D36E`/`0x24D374`. It is called with fInserted 0 at
  `0x24F045` and with 1 at `0x24F41C`.

```toml
XPP_Current  = 0x0  # XGetDevices 0x24D389 returns it; reporter 0x24D36E/0x24D374 sets/clears the port bit
XPP_Changed  = 0x4  # XGetDevices 0x24D38B clears it, XGetDeviceChanges 0x24D3A9 tests it, reporter 0x24D364 sets it
XPP_Previous = 0x8  # XGetDevices 0x24D391 and XGetDeviceChanges 0x24D3EE copy Current into it
```

Data addresses for the `[data]` table. These are observations only; I do not write `[data]`.

- Gamepad type record (`XPP_GamepadType`) = `0x24C2BC`. The game passes it to XGetDevices (`0x499C2`),
  XGetDeviceChanges (`0x49AD3`), XInputOpen and the XInitDevices prealloc list. The XID pointer list at
  `0x24C2E0` holds it first, and the descriptor that follows at `0x24C2EC` (`82 03 FF FF`, count 3)
  lists the three XID records.
- `0x24C238` is the other type the game preallocates and polls: XGetDevices at `0x49A56`,
  XGetDeviceChanges at `0x49C1E`. Its descriptor at `0x24C248` reads `82 08 FF FF` with count 1; USB
  class 8 is mass storage. [INFERENCE] It is `XPP_MemoryUnitType`.

## XAPI misc (XAPILIB 3911, .text)

### `XLaunchNewImageA = 0x030603` (0x30603-0x30715)
- **The game names it.** At `0x184EF` the game calls `0x30603(path, 0x2D08A8)`. The path is either
  `"d:\XDemos\XDemos.xbe"` (`0x254F38`) or a runtime path buffer (`0x2D08B4`). It then formats
  `"\n\nXLaunchNewImage returned %d\n\n"` (`0x254F18`) with the return value.
  - Game `0x4FA49` calls it with `(NULL, NULL)` to go to the dashboard.
  - Library `0x33C67` (ret 0xC, 3 args) builds a zeroed 0xC00-byte LAUNCH_DATA with reason/parameters
    and calls `0x30603(NULL, &ld)`, the boot-to-dashboard path.
- **The body matches 5558** (its internal `0x102D2E`):
  - The NULL path pushes `pLaunchData != NULL ? 1 : -1` (`setne; lea [x+x-1]`) and the TitleID read from
    the certificate (`[0x10118]+8`).
  - `D:\` parsing: `cmp al,'D'` or `'d'`, then `':'`, then `'\'`.
  - `push 57h; pop eax` for an invalid path.
- **Kernel ordinals reached:**
  - Directly: NtOpenSymbolicLinkObject (203) and NtQuerySymbolicLinkObject (215) on `\??\D:`
    (`0x25783C`), with a 0x208 buffer, then NtClose (187). The default device is `\Device\Cdrom0`
    (`0x257844`).
  - Through `0x305BE`, which is XWriteTitleInfoAndReboot-shaped (ret 0x14):
    - `0x30480` writes the launch-data page: LaunchDataPage (164); MmAllocateContiguousMemory (165) for
      0x1000 bytes; MmPersistContiguousMemory (178); type and TitleID at +0/+4; `"<dev>;<path>"` at +8;
      0x300 dwords of LAUNCH_DATA at +0x400.
    - Then `HalReturnToFirmware(2)` (49, HalQuickRebootRoutine), with RtlNtStatusToDosError (301) on
      failure.
- ret 8 matches 5849 (`stdcall(LPCSTR, PLAUNCH_DATA)`). 3911 implements the API in one function, where
  5849 has a 0x42-byte wrapper.
- Order matches 5558: XLaunchNewImageA comes before XMountUtilityDrive (`0x102564` before `0x1035AF` in
  5558).

### `XMountUtilityDrive = 0x030E25` (re-confirmed anchor)
- `ret 4`.
- It formats `\Device\Harddisk0\Partition%d\` (`0x257940`) into `[ebp-114h]`. Its three uses of
  `[ebp-114h]` are the 5849 `k` `0xFFFFFEEC` x3.
- It initializes two ANSI strings via RtlInitAnsiString (289, `0x25338C`).
- It calls `0x33DDB` (NtOpenFile, NtFsControlFile, NtDeviceIoControlFile, NtWriteFile) when the drive
  must be (re)formatted, and `0x33794` to mount it.
- `IoCreateSymbolicLink(\??\Z:, ...)`: ordinal 67 at `0x30EE7`; the string is at `0x257938`.

## sec-less (`(none)`) rows that exist in this image

### `D3DDevice_SetPalette = 0x1F60F0` (0x1F60F0-0x1F616E)
- **Unique NV2A method.** `add esi, 0x41B20` after `shl esi, 6` at `0x1F6158` pushes
  `NV097_SET_TEXTURE_PALETTE(stage)` (method 0x1B20 + stage*64, count 1). No other instruction in the
  image uses `0x41B20` or a per-stage variant (`0x41B60`/`0x41BA0`/`0x41BE0`). The only other byte match,
  `0x21E70D`, lies inside a byte table in DSOUND's data.
- **Matches 5558 `D3DDevice_SetPalette` `0x2473A0` line for line.**
  - It reads the old `device->Palettes[stage]`, sets its Lock to the device time and drops a reference
    (`add 0FFF80000h; test 78FFFFh`), destroying it at zero (`0x1FC6A0`).
  - It stores the new palette and adds a reference through the proven anchor `D3D_InternalAddRef`
    `0x1FC780`.
  - It takes the size bits from `Common>>28`, gets push space via `0x1FB3B0(device)` (push >= threshold,
    then MakeSpace), and writes `{method, Data | size}`. Then `ret 8`, the 5558 `ret 8`.
- Game callers pass `(stage, palette)`, or `(stage, NULL)` to unbind: `0x3D1EB` (stage 0, `&tex+0x40`
  when its format tag is 5), `0x11F77A` (a loop clearing stages), `0x4155F`, `0x4178C`, `0x417F0`,
  `0x41878`.
- 3925 layout seen here, for the D3D layout work: `D3D__pDevice` at `0x206720`; palettes at device
  `+0xBB0` (5849: `0xF98`); time at `+0x1C` (5849: `+0x2C`).
- Neighbour order does not help in D3D. The 3925 object order differs from 5558: `D3D_InternalAddRef`
  comes before SetPalette in 5558 and after it here.

### `IDirectSound_DownloadEffectsImage = 0x20726F` (0x27 bytes)
- **The wrapper.** It computes `pThis ? pThis-8 : NULL` (IDirectSound to CDirectSound), forwards its 4
  arguments to `0x206B88`, then `ret 14h`. Its masked hash `0xE0165516ACA00EEE` equals 5558
  `IDirectSound_DownloadEffectsImage`. GetEffectData's wrapper has the same bytes, so the callee
  decides.
- **The callee `0x206B88` is 5558 `CDirectSound_DownloadEffectsImage` `0x28BF62` without the dead-object
  check.**
  - It calls AutoLock `0x2069AA`, whose 0x1E bytes hash to 5558's `0x77CF69460E7107DD`.
  - It stores `DSEFFECTIMAGELOC` (two dwords, or -1/-1 for NULL) through `0x206B16`. 5558 does the same in
    `0x28BDE9`.
  - It forwards `(pvImageBuffer, dwImageSize, ppImageDesc)` to the loader `0x206A23`, which sets or clears
    the "image loaded" bit (5558 `0x28B9C1`).
  - GetEffectData's 5558 method has a different shape: one 0x3A helper and no image-location store.
- The SetI3DL2Listener method (`0x206C46`, behind wrapper `0x2072F2`) later returns `0x88780032` when
  that stored I3DL2 index is -1.
- **The game names the action.** In `bInitSpecificWave` (`0x55440`):
  - DirectSoundCreate is called, then `0x20726F(pDS, 0x285A98, 0x60F8, &{0,1}, &desc)`.
  - The failure string is `"Downloading default DSP program failed"`; the success string is
    `"Downloaded default DSP program from static array OK"`.
  - SetI3DL2Listener follows.

## Not proven (20)

| Name | Reason |
|---|---|
| `crt_tan` | Not linked. No hit for the 5849 hash (0x95). No code section contains `mov edx, 20h` (OP_TAN) next to a `lea ecx, <name>`. The same search finds exactly the known acos (`0x1E0605`, `0x1E0669`) and sin (`0x1E108F`, `0x1E10E4`) sites. No code references a "tan" string. The LIBC `.data` name cluster `0x2B74D8-0x2B755C` has only pow, acos, asin, fmod, sin, cos and sqrt. The 15-name table at `0x27C81C-0x27C8C0` is referenced only from data (`0x2B7A2C-0x2B7AE4`). Every `fptan` in the image is inline code inside ordinary functions (e.g. `0x11C7E0`, `0x1D6960`, `0x1EB590`); none has the worker shape (`fstcw` prologue, OP/name error report, shared tails). |
| `crt_atan` | Not linked. No hash hit (0x82), no OP_ATAN (`0x0F`) / name load in any code section, no code-referenced "atan". LIBC carries only the old-style two-argument atan2: a dispatch table at `0x2B7C99` (OP 0x10) with entries `_CIatan2` `0x1E10F8` and `atan2` `0x1E1102`. atan2 is a different function from the one-argument `crt_atan` worker and is not a target. Apart from atan2's handler (`0x1E645B`), the only `fpatan` in the CRT math code are crt_acos and crt_asin. |
| `crt_log` | Not linked. No hash hit (0xB8), no OP_LOG (`0x1A`) / name load in any code section, no "log" string in the LIBC cluster. `0x22A7FA` references "log" from XGRAPHC (vertex-shader instruction text). The old-style dispatch tables present are fmod (OP 0x16 at `0x2B74FD`), exp (OP 0x14 at `0x2B75C8`) and atan2; there is none for log. The `fldln2`/`fldlg2` + `fyl2x` block at `0x1E3562-0x1E359F` inside LIBC's shared x87 object has no call, jump or table pointer reaching it. The `fldln2/fyl2x` at `0x2BF90` and in D3D are inline. |
| `crt_log10` | Not linked. No hash hit (0xB8), no OP_LOG10 (`0x1B`) / name load in any code section, no code reference to "log10", and no log10 dispatch table (see `crt_log`). |
| `XNetGetEthernetLinkStatus` | No network stack. No hash hit (0x48). No PhyGetLinkState (252) or PhyInitialize (253) import. No NIC MMIO constant (`0xFEF00000-0xFEF00FFF`) in any code section. No XNET section or library. See the socket section. |
| `D3DDevice_BeginPush` | Not in D3D8 3925. No hash hit (0x44). No public `BeginPush(Count)` (1 argument, returns the push pointer). The only `+0x200` push-slack reserve is the internal `0x1FB3D0(device, count)` (`ret 8`). Its 7 call sites are in 6 internal routines: `0x1F8090` (vertex-program load, method `0x41E9C`), `0x1F82C0`, `0x1F84A0`, `0x201390`, and the DrawVertices `0x2014D0` and DrawIndexedVertices `0x201570` anchors. Naming it would change the ABI. |
| `D3DDevice_EndPush` | Not in D3D8 3925. No hash hit (0xF). No function stores its argument into `[D3D__pDevice]` (`0x206720`), by exact pattern or by a scan of all small D3D functions. |
| `DirectSoundUseLightHRTF` | Not separable. The image has exactly one HRTF installer, `0x206A86`: AutoLock, `mov [0x22180C], 0x20B291`, leave lock, `ret`. `0x22180C` starts zeroed, and the game calls the installer once (`0x554DD`) after the DSP download. Nothing tells Full from Light: 3925 has a single HRTF table (61x31 grid at `0x220200`), and its code does not resemble either 5558 variant. DSoundFns emits it as `DirectSoundUseFullHRTF`; so that the address never carries two names, it is not emitted here. |
| `IDirectSound_GetEffectData` | Not linked. DSOUND has exactly 8 IDirectSound wrappers (the `pThis-8` idiom, nothing outside DSOUND): Release `0x206A55`, GetCaps `0x207253`, DownloadEffectsImage `0x20726F`, SetAllParameters `0x207296` (copies 16 dwords to listener+0x144), SetDistanceFactor `0x2072B6`, CommitDeferredSettings `0x2072DA`, SetI3DL2Listener `0x2072F2`, CreateSoundBuffer `0x207D05`. The only `ret 14h` one is DownloadEffectsImage, proven above. |
| `IDirectSound_CommitEffectData` | Not linked. The only `ret 4` IDirectSound wrapper is `0x2072DA`. Its method `0x206C91` calls the listener commit `0x20851A(0)`, which atomically takes the pending-flags word at +0x1B4 and rebuilds the orientation cross-products. That is CommitDeferredSettings, not DSP effect data. |
| `IDirectSound_SynchPlayback` | Not linked. Same reason: no other `ret 4` IDirectSound wrapper exists. |
| `IDirectSound_GetOutputLevels` | Not linked. The two-argument IDirectSound wrappers are SetAllParameters and SetI3DL2Listener, each identified by its body. No code reads the APU peak-meter registers (`0xFE85A6xx`) that the 5558 method reads. |
| `IDirectSoundBuffer_Pause` | Not linked. DSOUND has 15 IDirectSoundBuffer wrappers (the `pThis-0x1C` idiom), each identified by its body. The one-argument wrappers are SetVolume `0x207312`, GetStatus `0x2073E0`, SetCurrentPosition `0x20741C` and SetFrequency `0x207953` (48000 Hz means pitch 0). None is Pause. |
| `IDirectSoundBuffer_PlayEx` | No C API wrapper. The class method `0x206EA1(this, rtTimeStamp, dwFlags)` exists. Its only caller is `CDirectSoundBuffer::Play` `0x2077AB`, called with timestamp 0 behind the Play wrapper `0x20796F`. The method takes the class pointer (interface - 0x1C), so naming it would hand the host the wrong pointer. |
| `IDirectSoundBuffer_StopEx` | No C API wrapper. Same as PlayEx: method `0x206EDE`, whose only caller is `CDirectSoundBuffer::Stop` `0x2077DD` (behind the Stop wrapper `0x207993`). |
| `IDirectSoundBuffer_SetRolloffCurve` | Not linked. The only three-argument buffer wrapper is `0x20796F`, and it is Play (above). |
| `IDirectSoundBuffer_SetAllParameters` | Not linked. The two-argument buffer wrappers are GetCurrentPosition `0x2073FC` (Lock calls its method with `(0, &offset)`), SetBufferData `0x2079AB` (allocates through the 'DSND' pool when the pointer is NULL) and SetNotificationPositions `0x2079CB` (copies count x 8-byte notifies; NtSetEvent at fire time). |
| `IDirectSoundBuffer_SetPlayRegion` | Not linked. Same enumeration as SetAllParameters. |
| `IDirectSoundBuffer_SetFormat` | Not linked. Same enumeration as Pause (one-argument wrappers). |
| `IDirectSoundBuffer_SetOutputBuffer` | Not linked. Same enumeration as Pause. |

The exhaustive byte-hash search found no hit at all for the 5849 or 5558 signatures of the names marked
"no hash hit". For the DSOUND names it found only the shared wrapper shapes, resolved by callee above.

## Socket / XNet / XOnline family (no target row; accounted for explicitly)

`config/game.toml` lists 37 network names: `accept`, `bind`, `closesocket`, `connect`, `getsockname`,
`getsockopt`, `ioctlsocket`, `listen`, `recv`, `recvfrom`, `select`, `send`, `sendto`, `setsockopt`,
`shutdown`, `socket`, the `WSA*` names, the `XNet*` names and `XOnlineStartup`/`XOnlineCleanup`. None
is in `docs/symbol-targets.json` except `XNetGetEthernetLinkStatus`. The image has no network stack:

- There is no XNET or XONLINE section; the section list is above. The 5849 signatures put every XNet
  name except XNetGetEthernetLinkStatus in `sec = "XNET"`.
- There is no XNET or XONLINE library in the XBE library table.
- None of the 111 kernel imports is network hardware: no PhyGetLinkState (252) and no PhyInitialize (253).
- No code section contains the NIC MMIO base (`0xFEF00000-0xFEF00FFF`).
- No byte of the image contains `XNet`, `xnet`, `XNET`, `WSA`, `socket`, `Winsock`, `XOnline`,
  `closesocket`, `getsockopt`, `ioctlsocket` or `PhyGetLinkState`.

Fuzion Frenzy is an offline launch title. Nothing is emitted for this family, and nothing was attached
to unrelated code.

## Found but deliberately not emitted (no target row, or not the target ABI)

- XPP: `XGetDevices` `0x24D37E`, `XGetDeviceChanges` `0x24D3A0`, reporter `0x24D357`, gamepad record
  `0x24C2BC`, probable MU record `0x24C238`, the XInitDevices thunk `0x24D379`.
- XAPI: SetLastError `0x31127`.
- XAPI: `0x3055A` (`ret 8`) is shaped like XGetLaunchInfo. [INFERENCE: not compared with a reference] It
  reads LaunchDataPage, accepts types 2/3 or its own TitleID, copies 0xC00 bytes, frees the page
  (ordinal 171), and otherwise returns 0x490.
- CRT: the cdecl entries listed in the CRT section.
- DSOUND: HRTF installer `0x206A86`, class methods PlayEx `0x206EA1` and StopEx `0x206EDE`.
