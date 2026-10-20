# First boot: the NV2A miniport crash, and what it proved

Recorded 2026-10-19 against `out/build/linux-release/FuzionFrenzy`, built from the 14-symbol
`config/game.toml` that `xbe_symbols` wrote.

## What happened

The binary links, starts, reads the settings file, finds the disc and verifies the XBE:

```
[    0.019] Game: default.xbe in .../assets, sha1 c6fb6082900532cde5fa8dbb7de8dfa202f0fdad
[    0.083] error: signal 11 on guest thread 14
[    0.083] error:   eax 00206088 ecx 00205E98 edx 80100000 ebx 00000001
                     esp D005FD10 ebp D005FD28 esi 00205E98 edi 80100060
[    0.083] error:   [esp+000] 001FE5AB
[    0.083] error:   [esp+01C] 001FBBE9
[    0.083] error:   [esp+120] 001F76B9
```

`0x1FE5AB`, `0x1FBBE9` and `0x1F76B9` are all inside the `D3D` section (`0x1F5340-0x206970`), so the
fault is in the **title's own copy of D3D8**, which is what runs for every `[symbols]` name left at 0.

## The chain

`0x1F76B9` is the return address of a call at `0x1F76B4` in `sub_1F7650`. That function:

```c
int __stdcall sub_1F7650(int a1, int a2, int a3, char a4, _DWORD *a5, _DWORD *a6)
{
    if ( dword_206958 == 0 ) dword_206958 = 0x80000;
    if ( dword_206954 == 0 ) dword_206954 = 0x8000;
    dword_206720 = (int)&dword_203B90;
    dword_206724 = (int)&dword_203B90;
    dword_203F98 = 1;
    dword_203B9C |= a4 & 0x10;
    v6 = sub_1FBB00(this: (char *)&dword_203B90, a2: a5);
    if ( v6 >= 0 ) { if ( a6 ) *a6 = &dword_203B90; return 0; }
    sub_1FC010(a1: &dword_203B90);
    return v6;
}
```

Compare `Direct3D_CreateDevice` at `0x2443C0` in the 5558 reference image (`out/sims5558.exe`):

```c
int __stdcall Direct3D_CreateDevice(int a1, int a2, int a3, char a4, _DWORD *a5, _DWORD *a6)
{
    if ( dword_2670E4 == 0 ) dword_2670E4 = (int)sub_00080000;   // 0x80000
    if ( dword_2670E0 == 0 ) dword_2670E0 = 0x8000;
    dword_264948 = (int)&dword_264950;
    dword_265288 = 1;
    dword_264958 |= a4 & 0x10;
    v6 = sub_002500E0(this: &dword_264950, a2: a5);
    ...
}
```

Same signature, same two magic statics `0x80000` and `0x8000`, same `flags |= arg4 & 0x10`, same
device-pointer store, same init call, same success and failure shapes. That is
**`Direct3D_CreateDevice = 0x1F7650`**, and with it:

| Name | Address | How |
| --- | --- | --- |
| `D3D_Device` (the static `CDevice`) | `0x203B90` | the object `Direct3D_CreateDevice` passes to the init body and returns through `a6` |
| `D3D_pDevice` | `0x206720` | holds `&CDevice`; referenced **95 times** inside the `D3D` section, against 2 for the `0x206724` alias set on the same line |

`D3D_pDevice` is confirmed by a function the scanner had already proven,
`D3DDevice_SetRenderState_CullMode` at `0x1F67E0`:

```asm
mov  esi, dword_206720        ; -> &CDevice, and CDevice_Push is at offset 0
push esi
call sub_1FB3B0               ; MakeSpace -> eax = push cursor
mov  dword ptr [eax], 40308h  ; (1 << 18) | 0x0308, the NV2A cull-face-enable method
mov  [esi], eax               ; store the advanced cursor back
```

So this build encodes push methods as `(dwords << 18) | method`, exactly as later builds do, and
`sub_1FB3B0` is its `MakeSpace`.

## Why it faulted

`sub_1FBB00` (`0x1FBB00-0x1FC006`) is the device-init body. It allocates the push buffer with
`MmAllocateContiguousMemoryEx`, creates the NV2A classes, writes `MEMORY[0x80000000]` and calls
`__wbinvd()`. It then calls `sub_1FE540` with `this + 8968`, i.e. **`CDevice + 0x2308`** — the
embedded miniport (`CDevice + 0x1C28` in 5849). `sub_1FE540` sets up two 768-byte gamma ramps, does
`__outbyte(0x80C0, 1)`, and reaches code that reads and writes the **NV2A register window**:

```asm
mov  dword ptr [ecx], 0FD000000h
mov  eax, ds:0FD001804h
or   eax, 4
mov  ds:0FD001804h, eax
mov  dword ptr ds:0FD600140h, 0
mov  dword ptr ds:0FD009140h, 0
```

There is no NV2A on the host, and no mapping behind those addresses, so the guest faulted. This is
the canonical failure of an unnamed D3D8 entry point: the console routine ran and touched hardware
the host never built.

## The fix, and what it proved

`Direct3D_CreateDevice = 0x1F7650` was added to `[symbols]`, and `D3D_Device` / `D3D_pDevice` to
`[data]`. After recompiling and rebuilding, the signal 11 is gone and the run instead stops with:

```
[    0.072] error: d3d: D3D_DirtyFlags is unknown for this image, add it to the config
[    0.072] error: d3d: D3D_DeferredTextureState is unknown for this image, add it to the config
[    0.072] error: d3d: D3D_RenderState is unknown for this image, add it to the config
[    0.072] error: d3d: D3D_Streams is unknown for this image, add it to the config
[    0.072] error: d3d: D3D_SimpleRenderStateEncode is unknown for this image, add it to the config
[    0.072] error: signal 6 on guest thread 14
```

That is the **host** `Direct3D_CreateDevice` in `sdk/runtime/xdk/d3d8.cpp` running its
`XDK_REQUIRE` list and refusing to continue without the data it needs. Three things follow:

1. The guest reached the host implementation, so `0x1F7650` is the right address — a wrong one would
   not have been called at all, and the NV2A fault would have repeated.
2. The NV2A access is gone from the boot path, because the console routine no longer runs.
3. The remaining gate is data and layout, not code: the five `[data]` names above, plus the
   `CDevice_*` offsets `sdk/runtime/xdk/layout.h` resolves out of `sdk/xdk/3925/layout.toml`.
   `layout.h` is explicit that a value the config lacks "is unknown and traps where it is first
   needed rather than being guessed", which is why this failure is a clean abort with five named
   causes instead of silent corruption.

## Open

`sdk/xdk/3925/layout.toml` is still empty. `CDevice_Push = 0x0` is proven (above) and the miniport
sits at `CDevice + 0x2308`; the rest is being derived from this image the way
`sdk/xdk/5849/layout.toml` records its own, one proving function per field.
