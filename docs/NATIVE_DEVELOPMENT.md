# Native development after Mac Test 1.4

Status: source work in progress; **not a new published installer**. No physical
Mac gameplay or Windows co-op has been tested. Credential recovery remains
removed. The published 1.4 build retains its recorded source commit and checksum.

Runtime source `ee6044ad5c35daced438fad12b8e13372674f8d0`
[passes portable, Intel and Apple Silicon build jobs](https://github.com/supagood-oss/BO3MacFix/actions/runs/40809493572),
including compilation, signing, installer recovery and converter startup.
The CI archive is named `BO3MacFix-Native-Development.zip`; no replacement
release or installer version has been published for this source work.
Local changes after that commit add the free-aim adapters, interactive prompts
and private-lobby migration retry below. They have
Windows-hosted substituted-engine checks, but have not run through the Mac SDK
build jobs. Installer inputs now identify the next candidate as 1.5; **there
is no built or published 1.5 download yet**.

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
| AAE migration eligibility, host ratings and notification retry | `140dae8`, `e07413`, sender `123297b` scoped to return `e076be` | Eligibility/table/stride checks; 300 bounded rating comparisons; 432 native notification comparisons. The local retry requires an active private host lobby and selected-XUID membership, and retains the Mac sender's message packaging. Live migration remains unqualified. |
| AAE viewmodel UI widgets and bone snapshots | `d884f4`, `f73f60`, method chain `b2c9278` | Both widget methods, per-client bone positions/quaternions, rooted Lua callback/table objects and Mac projection/layout; 304 position calculations match bounded Windows execution bit for bit |
| AAE checkpoint effects-list relocation | `12d4629`, restore return `10405d3` | Mac pool initializer `12d4115` and allocator initialization `12d4209` confirm offsets; 300 bounded comparisons against the Windows repair, including damaged inputs |
| AAE displayed grenade-arc anchors | draw `11f4926`, enqueue `1392bda`, returns `11f4b43` / `11f4c56` | Mac descriptor layout and the first two draw passes matched; 488 smoothing cases match original Windows arithmetic bit for bit; native-sized persistent point ring |
| AAE arc prediction weapon override and segment guard | definition `12362cd`, lookup `1236c1b`, sample `11f531b` | Definition replacement scoped to worker return `11f2643`; same Mac projectile field offsets; 56 bounded comparisons against native override validation; SysV two-float/two-GP sample arguments forwarded |
| AAE co-op pause synchronization | server frame `11d64a6`, client frame `e1a4c3`, paused check `11d5ba4`, tokenizer `f76456` | Reliable command/ACK barrier, heartbeat, guest state/timeout and menu policy; 840 bounded native comparisons and 104 native map-name comparisons; Mac argument/layout fixtures |
| AAE camera links and checkpoint link restoration | draw `e0f8b8`, axis `1352ba1`, pose refresh `104b9ab`, save `fb16a0`, load `fb4a92` | 630 bit-exact native pose comparisons; deferred draw queue, copied shader descriptors, deletion cleanup, Mac CSC by-value entity references, two rotating memory-file slots and split-screen isolation tests |
| AAE shared checkpoint engine support | frame `1225cee`, server load `fb3965`, message string `12477a5`, builtin row `694cce0` | Server/client buffers, `savegame_create` Zombies dispatch, two-part restore mask and reliable-queue cleanup; 192 native buffer and 128 restore-mask comparisons |
| AAE free-aim shooting and reliable commands (local changes) | bullet `e285ad`, projectile `1023e64`, grenade `1022bc8`, rocket `10230bf`, command creation `149f3aa`, command execution `105b379` | 900 native basis, 525 number, 518 command and 800 projectile comparisons; typed Mac argument forwarding, 50 ms change-only updates, backlog/wraparound, local stability and remote pointer/state checks |
| AAE free-aim reticle and client spread (local changes) | scoped crosshair return `d54fac`, inline sites `d555ad` / `d555f7` / `d561e8`, spread `d39481` and two callers | Mac predicate is inlined; scoped resume preserves stack/registers; 900 native fade comparisons; spread caller pose capture, temporary basis pointers and original argument/input preservation fixtures |
| AAE free-aim muzzle snapshots and trace consumer (local changes) | inlined queue `d38755` / `d39374`, queue `10876ba0`, client DObj `f543c3`, world tag `10b5d51` | Native 32-record ring, 250 ms muzzle fallback, 500 ms selection and three temporary vectors; all 1,100 bounded native selection comparisons match, including 649 selected records; queue/frame/owner, engine-edit preservation and restoration tests |
| AAE interactive prompts (local changes) | projection `1304895`, fallback `1304789`, model clear `1305337`, scoped mode `127af60` | 4,480 native smoothing steps match exactly; native 128-record registry and caller-specific CP UI behavior; separate position/reset/model paths, inactive and mod-unload fixtures |

All locations are RVAs in the independently hashed Mac executable recorded in
`t7o_mac_offsets.h`. Native hooks require that engine build and the generated
prepatched executable. That is an engine-layout requirement, not a general
Workshop version allowlist. Inactive hooks retain original engine paths.

The local development prepatch fixture accepts 81 hooks and 35 settings using
6,106 of 6,272 bytes of verified existing padding. It passes 162 real-image
redirect/fallback cases. The migration entry's relative call is relocated and
its original callee is substituted with RET for that bounded fallback check.
Another 52 actual-image setting checks cover all 35 settings, including 25
unaligned stub starts, with engine CALL targets substituted. Zero-slot paths
and empty inline callbacks preserve the original live registers/SIMD/flags;
the three MOV settings explicitly declare their comparison flags dead.
Sixteen assembly bridges pass 1,078 synthetic machine cases,
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

Hook, setting and call stubs now use byte packing within the same verified
padding. x86 branch targets have no instruction-address alignment requirement;
the __DATA pointer slots retain eight-byte alignment. The generated table ID
includes this layout choice so the launcher rebuilds an older cached executable.
The actual-image fallback tests cover every hook at its generated address.
Format 3 omits the hook records' duplicate sixteen-byte original-code copy:
the verified compiled hook table supplies exactly the overwritten prefix, and
untouched bytes remain in the image. The launcher rebuilds its cache for this
format/table change; a mismatched runtime table is refused. All 2,703 tested
original-code overlay windows match the actual unpatched Mac image.

The camera adapter uses the Mac's five-argument DObj scene submission routine,
not the lower renderer's different interface. It waits for the matching CG view
axis update before drawing a linked object, checks its frame/entity identity,
and refreshes its pose after changes. Four pending records per local client
retain the native shader/descriptor copies. The Mac `delete`, `forcedelete` and
`linktocamera` builtin entries are restored on unload; `linktocamera` accepts the
native extension's zero-argument default mode. Checkpoint links are cached by
the two existing client memory-file slots and their buffer identities; no disk
save or network format is added. This covers camera state restoration only,
not the separate checkpoint transaction, script and transport extensions.

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

The effects allocator component repairs the restored free list.
It runs after the engine's restore call, for the matched save-load caller and
Zombies session mode. The complete physical block partition and doubly linked
free list must validate before any pointer is changed. Other modes, damaged
lists and inactive mods retain the engine result. No save is rewritten on disk.
The additional shared checkpoint adapter enables two 4.5 MiB server buffers and
two 2.5 MiB client buffers before engine memory-file initialization. The ordinary
Mac Zombies initialization leaves the client buffers absent. Existing adequate
buffers are retained; replacement invalidates the engine's checkpoint slots and
state just as the native repair does. The owned buffers stay allocated for the
process lifetime because engine memory-files can retain their addresses.

The `savegame_create` builtin follows the original path outside Zombies and
schedules the existing engine checkpoint operation in a running Zombies map,
unless another create/commit/restore is pending. Its original callback is
restored on mod unload. The server load and shared camera/client load chain
clear stale reliable strings and advance acknowledgements to the restored
sequence. Client and server completion bits retain the native ordering behavior.
During restoration, a scoped reliable-string writer substitutes an empty string
only when a bounded read of the existing pointer fails; valid commands and all
other writer callers are preserved. The Mac uses its own memory-read API.

The local disk adapter described below adds slots, menu dates and cross-session
restore. This remains unqualified in the physical Mac game.

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

The Mac `mhead` packet path uses `e077de` / `14022b6`. The separate migration
notification sender `123297b` matches Windows `1ed50b0`: host-session lookup,
migration eligibility, selected-XUID lookup and existing message packaging.
Its caller `e0756d`, return `e076be`, collects the same XUID list/count as the
Windows migration call at `21fa299`. The local retry hook first retains the
normal result. Only a failure during phase 4 in the matching Zombies/private
session may retry through lobby 0, after state 2 and membership checks on the
private host-session array. The Mac root `101c8760`, state `+40`, member `+f8`
and stride `30` are independently confirmed by its session/member routines.
All 432 bounded native cases match results and sender arguments. This remains
a source/fixture result; actual Windows-Mac host migration needs qualification.

## Additional adapters in the 1.5 test build

Free aim's arithmetic, parsers, projectile adjustment and reticle fade have
3,643 bounded native comparisons, including successful output bits. The local
Mac adapter connects seven entry hooks and seven inline sites, preserving the
Windows `aae` reliable-command protocol and its default settings. The two
client spread callers capture the Mac pose from the verified caller frame;
the spread hook forwards all six GP, five SSE and four stack arguments with
temporary rotated basis pointers. Unrelated callers and inactive paths retain
their original inputs. No Windows DLL is initialized or loaded on Mac.

The additional Windows trace consumer (`123de80`, caller `12419ef`) is inlined
into the Mac queue drain `d3870e`. Each queue entry is `8b0` bytes and contains
the same parameter block at `+4`; its three relevant vectors retain `+10`,
`+1c` and `+60` relative to that block. The local adapter brackets each queued
iteration after the engine worker barrier. It checks the local client, owning
entity and active view before selecting the newest unconsumed matching record.
The original vectors return after the iteration, individually preserving fields
the engine changed. A nested queue frame cannot restore the outer frame.
The passive completion callback remains available across mod deactivation for
an iteration already in flight. The Mac's own DObj/world-tag path supplies
`tag_flash`; no Windows entity, object or function address is used at runtime.
Selection matches 1,100 bounded native cases, including 649 successful changes.

Interactive prompts use the original helper's two-piece easing, bounded frame
delta and speed, valid element bounds and transition reset behavior. All 4,480
bounded native steps match exactly (registry and CRT hypot substituted).
The Windows patch bytes for its three CP mode calls are `mov al,1; nop*3`,
not empty NOPs. The Mac mode hook returns true only for mode 2 at the matching
three layout callers; other callers use the original mode predicate. The
generic widget's parent `clamped` model is cleared at the matched layout point.
These additions passed the 1.5 Mac SDK builds. Physical gameplay qualification remains pending.

AAEUnified has real deferred callbacks for checkpoint and link-to-camera.
Earlier classification of these as stubs was incorrect. The empty `lua_start`
methods say nothing about deferred engine or UI registration.

Remaining optional AAE work includes the LAN browser. The adapters below now cover
the local checkpoint disk workflow and mantle hints; live qualification remains
separate from implementation.

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


## Checkpoint disk persistence and mantle hints (1.5 source)

The disk adapter copies only a completed server/client checkpoint pair. It
observes both save starts and both index commits, checks MemFile completion
fields and block partitions, then sends copied bytes to a file-only worker.
The six `zm_savegame_slot` choices, `zm_savegame_autorestore 0|1` and
`aae_savegame_date_1` through `_6` retain the menu contract. The existing
`aae_savegame_folder` default is `_NativeCoreTest`.

Saves reside in `~/Library/Application Support/BO3MacFix/AAESavegames`.
The Mac-specific `.macsav` container records the engine layout, Steam owner,
map, mod directory, player capacity, slot, checksums and pointer-free camera
links. It deliberately does not import Windows AAE save files. No displayed
Workshop version allowlist is added. Full checksums, exact file length and
at most 256 bounded MemFile blocks are verified before engine data changes.
Writes sync a temporary file, retain a `.bak`, and replace the save atomically.

Resume waits for a committed checkpoint pair, active connected players and
five seconds of stable state. The original Mac trigger `1226241` selects the
last committed server checkpoint and calls `140d828(true)`, which sends the
engine's existing `fastrestart` message. All 144 bounded original Mac/Windows
trigger cases match, with the network helper substituted. A rejected trigger
restores the previous buffers, MemFiles, states and camera sidecar. Accepted
restarts must complete both server and client loads; a stalled restore prevents
further disk overwrites and reports the incomplete state. Changing the request
or context cancels a pending read. A failed read does not replace its source
with the current fresh-game checkpoint.

Local tests cover disk round trips in separate processes, all 1,024 one-byte
header mutations, malformed blocks, damaged/truncated/trailing files, retained
backups, paired saves, request cancellation, a fresh-session adapter reset,
waiting for both load parts and rejected-trigger rollback. These do not prove
that every serialized engine object survives a real process restart or that
remote Windows clients remain synchronized. Both are explicit first-Mac tests.

Mantle hints use the Mac move/gate/trace/HUD functions at `113ebf6`, `14c4f60`,
`14c3d7e` and `112fe20`, plus height site `14c3ff8`. Scoped probes cannot start
physical traversal. The two-second warmup, 20 ms latch, separate prediction/
server channels, configurable height, traversal resets and HUD bit handling
match 3,475 bounded native comparisons across 1,300 sequences. The Mac engine's
own mantle-enable dvar supplies its enable policy; an additional Windows-only
integer-dvar address has not been mapped and is never used on Mac. HUD mode
changes apply only to the matching caller.

Prepatch format 4 reconstructs hook and setting originals from the matching
compiled tables. The existing table ID rebuilds the cache when those tables
change. No new executable segment is added: the actual image builder fits
85 hooks, 36 settings, two calls and 41 byte patches in 6,056 of the existing
6,272 padding bytes. Local checks cover 170 hook dispatches, 54 setting cases,
2,788 original-code overlay windows, and 1,102 synthetic machine cases across
17 assembly bridges. Both Mac SDK builds, signature verification, automatic
dependency/converter checks and archive inspection passed for runtime revision
`f12af1dbcc90f953798a750133dde10a5d94fe01` in
[run 40822820940](https://github.com/supagood-oss/BO3MacFix/actions/runs/40822820940).
[Mac Test 1.5 is available](TEST_1_5.md). Physical Mac/Windows co-op and
cross-session engine restoration remain unqualified.
