# Exact Mac binding evidence

Development research for the licensed Steam Mac executable downloaded through
the owner's signed-in Steam client on 2026-10-25. No game executable, Workshop
package, decoded opaque backend value or account credential belongs in Git.

- SHA256: `932f0918fb9871bc625b69a587135a5c2976e9f9e6b7289cfadda8ab996e1cd0`
- SHA1: `5e42371b6a16f2b11e637f48e38554625570aa9f`
- Mach-O UUID: `3c8ec670-ccac-3fb4-b303-50ac356e845f`
- Architecture: x86_64, thin Mach-O executable; preferred base `0x100000000`.
- Steam app/depot/manifest: `311210 / 1021320 / 4877125046299055051`.

All addresses below are **RVAs from the game's base**, with ASLR applied at
runtime. These are development bindings. None is a qualified live Mac hook.

## Client FX cache

| RVA | Recovered contract and evidence |
| --- | --- |
| `0x12a7d0d` | Generic register: `(instance,type,table,capacity,name,definition,increaseReferences)`. Both client-FX registration wrappers call it. Only type 27 with the expected old table/capacity is redirected. |
| `0x12a7048` | Generic find/allocate index: `(instance,type,table,capacity,name,allocate)`. Complete routine exercised with 1024- and 2048-entry tables, case folding, missing names, last entry and full-table rejection. |
| `0x12a7474` | Client-FX definition pointer by instance/index. |
| `0x12a749c` | Client-FX name; the definition's first field is its name pointer. |
| `0x12a74d5` | Client-FX lookup by name. |
| `0x12a75a1` | Unregister: reference count byte at record offset 12, zero the 16-byte record when the final reference goes away. |
| `0x12a1ae0` | Whole-cache instance reset. The replacement additionally clears the extended table. |
| `0x0a9b8530` | Old client-FX table, 1024 records of 16 bytes; instance stride `0x45fe00`. |

The adapter migrates both instance tables to capacity 2048. It preserves every
byte of existing records. Server FX (type 24), client tag-FX and the DB asset
pool remain separate. Hook prologues are checked before installation and are
also present in the prepatched executable's hook-slot list. Ordinary builds
keep these hooks disabled; `DEV_MOD_HOOKS=1` enables development installation.

Activation must occur before cache users run. That assumption and map/VM reset
ordering require a real Mac test. Portable substituted-engine tests exercise
both registration routes, migration, references, invalid indexes and fallback.

## Script VM and the five AAE operations

| RVA/layout | Recovered contract and evidence |
| --- | --- |
| `0x14450ef0`, stride `0xa048` | Two VM-public records. Top pointer +`0x20`, result count +`0x34`, input count +`0x38`. Script values have a 64-bit value and 32-bit tag in a 16-byte record. |
| `0x11a1e78` | Get integer `(instance,index)`: accepts hash tag 5 and integer tag 7. Actual-code checks include signed high-bit values, wrong types and missing arguments for both instances. |
| `0x11a2890` | Get string, through const-string getter `0x119f199`, cast `0x12f74f8` and string pointer conversion `0x11b6940`. Actual-code checks preserve case for ordinary string tag 2; localized tag 3 is rejected. |
| `0x11a317d`, `0x11a3120` | Push integer / allocate result; actual-code checks verify stack movement, tag and result count with no pending inputs. The real input-release path still requires VM qualification. |
| `0x11a344b` | Push vector `(instance,vector*)`, tag 4, allocates a copied vector. Static call-site and layout evidence; not yet emulated. |
| `0x119f219` | Script error `(instance,message,flag)`, matched to argument-getter error paths. |
| `0x05de4700` | Common `isprofilebuild` builtin entry: hash +0, min/max args +4/+8, function +`0x10`, kind +`0x18`, total `0x20`. Initialized at constructor `0x1205f66`. Original callback `0x1205df7`. |

The bridge validates the builtin's hash, original callback and argument bounds
before changing this data entry. It preserves zero-argument `IsProfileBuild()`
and leaves the normal `int` builtin untouched. Unknown operation IDs retain the
current native dispatcher's no-result behavior. Invalid supported calls enter
the engine's script-error path. It does not patch the separate Lua
`Engine.IsProfileBuild` wrapper.

| AAE operation ID | Mac binding |
| --- | --- |
| `0x22ea46c7` hash | Seed `0x4b9ace2f`, ASCII lower case, prime `0x1000193`, includes the trailing NUL. Known AAE operation-name goldens are tested. |
| `0x35224a7a` getvieworigin | CG pointer array `0x145b9fd0`, count `0x11534590`, stride `0x342a80`; predicted player state +`0x11a8d0`, game time +`0x11a8ac`; routine `0x10768b4(ps,out,time)`. CG caller `0xd3999d` independently establishes that contract. The time field is **not** a client index. |
| `0xe2a60154` startweaponanim | Weapon at player-state +`0x2c0`, dual-wield predicate `0x1382008`; per-hand animation `0xed3b3c(ps,anim,hand)`, both-hand routine `0xed3b76(ps,anim)`. Thirty complete per-hand actual-code cases verify PM-type guard, `0x1fff` mask, `0x2000` toggle and `0x1c` hand stride without changing other fields. |
| `0xe2555348` playanim | `0x12def8b(ps,anim,bodyPart,0,0,1,1)`. Static evidence: seven-argument CG calls, legs/torso words at PS +`0x124`/+`0x126`, timer fields, animation-definition indexing. The Mac TLS descriptor `0x2104fb8` resolves a slot, then two dereferences locate animation definitions; loaded count +`0xa0000`. Guard the index before calling. |
| `0x50ee1cfb` anim_event | Server entity base `0x10cea410`, stride `0x4f8`; client pointer +`0x250`, entity type +`0x1a0`. Client number selects a `0x1660`-byte animation-client record from the address in GOT slot `0x1966c08`. Call `0x12dfb48(client,event,0,1,0x11c042e())`. Native Mac script callers at `0xda5f71` and `0xda5ffe` independently use the same sequence. The parser clears `0x26730` bytes of `0x608`-byte event records: 102 events. |

An absent/non-player server entity causes no animation-event side effect, as in
the current AAE helper. Guest behavior, TLS context, full view-origin math,
both-hand animation, script errors, result cleanup and mod switching still
need live qualification. UEM's inspected scripts do not demonstrate these
custom dispatcher calls, but AAE does.

## Reproducing the bounded checks

Keep the owner's executable outside this repository. In a developer environment
with Unicorn installed:

```sh
python3 tools/inspect_mac_build.py /path/to/CoDBlkOps3_Exe --output build/mac-build.json
python3 tools/verify_mac_contracts.py /path/to/CoDBlkOps3_Exe --output build/mac-contract-checks.json
```

The verifier refuses other executable hashes. It passed **133 cases** on
2026-10-25. It maps isolated memory and executes only the explicitly permitted
routines. ASCII `tolower` is substituted. Original game initialization, OS
imports, native mod initialization and backend requests do not run. This does
not establish hook safety, Rosetta behavior or network compatibility.

## Pause and movement bindings

The authorized Windows code capture matched client pause `0x136b72b`, server
pause `0x155ae00`, actual server-paused predicate `0x155aec4`, strafe baseline
`0x1073e5d`, and sprint policy `0x1143aa2`. The new development adapters read
`hook_pausegame`, `hook_movement_perks`, `t7o_strafe_speed_scale` and
`t7o_omnimovement`. Their original-function fallbacks and input restoration
pass substituted-engine tests. The prepatch builder relocates the client
pause LEA and strafe float load; generated dispatch/fallback paths passed
bounded checks against a private builder fixture.

[Gameplay port evidence](GAMEPLAY_PORTING.md) gives the exact supporting
bindings, Windows build identity, named movement-patch semantics and remaining
work. The implemented timing changes have Mac counterparts, but the Mac
sprint-lerp comparison uses 349 with `jg` where Windows uses 350 with `jge`.
Copying the Windows replacement immediate would introduce an off-by-one error.
The Mac compiler also inlines the corresponding landing calculation into a
larger movement routine. The implementation uses mutable prepatch slots and verified inline boundaries for these paths.

## Completed movement, lifecycle and presentation bindings

All eleven movement options, live command registration and DVAR routing are
implemented. The lifecycle hook resets the adapters after the original mod-load
core, while cache records stay under the engine reset hook's lifetime. The
by-name loader uses `(int,const char*,bool)`; the core uses a record pointer.
The aim-state publisher uses independently matched CG breath timer fields and
weapon predicates. Exact sites and known limits are in
[GAMEPLAY_PORTING.md](GAMEPLAY_PORTING.md).

The private extracted builder accepts all 47 hook prefixes and 21 mutable
settings; 94 generated entry-hook fallback/dispatch checks pass. Synthetic
codegen/inline-bridge coverage is 754 cases. The macOS runner has compiled the
full candidate and run HTTP and installer fixtures successfully. These are
separate forms of evidence: none is a live engine hook or network session.
Ordinary revision/production gates remain closed; the explicit candidate gate
allows the first Mac qualification test after all required adapters activate.
