# Native development after Mac Test 1.4

Status: source work in progress; **not a new published installer**. No physical
Mac gameplay or Windows co-op has been tested. Credential recovery remains
removed. The published 1.4 build retains its recorded source commit and checksum.

Runtime source `bee7abe0c0720e4bde544b3262b91e6f171dfbdc`
[passes portable, Intel and Apple Silicon build jobs](https://github.com/supagood-oss/BO3MacFix/actions/runs/40798165724),
including compilation, signing, installer recovery and converter startup.
The CI archive is named `BO3MacFix-Native-Development.zip`; no replacement
release or installer version has been published for this source work.

## Implemented adapters

| Native contract | Mac binding | Evidence / qualification |
| --- | --- | --- |
| Expanded forced third-person crosshair | `d54e99` | Predicate and CG offset independently matched; substituted-engine on/off/unload tests |
| AAE 4 GiB main reserve | `d327e4` | Engine startup tuple `(0,0,2,"main",0x80000000,0x1e)` only; no live relocation or eager physical commitment; other-pool preservation tests |
| AAE traversal animation type | `14c50bc` | SysV float3 aggregate ABI; 3,808 differential cases against bounded original Windows arithmetic; portable boundary regressions |
| Traversal height | `d973da`, return `14c415a` | Only the traversal caller's `trm_maxHeight` setting is replaced; invalid values retain native defaults |
| Traversal input comparisons | `14c3e6a`, `14c4fc4` | Original strict comparisons and zero-input asymmetry retained; 5,140 adapter cases; command axes unchanged |
| Traversal surface / vehicle permission | `14c4101`, helper `d691e0` | Mac gclient stride `17210`, level/client/entity relationships, server/predicted fallbacks and deny result 4 checked |
| Expanded viewmodel lean | `d37a72`, key processing `1549275` | Mac DObj lookup `f74d4f`, local tag setter `f74879`, key query `15467e4`; key-up ordering, easing, fire reset, UI/main-thread guards and local-client isolation tests |
| AAE migration eligibility and host ratings | `140dae8`, `e07413` | Mac eligibility wrapper matched by call flow; host-selection table/stride and caller checked; 300 bounded comparisons against the Windows rating helper |
| AAE viewmodel UI widgets and bone snapshots | `d884f4`, `f73f60`, method chain `b2c9278` | Both widget methods, per-client bone positions/quaternions, rooted Lua callback/table objects and Mac projection/layout; 304 position calculations match bounded Windows execution bit for bit |
| AAE checkpoint effects-list relocation | `12d4629`, restore return `10405d3` | Mac pool initializer `12d4115` and allocator initialization `12d4209` confirm offsets; 300 bounded comparisons against the Windows repair, including damaged inputs |
| AAE displayed grenade-arc anchors | draw `11f4926`, enqueue `1392bda`, returns `11f4b43` / `11f4c56` | Mac descriptor layout and the first two draw passes matched; 488 smoothing cases match original Windows arithmetic bit for bit; native-sized persistent point ring |
| AAE arc prediction weapon override and segment guard | definition `12362cd`, lookup `1236c1b`, sample `11f531b` | Definition replacement scoped to worker return `11f2643`; same Mac projectile field offsets; 56 bounded comparisons against native override validation; SysV two-float/two-GP sample arguments forwarded |
| AAE co-op pause synchronization | server frame `11d64a6`, client frame `e1a4c3`, paused check `11d5ba4`, tokenizer `f76456` | Reliable command/ACK barrier, heartbeat, guest state/timeout and menu policy; 840 bounded native comparisons and 104 native map-name comparisons; Mac argument/layout fixtures |

All locations are RVAs in the independently hashed Mac executable recorded in
`t7o_mac_offsets.h`. Native hooks require that engine build and the generated
prepatched executable. That is an engine-layout requirement, not a general
Workshop version allowlist. Inactive hooks retain original engine paths.

The development prepatch fixture accepts 65 hooks and 25 settings using
6,100 of 6,272 bytes of verified existing padding. It passes 130 real-image
redirect/fallback cases. The migration entry's relative call is relocated and
its original callee is substituted with RET for that bounded fallback check.
Seven assembly bridges pass 850 synthetic machine cases,
covering integer/SIMD registers, flags, stack alignment and red-zone preservation.
These are offline tests; they do not exercise Rosetta or actual engine gameplay.

The new pause adapter retains the existing `aae_coop_pause_remote` preference
(native default `0`) and the original `q @` command format, including nonce
heartbeats and reliable-sequence wraparound. The host waits for the tracked
remote clients' acknowledgement before freezing; guest frame time and time scale
are zeroed together. Unpause, timeout, leaving the map, missing snapshots and
mod unload have separate paths. Actual Mac transport timing still needs testing.
The Mac server-client stride is `e5270`, not the Windows `e5170`; sequence/ACK
fields remain `5030`/`5034`, while the test-client field moves to `bb460`.
The source uses four Mac engine hooks: zeroing the outer frame arguments also
reaches the inner command/simulation calls that Windows patches separately.
Menu permission extends the already shared client-pause hook. No packet format,
HTTP endpoint or background network worker is added.

Hook stubs now use four-byte packing within the same verified padding. x86
branch targets do not require sixteen-byte alignment. The generated table ID
includes this layout choice so the launcher rebuilds an older cached executable.
The actual-image fallback tests cover every hook at its generated address.

The UI adapter registers `setupViewmodelTagWidget` and
`setupViewmodelTagWidgetLine`. It uses the Mac HKS table/ref/unref functions,
forwards all six Mac layout arguments, and never fabricates Windows Lua objects.
Tests cover missing bones, invalid projections, callback errors and reentry,
reconfiguration, heap changes and inactive fallback. Registry references replaced
within one activation are released. At mod reload, old handles are deliberately
not dereferenced because the engine can already have destroyed their VM and
reused its address. A surviving VM can retain up to two roots per line widget
per activation until its destruction. This bounded-per-activation retention and
actual render timing still need Mac testing. Prediction/arc anchors are separate.

The checkpoint addition repairs only the effects allocator's restored free list.
It runs after the engine's restore call, for the matched save-load caller and
Zombies session mode. The complete physical block partition and doubly linked
free list must validate before any pointer is changed. Other modes, damaged
lists and inactive mods retain the engine result. No save is rewritten on disk.
AAE's additional checkpoint serialization, save/load callbacks and script APIs
are still separate work; this repair does not establish checkpoint parity.

Arc rendering uses `aae_arc_tag` when its selected bone has a valid position,
then falls back to `tag_weapon_right`. The native name alphabet and 63-character
limit are preserved. Positions remain separate from the quaternion validation
needed by rotated UI offsets. Only the first descriptor at each of the two
matched draw sites changes; other calls and descriptors pass through. The
original join point and endpoint remain fixed. Persistent buffers retain the
same 1024-descriptor lifetime as the Windows helper. The arc worker alone can
select `aae_arc_weapondef` from the Mac engine's loaded weapon-name table.
Matching weapon type, native speed/fuse/lifetime bounds and boolean projectile
flags are required; invalid overrides use the original definition. This affects
the arc predictor, not the firing or projectile-spawn paths. The two matched
sample call sites retain the native five-segment limit with the Mac argument
registers. Unlike the native one-second cache, the adapter checks the selected
loaded definition directly each time, avoiding stale cache pointers on reload.
Actual predictor behavior, render timing and Rosetta execution need Mac testing.

Expanded's lean code rotates `tag_ads`; it does not alter player collision,
aim or movement. The native key-up callback occurs before the Windows event
queue dispatch; the Mac adapter operates before key-state processing instead.
Input routing and render timing remain part of the Mac test. Other mods' bones
remain unchanged until a configured lean key actually requests a lean.

The traversal surface helper also matches 120 bounded original-stub cases.
Its unknown-vehicle-context branch reverses the vanilla bit-24 test; explicit
permission and the ignore setting follow separate exits. The animation word at
trace `+14` is independent of the tested word at `+18`.

Migration transport differs between executables. The Mac `mhead` path sends
to clients directly through `e077de` / `14022b6`; the Windows helper also retries
a game-lobby notification using the private lobby. The two common eligibility
and rating changes are ported. An equivalent additional lobby notification is
not implemented; transport/host-change parity requires further work and a real
Windows-Mac session. No new packet or transport format is invented here.

## Still separate work

AAEUnified has real deferred callbacks for checkpoint and link-to-camera.
Earlier classification of these as stubs was incorrect. The empty `lua_start`
methods say nothing about deferred engine or UI registration.

Remaining AAE components include checkpoint serialization/callbacks and link-to-camera,
migration transport parity, free aim, interaction
prompts, LAN browser and mantle-hint probe/latch/UI behavior. The existing shared
pause and movement adapters are not substitutes for those components.

Expanded cursor/animated-cursor handling and Windows-only authoring/debugging
options remain separate. Optional achievement/ranked overrides, custom GSC,
server FX, sound limits and ultrawide behavior are unqualified. Do not describe
this branch or the existing 1.4 installer as a complete native port.

## Research provenance

AAEUnified SHA256:
`577b4d054128b39bd4999197fc9ce51dc04c3710e49ca547c8f037fec7aa9eac`.
Expanded shared base SHA256:
`016ba5559c4f66cd8de864e4a85a8e7de573e97f4716ec0fd476558c9b4165c3`.

The owner-authorized Windows capture read engine code and static data only.
The game was closed normally. No game memory was written and no original mod
DLL initialization or mod backend was run. Bounded arithmetic comparisons use
substituted memory/services, never native initialization.

The upstream T7Overcharged [DObj declarations](https://github.com/JariKCoding/T7Overcharged/blob/main/src/client/game/symbols.hpp)
and [viewmodel callback](https://github.com/JariKCoding/T7Overcharged/blob/main/src/client/components/bullet_depletion.cpp)
were used as independent interface references; their PC addresses were not
copied into the Mac bindings. The downloaded public reference was inspected
without running or building it.
