# DirectSound effects image layout and the XPP gamepad record (XDK 3925)

Target: `out/ff.exe` (Fuzion Frenzy `default.xbe`, XDK 3925, guest addresses 1:1).
Reference: `out/sims5558.exe` (The Sims: Bustin' Out, XDK 5558).
Names below were applied to the shared IDA database for navigation (`CMcpxAPU_ParseEffectsTable`,
`XDEVICE_TYPE_GAMEPAD_TABLE`, ...); they are descriptive, not recovered symbols.

## Effects image layout for XDK 3925

```toml
EffectsImage_Skip0    = 0x804   # CMcpxAPU_ParseEffectsTable 0x2091EB (reached from CDirectSound_DownloadEffectsImage 0x206B88) adds this dword count: `add eax, [ebx+804h]` at 0x209200
EffectsImage_Skip1    = 0x80C   # 0x2091FA loads this dword count: `mov eax, [ebx+80Ch]`
EffectsImage_Desc     = 0x818   # 0x209208: `lea esi, [ebx+eax*4+818h]` - descriptor at image + 0x818 + (Skip0 + Skip1) * 4
```

**Verdict: the 3925 effects-image format matches the host model exactly.**
`table = 0x818 + (u32(image + 0x804) + u32(image + 0x80C)) * 4`, `count = u32(table)`, then `count`
32-byte records starting at `table + 8`, each holding four (pointer, size) pairs. Proven below from the
3925 code, cross-checked against the 5558 twin, and confirmed against the game's real image bytes.

### Call chain (3925)

| Address | Function | What it does |
|---|---|---|
| `0x20726F` | `IDirectSound_DownloadEffectsImage` | adjusts `this` by -8 and calls `0x206B88` |
| `0x206B88` | `CDirectSound_DownloadEffectsImage` | calls `0x206A23` with `(image, size, descriptor out)` |
| `0x206A23` | (wrapper) | `ecx = [esi+0D0h]`, calls `0x20946C`, sets/clears bit 0 of `[esi+1C4h]` |
| `0x20946C` | `CMcpxAPU_DownloadEffectsImage` | copies the header and payload into DSP memory, calls `0x2091EB` |
| `0x20DC97` | `CMcpxDspImage_CopyToDsp` | `edi = [[ecx+10h]] + offset; rep movsd` |
| `0x2091EB` | `CMcpxAPU_ParseEffectsTable` | locates the table, reads the count, walks the records |

### Why reading the DSP copy equals reading the image

`0x2091EB` does not read the caller's image; it reads the DSP memory base `ebx = [[[ecx+0Ch]+10h]]`
(0x2091F1-0x2091F8). That is the same base `0x20946C` wrote to (`esi = [[[edx+0Ch]+10h]] + 800h`,
0x209476-0x209482) and the same base `0x20DC97` writes to (`edi = [[ecx+10h]]`, with `ecx = [edx+0Ch]`).
`0x20946C` fills it as:

- `dsp[0x800..0x817]` = image dwords `[0x800]`, `[0x804]`, `[0x808]`, `[0x80C]`, `[0x814]` (0x20948F-0x2094B8;
  `[0x810]` is left zero and later set to 3 at 0x209343);
- `dsp[0x818..]` = `image[0x818 .. size)` via `0x20DC97(0x818, image + 0x818, size - 0x818)` (0x2094BB-0x2094D6).

So `dsp[0x804] == image[0x804]`, `dsp[0x80C] == image[0x80C]`, and every byte at or past `0x818` is the
image byte at the same offset. The table address computed in DSP memory is therefore the image offset
the host computes.

### The table walk (3925, `0x2091EB`)

```
0x2091fa  mov  eax, [ebx+80Ch]                 ; Skip1 dword count
0x209200  add  eax, [ebx+804h]                 ; + Skip0 dword count
0x209208  lea  esi, [ebx+eax*4+818h]           ; table = 0x818 + (Skip0+Skip1)*4
0x20920f  mov  edi, [esi]                      ; count = u32(table)
0x209211  dec edi / shl edi,5 / add edi,28h    ; descriptor size = 32*count + 8
0x20924c  push dword ptr [esi+4]               ; u32(table+4) -> sub_20DCBF (scratch size, 4 KB pages)
0x2092b3  lea  edi, [esi+8]                    ; first record at table + 8
0x2092bb  push dword ptr [edi+4]               ; record +4: code size for this effect
0x2092fb  add  [edi], ecx                      ; record +0  pointer relocated
0x20930e  add  [edi-18h], ecx                  ; record +8  pointer relocated (after edi += 20h)
0x2092ea  add  dword ptr [edi+10h], 0FE836000h ; record +16 pointer relocated
0x20931c  add  [edi-8], eax                    ; record +24 pointer relocated
0x209308  add  edi, 20h                        ; 32-byte stride
0x209325  cmp  eax, [esi]                      ; loop count times
0x209338  rep movsd                            ; copies the 8 + 32*count byte descriptor out
```

Pointers sit at record `+0`, `+8`, `+16`, `+24` and sizes at `+4`, `+12`, `+20`, `+28`: four
(pointer, size) pairs, exactly what `EffectsDesc` reads (`m = t + 8 + i*32`, size at `m + k*8 + 4`).
The `8 * count` bytes after the table are per-effect 8-byte keys fed to `sub_20DE32` together with the
code bytes; that only affects DSP code contents, which the host does not run. The count and the
records are read before any of that processing, so they are plaintext in the image.

### 5558 twin (`out/sims5558.exe`)

`IDirectSound_DownloadEffectsImage 0x28DD4C` -> `0x28BF62` -> `0x28B9C1` -> loader `0x29426D` -> walker
`0x294000`. The loader `0x29426D` is instruction-for-instruction the 3925 `0x20946C` (same header copy of
`[0x800]/[0x804]/[0x808]/[0x80C]/[0x814]`, same `0x29B182(0x818, image+0x818, size-0x818)`). The walker
`0x294000` is the 3925 `0x2091EB`: `mov eax,[ebx+80Ch]` (0x294013), `add eax,[ebx+804h]` (0x294019),
`lea esi,[ebx+eax*4+818h]` (0x294022), `[esi]` count, `[esi+4]` scratch size, records at `esi+8`, stride
`20h`, the same four relocation constants (`0FE836000h`, `17C6818h`, `0FFA0BE7Ah`, `0C000h`). The only
differences are bookkeeping, not format: 5558 allocates the descriptor from a tagged pool (`'DSob'`) and
keeps it at `this+20h` (3925: `sub_11700`, `this+24h`), and 5558 spins on `[0x810]` after writing it.
These are the same instructions `sdk/xdk/5849/layout.toml` cites at `0x4D1DA2` / `0x4D1DA8` / `0x4D1DB1`.

### Check against the game's real image

`bInitSpecificWave 0x55440` passes the static array `0x285A98`, size `0x60F8`, to
`IDirectSound_DownloadEffectsImage` (0x55492/0x5548D); its success branch logs
"Downloaded default DSP program from static array OK". Parsing those bytes with the model:

- header: `[0x804] = 0xBB8` dwords (code, bytes `0x818..0x36F8`), `[0x808] = 0x36F8`,
  `[0x80C] = 0xA38` dwords (state, bytes `0x36F8..0x5FD8`), `[0x810] = 3`;
- table at `0x818 + (0xBB8 + 0xA38) * 4 = 0x5FD8`; `count = 7`; scratch `u32(table+4) = 0xC2000`;
- records end at `0x5FD8 + 8 + 7*32 = 0x60C0`; 7 eight-byte keys end at `0x60F8` = the image size exactly;
- record pair 0 offsets run contiguously from `0x818` (`0x818+0x12EC = 0x1B04`, `0x1B04+0x1AC = 0x1CB0`, ...),
  record pair 1 offsets start at `[0x808] = 0x36F8` and are contiguous (`0x36F8+0x8A8 = 0x3FA0`, ...).

Every number is self-consistent; a wrong offset could not land the table end on the image end.

## XPP_GamepadType

`XPP_GamepadType = 0x24C2BC` - **proven**. Written to `config/found-dsound-xapi-data.toml`.

`XInitDevices 0x24CA33` allocates the 0xB4-byte USB manager and constructs it with `sub_25070F`, which
clears `+0xA0` / `+0xA1`, stores the preallocation count at `+0x98` and the preallocation array at
`+0x9C`, and zeroes `+0xA4..+0xB3`. It then calls every class-driver init in the table
`0x24C224..0x24C233` = `{0x24C3D4, 0x24C248, 0x24C2EC, 0x24C304}`. Each class descriptor is
`{0xFFFFccxx, init, ..., type count, type-record list}`:

| Descriptor | USB class | Types | Type records |
|---|---|---|---|
| `0x24C3D4` | `0x09` hub | 1 | list `0x24C3D0` |
| `0x24C248` | `0x08` mass storage | 1 | list `0x24C244` -> `0x24C238` |
| `0x24C2EC`, `0x24C304` | `0x03`, `0x58` (XID) | 3 | list `0x24C2E0` -> `0x24C2BC`, `0x24C2C8`, `0x24C2D4` |

The type records are 12 bytes apart (`0x24C2BC`, `0x24C2C8`, `0x24C2D4`; `0x24C238` then the list
pointer at `0x24C244`), matching the three-dword `{Current, Changed, Previous}` record that
`XGetDevices 0x24D37E` reads (`mov esi,[edx]` / `and dword ptr [edx+4],0` / `mov [edx+8],esi`).

Evidence that `0x24C2BC` is the **gamepad** record:

1. `XInputOpen 0x24D22D`: `cmp [ebp+arg_0], offset 0x24C2BC` (0x24D235) selects XID type index 0;
   `0x24C2C8` -> 1 (0x24D242), `0x24C2D4` -> 2 (0x24D24F), anything else fails with error 0x57.
2. The game's own `GetAttachedDevices` `0x49990`: `push offset 0x24C2BC; call XGetDevices` (0x499B2),
   logs "GetAttachedDevices: device mask 0x%08x", tests port bits 0..3 (`dword_285130` = 1,2,4,8), and
   for each set bit calls `XInputOpen(0x24C2BC, port, slot, 0)` (0x49A0D) logging
   "GetAttachedDevices: Connected pad found".
3. `XID_ClassInit 0x24C503` reads the preallocation count for `0x24C2BC` into the XID type-info entry 0
   (`mov byte ptr [0x24C388], al`, 0x24C547). Entry 0 at `0x24C388` has default count 4 (one per port)
   and points at `0x24C344`, whose report sizes are `0x12` input (= sizeof `XINPUT_GAMEPAD`) and
   `0x04` output (= sizeof `XINPUT_RUMBLE`); entries 1 and 2 default to 1 and declare 8/1 and 4-byte
   reports. [The device names for types 1 and 2 are inference; only index 0 matters here.]

`0x24C238` is the **memory-unit** record, not the gamepad: `MU_ClassInit 0x24C3EC` (descriptor
`0x24C248`, USB class 0x08 mass storage) looks up the preallocation count for `0x24C238` (0x24C440),
clamps it to 8, and creates `\Device\MU_0`...`MU_n` device objects; the game calls
`XGetDevices(0x24C238)` at 0x49A51, logs "GetAttachedDevices: memory card mask 0x%08x", and tests 8 slot
bits (`dword_285130` entries 0..7 = bits 0-3 and 16-19, top and bottom slots). What distinguishes them:
the driver class (XID vs mass storage), the `XInputOpen` dispatch (only the XID records are accepted),
and the bit layout (4 port bits vs 8 slot bits).

So the reported candidates are **verified**: `0x24C2BC` = gamepad, `0x24C238` = memory unit.

Optional, not written to the TOML (outside the requested key set): the host's `xapi.cpp` also accepts
`XPP_MemoryUnitType`; the proven value would be `XPP_MemoryUnitType = 0x24C238`.

## Not established

Nothing required is open. Not examined (not needed for the four values): the exact algorithm of the
per-effect `sub_20DE32` processing, and the device names of XID type records `0x24C2C8` / `0x24C2D4`.
