# Fuzion Frenzy (USA): disc, XBE and XDK analysis

Every number here was read from the disc image or the XBE on this machine with the tools in `tools/`.
Commands are reproducible from the repository root.

## The disc image

```
python3 tools/xiso_extract.py info    "Fuzion Frenzy (USA).xiso.iso"
python3 tools/xiso_extract.py extract "Fuzion Frenzy (USA).xiso.iso" assets
```

| Field | Value |
| --- | --- |
| Image size | 2,084,569,088 bytes (1,017,856 sectors of 2,048) |
| XDVDFS partition base | `0x00000000` (trimmed image) |
| Volume descriptor | sector 32, offset `0x10000` |
| Root directory | sector 264, table 520 bytes |
| Contents | 148 entries: 136 files, 12 directories |
| File data | 2,083,933,828 bytes |

Layout: `default.xbe`, `Packages/*.xbp` (the game's archives), `Packages/MetaLevs/*.lnd` (six level
files), `Packages/English/*.xbp` (localised audio and UI), `XDemos/` (a separate demo XBE), and five
**empty** language directories: `Packages/French`, `German`, `Italian`, `Japanese`, `Spanish`.

### A reader defect those empty directories exposed

The five empty language directories have a 2,048-byte directory table filled with `0xFF`. The
XDVDFS reader ported from the sibling port decoded the all-`0xFF` word at offset 0 as a live entry
— `sector = 0xFFFFFFFF, size = 0xFFFFFFFF, attributes = 0xFF, name_len = 0xFF` — and then failed the
image-bounds check with

```
error: directory at sector 4294967295 (size 4294967295) runs past the end of the image
```

so `info`, `list` and `extract` all refused this disc. `tools/xiso_extract.py` now carries two guards
in `_read_directory`: a directory whose own sector or size is `0xFFFFFFFF` is empty and returns no
entries, and an entry whose sector *and* size are both `0xFFFFFFFF` is an unused slot in the AVL table
and is skipped. The genuine bounds check for real directories is unchanged.

## The XBE

```
python3 tools/xbe_info.py assets/default.xbe
```

| Field | Value |
| --- | --- |
| Title | Fuzion Frenzy |
| Title ID | `0x4D530002` (MS-2) |
| Region / media | North America / `dvd_x2` |
| Build type | retail |
| Size / SHA-1 | 2,834,432 bytes / `c6fb6082900532cde5fa8dbb7de8dfa202f0fdad` |
| Base / entry | `0x00010000` / `0x00030F7F` |
| Image size | 3,239,712 bytes, headers 2,420 |
| Built | 2001-09-25T10:19:05Z (certificate 2001-09-25T22:15:40Z) |
| Debug name | `C:\BlitzParty\Game\Src\Core\Release\FFRelease.exe` |
| Kernel thunk | `0x00253360`, **111** imports |

The build is pinned to this exact XBE. A different region or revision needs its own analysis pass.

### Sections

| Name | Virtual | Size | Raw | Raw size |
| --- | --- | --- | --- | --- |
| `.text` | `0x00011000` | 1,983,280 | `0x00001000` | 1,983,280 |
| `D3D` | `0x001F5340` | 71,216 | `0x001E6000` | 58,332 |
| `DSOUND` | `0x00206980` | 110,272 | `0x001F5000` | 109,964 |
| `XGRPH` | `0x00221840` | 68,696 | `0x00210000` | 65,508 |
| `WMADECXM` | `0x002324A0` | 4,112 | `0x00220000` | 4,108 |
| `WMADEC` | `0x002334C0` | 101,720 | `0x00222000` | 101,720 |
| `XPP` | `0x0024C220` | 28,968 | `0x0023B000` | 28,968 |
| `.rdata` | `0x00253360` | 189,396 | `0x00243000` | 189,380 |
| `.data` | `0x00281740` | 639,544 | `0x00272000` | 228,664 |
| `DOLBY` | `0x0031D980` | 28,056 | `0x002AA000` | 28,036 |
| `$$XTIMAGE` | `0x00324720` | 10,240 | `0x002B1000` | 10,240 |

There is **no `.XTLID` section**, and no `XNET`, `XONLINE` or `BINK` section. The absences matter:
see "Why this title is hard" below.

### Libraries — XDK 3925

| Library | Version | QFE |
| --- | --- | --- |
| `XAPILIB` | 1.0.3911 | 1 |
| `D3D8` | 1.0.3925 | 1 |
| `DSOUND` | 1.0.3925 | 1 |
| `XBOXKRNL` | 1.0.3911 | 1 |
| `LIBC` | 1.0.3911 | 1 |
| `XGRAPHC` | 1.0.3911 | 1 |

`xbe_symbols` selects an XDK signature directory by the D3D8 build, so this title needs
`sdk/xdk/3925/`. Fuzion Frenzy was an Xbox **launch** title (the console shipped 2001-11-15); 3925 is
a launch-era XDK, nearly two years older than the 5558 of the sibling port and three years older than
the SDK's own 5849 reference.

## Why this title is hard

The SDK ships signatures for XDK 5849, and the sibling *Sims: Bustin' Out* port added 5558. Neither
transplants to 3925 as **byte** signatures: the same library function recompiled for a different XDK
has different bytes. The sibling solved this with `.XTLID` — the export-id section whose ids are API
identities stable across builds — which named 65 of its symbols outright and bootstrapped the rest.

**This XBE has no `.XTLID` section at all**, so that channel contributes nothing:

```
assets/default.xbe: title 4D530002, entry 0x30F7F, libraries: XAPILIB 1.0.3911 D3D8 1.0.3925 ...
.XTLID: 0 exports; sections: .text@0x11000, D3D@0x1F5340, DSOUND@0x206980, ...
sdk/xdk/3925/signatures.toml: 240 symbols seeded for XDK 3925: 0 named by a .XTLID id this XBE
exports (102 ids kept), 59 with k >= 3 for the constant matcher, 3 with nothing portable
```

The seeded scan then reports:

```
xbe_symbols assets/default.xbe sdk/xdk --out config/game.toml
  found 14 of 240 functions, 0 of 12 data
```

### What the 240 names actually reduce to

The scanner's name list covers libraries this image does not link. Counting the 5849 set by its own
`sec` tag and dropping the sections this XBE has no code for:

| `sec` | Names | Reachable here? |
| --- | --- | --- |
| `D3D` | 100 | yes |
| `DSOUND` | 63 | yes |
| (none) | 17 | yes — CRT math and un-sectioned helpers |
| `.text` | 15 | yes |
| `XPP` | 5 | yes |
| `XNET` | 34 | **no** — no XNET section |
| `BINK` | 4 | **no** — no BINK section |
| `XONLINE` | 2 | **no** — no XONLINE section |

So the real target is **200 names**, of which 14 are proven by the seeded scan and 186 are not.
The 40 XNET/BINK/XONLINE names are expected zeros, not gaps.

### Seeding experiments that did not help

The SDK's own constant matcher is the only automated channel left once `.XTLID` is gone. Three seed
variants were measured against this XBE, re-signing the sibling's 5558 config with
`xbe_symbols --sign` to get a **second independent build** of the same functions as reference
(`config/ref/5558-signed.toml`, 194 of 242 signed):

| Seed `k` (constant set) | Found |
| --- | --- |
| `sig_bootstrap.py` as shipped (5849 constants) | **14** of 240 |
| 5849 constants only, re-emitted | 8 of 240 |
| intersection of 5849 and 5558 constants | 9 of 240 |
| union of 5849 and 5558 constants | 8 of 240 |

Cross-build measurement from the same two sets, over the 240 names present in both:

| Property | Agreement between 5849 and 5558 |
| --- | --- |
| `ret` (bytes popped by `retn N`) | 171 / 171 |
| `regs` (registers read before written) | 175 / 175 |
| byte `len` identical | 171 / 175 (97.7%) |

`ret` and `regs` are therefore usable as **disqualifiers** when testing a candidate address, and
length as corroboration. Constants are not enough on their own at this build distance, so the
remaining names are established by static analysis of the image, verified against those invariants.

## Recompilation

With the 14-symbol config and the (empty) `sdk/xdk/3925/layout.toml`:

```
xbe_recomp config/game.toml --xdk sdk/xdk
  direct: 228 functions, 13787 instructions
  pointer seeds: 2716 accepted, 3235 rejected
  gap fill: 2148 functions
  overlapping decodes: 414
  total: 10573 functions, 633915 instructions, 2165445 code bytes of 2368328,
         384 jump tables, 9 callback entries
```

2,165,445 of 2,368,328 code bytes decode (91.4%). The `unimplemented` opcode tally in
`generated/game/x86_recomp_stats.txt` (`aas` 134, `daa` 36, `insb` 33, `iretd` 37, `bound` 31, …) is
dominated by real-mode and privileged instructions that a user-mode Xbox title cannot execute; they
are gap-fill decoding data as code, not instructions the game runs.

## Kernel imports

```
python3 tools/kernel_coverage.py
```

111 `xboxkrnl` ordinals are imported through the thunk at `0x00253360`; the SDK runtime registers 275
exports and covers all 111, with 0 uncovered. No new kernel implementation is needed for this title.

## Host tool and asset rules

`assets/`, `generated/`, `out/` and `*.iso` are never committed. `config/game.toml` (addresses and
names) and `config/ref/5558-signed.toml` (a signature set derived from the sibling port's own
configuration, containing no game code) are tracked.
