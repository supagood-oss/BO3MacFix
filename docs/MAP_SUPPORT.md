> Builds 1, 1.1, 1.2 and 1.3 were withdrawn. Do not use their archives.
> Use clean test 1.4; gameplay changes remain under qualification.

# Map and mod support in Mac Test 1.4

This is an experimental test candidate. **No physical Mac gameplay, online
service acceptance or Windows-Mac co-op has been qualified.** The source work
below addresses specific recovered contracts. It does not establish complete
native-feature parity for every package surveyed.

## Expanded: additional shared native contracts

The analyzed [ZM Expanded Experience](https://steamcommunity.com/sharedfiles/filedetails/?id=3554303768)
package is manifest `4078568122437287745`. Its `T7Overcharged.ff` is a Windows PE,
SHA256 `016ba5559c4f66cd8de864e4a85a8e7de573e97f4716ec0fd476558c9b4165c3`.
Displayed Workshop version labels can change independently of the package.

Implemented in 1.3:

- `JSON.ReadJson` and `JSON.WriteJson`, including native string return values,
  boolean `0`/`1`, numeric types, sections and default values. Reads default to
  read-only. Writes preserve unrelated fields, create parents and use an atomic
  replacement. Malformed existing settings are preserved and return failure.
  Existing map-owned JSON encode/decode helpers are retained.
- `dvarflag name [bit-position ...]`, including clearing with no positions and
  resolving Mac session aliases before updating flags. Positions are converted
  to bits; they are not interpreted as numeric masks. The Mac flag offset,
  session alias layout and accessor were checked in the licensed executable.
- `t7o_override_gamepadtype`: Xbox/PlayStation/Steam choices map to Aspyr's
  different controller enumeration. Overrides stop when the mod unloads.
- The eleven recovered movement command names already match the implemented
  movement family. The `modvar` command already exists in the Mac executable.
  Asset-limit files continue to use the shared validated pool extension.

These changes cover the recovered startup/settings API gaps. **Expanded is
not a complete native-feature port.** Windows cursor files, cosmetic viewmodel
leaning, forced third-person crosshair and extra Windows logging/authoring
behavior remain unported. Optional achievement/ranked overrides, custom GSC,
server-FX, sound-limit and ultrawide experiments have not been qualified.
The settings API does not imply that every optional setting's engine effect is
implemented. Leave those experiments at their normal defaults in the first test.

## Current AAE is different from the earlier reference

Fresh [AAE](https://steamcommunity.com/sharedfiles/filedetails/?id=2631943123)
manifest `9050534316474466018` and
[AAE Lite](https://steamcommunity.com/sharedfiles/filedetails/?id=2994481309)
manifest `7694158027624644005` contain 4.0.0 assets. They share Expanded's native
base image above, plus `T7Overcharged/plugins/AAEUnified.dll`, SHA256
`577b4d054128b39bd4999197fc9ce51dc04c3710e49ca547c8f037fec7aa9eac`.
The earlier native reference was AAE 3.9.5 (`dbaed33f...`).

The credential-dependent HTTP integration introduced in previous test builds
has been removed. The patch no longer reads embedded service secrets, derives
metadata keys or substitutes recovered values into requests. All three encrypted
Axios members remain absent, including after UI VM reuse. The observed AAE 4.0
call sites check member presence before calling; UEM uses the retained ordinary
GET/POST/poll API. Actual startup and online-menu behavior still need testing.

AAEUnified is **a separate remaining native feature port**. Its 14 component
vtables and lifecycle methods were inspected. All component `lua_start` methods
are empty; there is no additional Lua library registration in those methods.
The active components include prediction/tag anchors, co-op pause, free aim,
host migration, interaction prompts, LAN browsing, mantle hints, a larger-memory
patch, traversal and viewmodel UI anchoring. The pool feature reserves or
extends a 2 GiB arena to 4 GiB; it is distinct from our asset free-list limits.
Mac equivalence is not established. Correction from the deeper native review:
checkpoint and link-to-camera startup schedule real deferred callbacks. Empty
`lua_start` methods do not make those components stubs. Their local adapters now
cover engine, disk and camera workflows; physical Mac support is unqualified.

Our original shared pause/movement/FX adapters do not constitute a port of those
new components. Ordinary AAE startup/core play is a test target; full current
AAE parity, optional new features and heavy-map memory behavior are unqualified.
The optional ZoneWriter Windows debug-output guard now sees `winOs` as absent,
so it cannot pass a presence check and then call a missing `execute` member.
No Windows command interpreter is exposed on Mac.

### Native work after the published 1.4 build

The source branch now contains additional development adapters. They are **not
in the published 1.4 download** and have no physical Mac qualification:

- The main CPU arena is reserved as 4 GiB at engine startup, before allocation.
  Only the original 2 GiB main reserve-only pool registration changes; physical
  commitment and every other pool retain the engine's own behavior.
- AAE traversal animation selection, high-height offset, mantle-height setting,
  input thresholds and server vehicle-vault permission checks have Mac bindings.
  Input changes affect cached comparisons, preserving actual command axes.
  The separate mantle-hint probe/latch and HUD now have a local adapter;
  physical Mac behavior remains unqualified.
- Expanded's forced third-person crosshair uses the Mac predicate with its
  different CG offset. Viewmodel leaning has a Mac key-up/bone adapter, native
  easing, per-local-client state and mod-unload reset. The original event-queue
  hook is replaced at Mac key-event processing; live input timing needs testing.
- AAE migration eligibility, host ratings and the scoped private-lobby
  notification retry retain the Mac transport. Live migration needs testing.
- AAE's two viewmodel widget methods now have Mac layout/projection, bone
  snapshots and rooted Lua callbacks. Displayed grenade arcs now use selected
  or default viewmodel tag positions with native smoothing. The scoped arc
  weapon-definition override and five-segment guard also have Mac adapters.
  Predictor behavior, VM reload retention and render timing need a real Mac test.
- AAE co-op pause has the native reliable-command acknowledgement barrier,
  guest frame hooks, heartbeat and reset behavior. Camera links have deferred
  drawing, pose refresh, deletion cleanup and in-memory checkpoint restoration.
- AAE checkpoint effects-list pointers have a validated post-restore repair.
  Shared server/client buffers, Zombies checkpoint script dispatch and reliable
  queue restoration are implemented. Disk slots and cross-session restore
  remain separate work.
- Local source changes add free-aim shooting, projectiles, reticle, spread and
  reliable commands, plus muzzle snapshots and the inlined trace consumer.
  Interactive prompts have scoped mode paths, smoothing/reset and model clear.
  These latest changes have local fixtures and bounded native comparisons;
  their Mac SDK build and gameplay tests remain pending.

See [the native development record](NATIVE_DEVELOPMENT.md) for evidence and
remaining components. These additions do not establish full AAEUnified parity.

UEM’s current manifest was also refreshed: `3514276820181770986` is unchanged
from the earlier 1.3.4 Build-001 analysis. Thirty-three cached selected files
were reverified against that manifest; native SHA256 remains `a2a8d492...`.
This refresh is separate from the forty-package survey counts below.

Subscribe to both full AAE and Lite when Steam lists the other as a required
item. Load one selected gameplay mod at a time. AAE/UEM are not prerequisites
for most standalone custom maps.

## Shared missing-mesh fallback

Some localized XPaks list mesh references absent from their payload tables.
The existing path hook selects a complete sibling only when reference/name
metadata matches, all existing compressed payloads are identical and every
missing reference is supplied. Only missing mesh entries qualify. It does not
edit Workshop files, change game language or use a map-version allowlist.
Positive selections are cached against file identity, size and timestamps.

When whole-file substitution would change the selected language, the path hook
can create a derived pack under `BO3MacFix/MeshCache`. It retains the original
data, references and name metadata and adds only missing referenced mesh chunks
whose exact metadata matches the sibling. Common payloads must still agree.
The output is validated and installed atomically; donor/source changes or cache
corruption invalidate it. Failure leaves the original path in use. It requires
no user file copying and does not execute a Windows DLL.

The implemented C selector passed against these complete downloaded pairs,
independently verified against Steam manifest hashes:

| Package | Workshop ID | Missing mesh entries supplied |
| --- | --- | ---: |
| Call of the Dead | 2910206016 | 35 |
| Der Riese: Declassified | 2271346839 | 23 |
| Karma Rezort | 3313949094 | 43 |
| Vae Victis | 3064355026 | 23 |
| Super Mario 64 | 3242453013 | 18 |
| Reaper Collection Rebuilt | 3814067303 | 7 |
| Modern Warfare Zombies | 1535759989 | 18 |
| Wanted | 1663631538 | 32 |
| Europa | 2142193861 | 32 |
| Atonement | 1807713320 | 32 |
| Return to Das Herrenhaus (derived English pack) | 1617497358 | 4 |

Total: **267 entries across eleven package pairs**, not 267 independently observed
crashes. The five additional pairs pass the existing shared implementation;
no per-map patch was added. Silent Hill's examined English pack is complete
and correctly remains unchanged.

Herrenhaus's English pack lacks four referenced meshes and its German sibling
has different global reference/name metadata. The derived-pack check passed
against the actual pair: all 627 existing English payloads and all English
reference/name bytes were preserved, only four verified mesh chunks were added,
and both originals retained their hashes. Whole-file substitution remains refused.
The author's optional Intro Fix supplies cinematic audio, not these missing
meshes. It is a separate loaded mod and cannot simply be stacked with AAE/UEM.

The format was cross-checked against
[Greyhound's BO3 XPak structures](https://github.com/Scobalula/Greyhound/blob/22c3d17f7428573042b56f43af0ed2585989bebd/src/WraithXCOD/WraithXCOD/DBGameFiles.h).
An [independent Call of the Dead patch](https://github.com/josevelaz/bo3-custom-zombies-patch)
also identified missing meshes. The inherited stream-stall workaround remains
separate; it does not establish successful loading of a missing asset.

## Retained 1.2 API and save work

[Dead High](https://steamcommunity.com/sharedfiles/filedetails/?id=885119667),
manifest `6852605559916940411`, uses an older API family. Implemented: Files
Exists/IsDir/MakeDir/file-only Delete; both accepted Crypto save-key generations;
legacy Axios Get/Post/PostAndResponse, numbered PostQueued and consuming Result;
UI diagnostics and Steam ticket headers scoped to its own HTTPS stats origin.
Tickets wait for Steam's success callback, stay in memory, are not forwarded to
other origins and are cancelled on mod changes. Backend acceptance of the Mac
identity remains untested. The save codec matched eight bounded original-code
checks using substituted allocation/string services; no original initialization
or backend was run. Data offsets are tied to native SHA256
`9a115ad7b7cb20c01b5bcaec66777467f570171df0a5bdf2972f334c9f66214f`.

Raw Lua write/append/create operations prepare missing relative `players/`
parents. Reads and `r+` do not create folders; traversal paths do not trigger
preparation. Absolute POSIX paths stay absolute. Installation does not migrate
or rewrite saves. Test Dead High and Die Rise persistence after a full restart.

## Wider actual-file survey

The [machine-readable survey](map-survey-2026-10-28.json) records **40 packages,
742 selected verified files, 649 fastfiles, 2,593 Lua objects and 4,081 GSC/CSC
import inventories**. All extracted Lua objects now parse completely after the
research parser was corrected to accept legal Unicode/binary Lua strings and
basename-only assets. Earlier reports' three partial parses were parser
limitations, not demonstrated corruption. Direct namespace tracing is not a
proof of complete dynamic-call or gameplay coverage. Bulk media was not all
downloaded; the twelve localized XPak pairs were read completely.

The selection uses Steam subscriber, rating and recent/popular lists, plus
interesting newer packages. Subscribers are not verified players. For example,
Wanted had 400,728 current subscribers and 9,744 ratings; Modern Warfare Zombies
155,892/3,955; Europa 154,440/4,441; Solo Easter Eggs 143,538/2,029; Wonder Weapon
Pack 107,610/1,373; Zombie Plus 103,479/2,157. Counts are the 2026-10-28 snapshot
linked in each survey row, not permanent rankings. Lifetime and Expanded have
much smaller audiences and were included for interest/native behavior.

| Additional package | Actual files / practical finding |
| --- | --- |
| Reaper Collection | Older T7Overcharged DLL is byte-identical to Super Mario 64's analyzed DLL. No new direct helper API family recovered. Full native-feature parity remains unqualified. |
| Reaper Collection Rebuilt | No Windows PE in selected package files; seven missing mesh references are handled by the shared fallback. |
| Modern Warfare Zombies | No additional native library recovered; eighteen missing mesh references are handled. |
| Solo Easter Eggs, Zombie Plus, Wonder Weapon Pack, Rogue Run | No Windows native library in selected package files. No new shared native requirement established. Scripts, asset capacities and mod interactions still need runtime testing. |
| Wanted, Europa, Atonement | No additional native DLL recovered; the shared fallback handles 32 missing mesh entries per package. |
| Silent Hill Zombies, Lifetime | No Windows PE recovered. Silent Hill's examined localized pack is complete; Lifetime has no downloaded complete equivalent sibling. This is not gameplay qualification. |
| Reforged Public | Manifest `1130523989656347214`; native base DLL is identical to the earlier UEM reference (`a2a8d492...`). Shared FileIO/MainLUA paths are covered. Save & Quit, mutators and networking settings remain unqualified. |
| Workshop Stack | Ships `bo3_stack.dll`, a separate native loader/composition system. Not ported by this patch. Its script/clientfield/load-order merging and extra capacities require a separate port. Reforged explicitly checks for Stack to unload; that check is not a dependency. Do not use Stack in the initial cross-platform test. |

The survey also retains Call of the Dead, Der Riese Declassified, Dead High,
Karma Rezort, Vae Victis, Super Mario 64, Nuketown Legacy, Town Reimagined,
Die Rise, Leviathan, Nightmare, Rainy Death, Kowloon, Daybreak, Mob of the Dead
2024, FIVE, Water Park, Quiet Cosmos, Iron Dragon, Monkey Village and Last Vessel.
The absence of a separate DLL cannot rule out script, Havok, asset, memory,
platform engine or networking differences in those maps.

Nuketown Legacy's undefined-value/stutter cause remains unresolved; Town's
reported stutter has not been shown to share it. Do not coerce all undefined
values to zero or suppress script errors. A bounded Mac error trace is needed.

## Build and real test status

Use [the simple install/test instructions](MAC_TEST.md). Portable C/Lua checks,
Mac JSON/HTTP/save fixtures, compilation, installer recovery, dependency setup
and converter startup are build evidence. They are not gameplay evidence.
The current build run and published archive verification are recorded in the
release notes and profile. `ready_for_coop` and executable `verified` remain
false until real tests pass. The installer remains easy to update in place.

The clean runtime `dc98f6c0e6015ead6c5c930d8c3bfbce883ef376`
[passed portable, Intel and Apple Silicon jobs](https://github.com/supagood-oss/BO3MacFix/actions/runs/40781871389):
11 C programs, 27 Python/Lua checks, 754 synthetic machine cases, native
HTTP/settings/save/Steam callback fixtures, universal GUI/Intel patch compilation,
signing, installer recovery and automatic dependency/converter startup.
Both archives' four payload hashes and architectures were verified, and the
removed decoder symbols/metadata/export markers were absent. The published
archive's SHA256 is `d2b0586d2b7c7cce942d7e9e37bb2f699c59e5b329447240db6dd4ba90a923d0`.
The actual eleven repaired XPak pairs cover 267 missing mesh references;
Herrenhaus preserves its 627 original English payloads and metadata.
None of these checks is a physical M-series game or Windows co-op test.


### Local 1.5 checkpoint and mantle additions

The source adds six local checkpoint disk slots, menu dates, pointer-free camera
state and cross-session resume through the Mac engine's existing restart path.
It also adds mantle hint probes, warmup/latch timing and scoped HUD handling.
They are included in [Mac Test 1.5](TEST_1_5.md), and are absent from 1.4.
See [native evidence and limits](NATIVE_DEVELOPMENT.md#checkpoint-disk-persistence-and-mantle-hints-15-source).
Saves are Mac-specific; importing Windows AAE saves is unsupported. Test saving,
quitting BO3 completely, reopening and resuming with the same mod/map and player
capacity, with a Mac host and Windows guests. Then test the reverse hosting
arrangement using the Windows host's existing save implementation.
