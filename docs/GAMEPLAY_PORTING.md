# Gameplay port evidence and test limits

This records offline findings from 2026-10-25. The target is ordinary Steam
BO3, based on the owner's description of the Windows friends' setup. None of
the findings qualify a live Mac hook or a Windows mod session.

## Windows reference capture

With the owner's permission, Steam launched the installed Windows game. Only
selected engine-code/constant ranges were read. The game exited normally after
the capture. No process memory was written, DLL injected, debugger attached,
mod loaded, or backend contacted by the analysis tools.

The Windows executable SHA256 was
`51ca63bbc660e0826943c60da67606f6bcb4b3b519528b5e0548c68c9423a323`,
Steam build `24784313`. Licensed inputs and the derived research image remain
outside this repository. The encrypted file on disk was insufficient for these
matches; the bounded initialized-code capture supplied the missing routines.

Several independently matched routines occur `0x6c0` below the AAE DLL's
hardcoded Windows RVAs. This is **not a general relocation rule**. The client
pause routine and low-address helpers did not have this shift. The pinned DLL
targets and the installed Windows layout differ; an actual working Windows
mod baseline remains necessary before claiming that the reference package is
qualified on this build. No conclusion that either mod is broken follows from
this static comparison alone.

## Implemented candidate bindings

All RVAs below refer to the exact Mac build in `MAC_BINDINGS.md`. They were
matched independently; Windows RVAs were not used as Mac addresses. Each hook
prefix/mutable-site byte sequence is checked by the prepatch builder.

| Behavior | Mac RVA / contract | Candidate implementation |
| --- | --- | --- |
| Client pause | `0x136b72b` | Live `hook_pausegame`; reject session mode 3 and demo playback. |
| Server pause | `0x155ae00`; paused predicate `0x155aec4` | Reject mode 3 and positive pending count, then call the real paused predicate. |
| Strafe | `0x1073e5d` | Original baseline approximately 0.667; live scale domain 0.667..0.97. |
| Omni sprint | `0x1143aa2` | Temporarily adjust pmove command axes/buttons around the original call, then restore them. |
| Console commands | add `0xf7651d`, find `0xf764e8`, thread context `0xd0604f` | Persistent 48-byte command records; command arguments from independently mapped nested thread args. |

The UEM pause helpers (`0xc2440`, `0xc240c` in its DLL) and the AAE helpers
(`0x1a0f04`, `0x1a0ed0`) independently implement the same decisions. Both use
`hook_pausegame`. A native ignored-return lobby query was omitted because its
matched engine routine reads state without changing pause state.

Supporting pause/settings bindings: session mode `0x127af60`, demo predicate
`0x12c1ab9`, pending wrapper `0xff6504`, underlying getter `0x102b646`, DVAR
lookup `0xf9ff7c`, value-to-string `0xfa04bc`.

The sprint argument is a pmove record: PS pointer +0, command time +8, buttons
+0xc, axes +0x40/+0x41. Remaining bytes, second argument and original input
values are preserved. Console thread context +0x18 points at args; nesting at
+0 (0..7), counts +0x64+4*nest, argv +0x88+8*nest. Invalid bounds are rejected.

## Eleven movement options

| Option | Matched Mac sites | Implemented behavior |
| --- | --- | --- |
| `slide_preserve_momentum` | slide acceleration `0x11488f0`, acceleration `0x1147ed9` | Reproduce the native velocity/flag branches. 96 finite-input cases match the isolated original AAE helper bit-for-bit. |
| `jumpslowdown` | penalty `0x14dcf3a`, apply `0x14dceb2`, jump `0x14dd05e`, severity `0x113caec`, cap `0x14dd2df`, landing boundaries `0x1146389`/`0x11469c3` | Neutralize penalty/severity; preserve original crouch decision; bypass horizontal cap only in the corresponding jump context; preserve XY and clear Z only after the matching landing entry. |
| `fast_sprintleap` | comparisons `0x14dd0c1`, `0x14dd200` | Threshold 1500 to 500 ms; preserve the ramp divisor of 1500. |
| `fast_sprintlerpin` | `0x1148644` | Mac 349/JG becomes 9/JG, equivalent to native Windows 350/JGE to 10/JGE. |
| `slideslowdown` | branch `0x11486b4`, friction load `0x113c51e` | Force the matched slide branch and select zero friction multiplier. See evidence caveat below. |
| `canslidethensprint` | `0x1143e22` | Bypass only the matched slide/sprint conditional branch. |
| `instant_slide` | eligibility `0xe8148b`, vector-yaw `0x10f1521`, angle-delta `0x10f39ba` | Reproduce original eligibility math without the 150 speed minimum. Other caller restrictions stay in the original engine path. |
| `fastswap` | timing `0xed9611`/`0xed9799`, animation `0xd86525`, presentation `0xd878a5` | Four Mac consumers correspond to three Windows logical sites; change 0.7 to native 0.4444 bits `0x3ee38866`. AAE later disables this option. |
| `fastads` | branches `0xed5e42`/`0xed5e56`, scale `0xed5e5c` | Remove both matched mode branches; replace doubling with live multiplier, default 2.15 and range 1..3 (AAE selects 1.4). |
| `fastjump` | interval `0x14dd451`, acceleration `0x1147ed9`, friction `0x113c32e`, constant `0x1073e6b`, airborne `0x113d293` | Interval 500 to 1 ms; caller-specific speed/acceleration adjustment; 67 ms per-player recent-air window; friction override only at return `0x113c500`, with original flag/speed guards. Air-acceleration caller return `0x1147411`. |
| `slideangles` | clamp `0x113e860`, view force `0x113d2f8`, caller return `0xe8225c` | Skip only limit-20 pitch clamp and the matching 500 ms slide pitch reaction; preserve other calls. |

The native jumpslowdown component also removes a penalty-state conditional
branch at initialization. The candidate mirrors this at `0x14dd155` while the
movement component is active, including when that particular option is off;
it restores normal behavior on component deactivation.

The slideslowdown native reference writes a RIP displacement selecting observed
Windows RVA `0x2f1f051`, which contained zero in the authorized capture. The
zero-friction interpretation follows that selected data target, not an
independent working Windows mod session. Step 4 must qualify this behavior.

The shared commands and live DVAR paths support individual perk toggles,
all-options changes, component gating and reset. Disabling/re-enabling
`hook_movement_perks` restores the chosen option state. Internal state uses
thread-local prediction context and a generation reset instead of sharing a
single current-pmove pointer across threads.

UEM registers the same movement commands/base settings. Its entire collection
of option callbacks has not been independently compared with AAE. The common
adapter is the candidate implementation, and UEM movement needs qualification
rather than an assumption of complete native DLL parity.

## Prepatch and ABI

Runtime changes write checked DATA slots, not translated game instructions.
Mutable compare/branch/float stubs preserve the relevant flags and original
paths. The three inline callbacks save all GPRs, flags, all 16 XMM registers
and the 128-byte red zone; the C callback receives a fixed 392-byte context.
Landing records are keyed to the game stack and player so a skipped entry
cannot restore another landing's velocity. Original entry fallbacks relocate
RIP-relative float loads and LEAs, and expand the existing short Jcc prefix.

The exact-hash builder now uses two verified empty TEXT padding arenas:
`0x1964080..0x1965000` and `0x1800..0x2100`. Load commands end at `0x17f8`;
the first section begins at `0x2100`. Data slots are in zero-fill BSS at
`0x1a268710..0x1a269000`, which are not file offsets. Overlap, range and byte
checks reject unsupported images. The builder fixture produces 47 hooks,
21 settings, two calls, 41 static patches and consumes 4,880/6,272 bytes.
All 47 entry stubs pass both redirect and fallback checks (94 cases).

## Unload and aim state

The by-name loader `0xe49c9a` takes `(int,const char*,bool)`; hooked core
`0xe49e88` takes `(int,const void*modRecord,bool)`. Unload respects
`disable_unloadmod`, checks the loaded name at `0x30878dc`, then calls by-name
with controller 0, empty name and reload enabled. The original core runs before
adapter deactivation. Movement, pause, CSC and presentation reset; HTTP jobs
are invalidated. The engine owns FX reset/lifetimes.
The script builtin is restored only if its current callback still belongs to
this adapter, preserving foreign changes.

Aim status hooks hold-breath hint `0x1130467` and publishes changes through
DVAR string setter `0xfa3386`. View weapon `0xed3bf3`, definition `0x12362cd`,
zoom-count `0x138606c` and zoom-level `0x138337e` determine base state. CG
breath spent/limit/recovery fields are +`0x2e77d0`/+`0x2e77d4`/+`0x2e77d8`;
frame duration is +`0x11a8a8`. These fields are independently used by Mac breath
update `0x3c960`. The portable tail preserves native signed-32 wrap behavior
for states 3/6/7/8/9; mod deactivation publishes zero.

**Not ported:** the native bullet-depletion model/bone/mesh visual effects.
Those are separate from aim status and the five CSC operations. This is an
explicit optional-feature limitation, along with Windows-only authoring,
hot reload and Discord integration.

## Evidence limits

Eleven C programs, nineteen Python/Lupa cases, 754 synthetic stub/bridge machine
cases, 133 selected actual-Mac-code cases, 94 actual-image generated entry
checks and 96 momentum differential cases have passed. The Mac SDK compile,
HTTP fixtures and installer/recovery fixtures have also passed on an Intel
macOS runner. Neither original mod DLL initialization nor backend requests
were performed by the research tools.

No live Mac gameplay hook, Rosetta game launch, mod/backend acceptance or
Windows-Mac co-op test has passed yet. The experimental activation gate is
separate from the production gate, which remains closed. Use
[MAC_TEST.md](MAC_TEST.md) for the first real session.

## Conversion helper process

Map conversion now launches Wine with literal arguments and a bounded child
wait instead of a shell command and a pipe. Output is a regular private temp
file, so background Wine services cannot block an EOF read. Failure/timeout is
reported instead of saying conversion completed. Injection settings are
removed only from the child; the game's environment remains intact.

The converter receives a short allowlist of runtime environment fields.
[WineHQ 60185](https://list.winehq.org/hyperkitty/list/wine-bugs@list.winehq.org/thread/6KTCKCQL6EJQ7KA4QK25I3ABBP4LEXWE/)
reports hangs with large environments on Wine 11. The regression fixture adds
12,000 bytes to the caller environment and runs the exact shared C wrapper.
The portable fixture checks literal spaces/metacharacters, environment
isolation, detached output handles, launch failure and child timeout. The
upstream converter's missing-input path also passed natively on Windows.
These checks do not qualify real map conversion.
