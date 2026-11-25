# GTA IV Complete Edition (1.2.0.59) support

This branch ports IV-SDK .NET to **GTA IV: The Complete Edition, exe version 1.2.0.59** (the current Steam/Rockstar build).
It has **not been tested in-game yet** – please report crashes together with the IV-SDK .NET log.

## What changed

### 1. Memory offset config: `IVSDK/12059.ini`
IV-SDK reads all game addresses from `IVSDK/<version>.ini`. For 1.2.0.59 that is `12059.ini`, located next to
`1070.ini`/`1080.ini` in `IVSDKDotNet/Memory Offset Config Files/IVSDK`. It has the same keys and layout as `1080.ini`
(all values are RVAs, i.e. offsets from the module base) plus one CE-only key, `CFrontEnd.ToggleWaypointObject`.

The addresses were obtained by binary-diffing the 1.0.8.0 exe against the 1.2.0.59 exe:

* Anchors: the ~2,850 script native handlers (identical hashes in both builds) and unique string references.
* Propagation through aligned call/data-reference sequences of matched functions, and through vtables.
* Code locations inside functions (hook call sites, patched string operands) by aligning the surrounding instructions.
* Every value was then cross-checked with independent methods: callee/size similarity, reference-shape audits,
  an instruction-level second opinion for globals, and globals touched by hash-identified native handlers.
* The method was validated by porting 1.0.8.0 → 1.0.7.0 and comparing with `1070.ini`: 491/495 identical, and the
  4 differences are errors in `1070.ini` itself (e.g. `CRestart.bFadeInAfterNextDeath` holds the 1080 value).

Of the 495 entries, 418 passed all automated checks, 31 were flagged and then confirmed by manual review, and
46 were resolved or corrected manually (justification for each is in `CE_PORT_REPORT.md` of the port deliverables).

### 2. Class layouts (compile-time)
Complete Edition changed three classes that IV-SDK maps directly:

| Class | Change on CE | Size 1080 → CE |
|---|---|---|
| `CVehicle` | 0x50 bytes removed at 0x210..0x260; every member ≥ 0x260 is **-0x50** | 0x20D0 → 0x2080 |
| `CPed` / `PedWeapons` | `PedWeapons` (at 0x2B0) lost its first 0x18 bytes (active slot +0x18→+0x0, weapons +0x3C→+0x24); CPed members 0x2C8..0x48x are **-0x18**, from 0x484 on **-0x10** | CPed 0xF00 → 0xEF0, PedWeapons 0xCC → 0xB4 |
| `CPlayerInfo` | +0x10 at 0x48..0x4C, -4 at 0x4AC..0x4BC, +4 at the end: members 0x4C..0x4A8 are **+0x10**, from 0x4BC on **+0xC** | 0x5C0 → 0x5D0 |

Everything else IV-SDK exposes (CEntity, CPhysical, CDynamicEntity, CObject, CPad, CCam, model infos, handling data,
garages, zones, radio stations, …) has the same layout. Sizes were confirmed through pool constructors
(`CPool<CPed>` item size 0xEF0, vehicle pool 0x2080, `CPlayerInfo` allocation 0x5D0).

Because C++ struct layouts are fixed at compile time, **the build target is chosen with a preprocessor switch**
in `IVSDK/IVSDK.h`:

* default on this branch: Complete Edition (`IVSDK_CE`)
* define `IVSDK_CLASSIC` to build for 1.0.7.0 / 1.0.8.0 as before

A build refuses to start (message box) on a game version it was not compiled for, so the wrong build cannot
silently corrupt memory. Use `IVSDK_VEH_OFS`, `IVSDK_PED_OFS`, `IVSDK_PLINFO_OFS` and `IVSDK_CE_SELECT` when
adding new members to these classes.

### 3. Calling conventions
CE was rebuilt with whole-program optimisation, which changed the calling convention of a few functions IV-SDK calls:

| Function | 1.0.8.0 | 1.2.0.59 |
|---|---|---|
| `CTheScripts::FindNativeAddress` | hash in ESI | `__stdcall(hash)` |
| `CBaseDC::Add` | `__thiscall` | `__cdecl(dc)` |
| `CNetwork::CheckFramerate` | `__cdecl(bool, int)` | bool in CL, int in EDX |
| `rage::grcTexturePC::grcTexturePC` | `__stdcall(this, name, unk)` | `__thiscall` |
| `CFrontEnd::ToggleWaypoint` | object in ESI (not set by IV-SDK) | object in ECX (`ToggleWaypointObject`) |

These have CE-specific call paths. All other function entries have matching conventions (`ret N`, register arguments).

### 4. Hardcoded addresses
`fiFile::Close/GetLength/Flush` and `IVMenuManager::Get/SetSetting` used hardcoded 1.0.7.0/1.0.8.0 addresses; they now use
the new 3-version `AddressSetter::Get/GetRef(addr1070, addr1080, addr12059)` overloads.

## Known limitations
* Not tested in-game, and not compiled with MSVC by the porter (the struct layouts were verified with clang `static_assert`s).
* Inferred (not directly observed) offsets: `CVehicle::m_fEngineRPM` (0x638), `CVehicle::m_n*Color2` (0x25D..0x260 – these
  look misattributed even on 1.0.8.0), and `CPed::m_pStandingOnEntity/m_nVoiceHash/m_pCollider/m_nWeaponObjectVisible`
  (follow the measured -0x10 shift).
* `MemoryAccess.GetValue/SetValue/GetAbsoluteAddress(addr1070, addr1080)` (script API with raw addresses) return 0 / do nothing on CE.
  Scripts should use named config entries (`GetAddressFromConfigFile`) instead.
* The bundled ScriptHookDotNet compatibility binary is not part of this repository and was not reviewed for CE.
* Scripts that hardcode 1.0.7.0/1.0.8.0 addresses or class offsets themselves will not work on CE.
