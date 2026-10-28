> Earlier public test downloads were withdrawn. The clean installer is being
> rebuilt; do not reuse an earlier archive. Credential-dependent online features
> are disabled, while gameplay and ordinary HTTP support are retained.

# AAE/UEM Mac test candidate

Mac Test 1.4 adds [Expanded settings/flags/glyphs with credential recovery removed](MAP_SUPPORT.md).
The wider survey covers 40 actual packages and ten localized missing-mesh fixes.
**Current AAE 4.0 adds AAEUnified.dll: its extra native features are not a completed
port.** The original shared adapters below are not full current-plugin parity.
See the [simple fresh/update installation and test instructions](MAC_TEST.md).
Physical Mac gameplay and Windows co-op remain pending.

The required shared adapters and all eleven movement options are implemented.
The Intel Mac dylib and universal graphical installer compile on the macOS
runner. This is an **experimental candidate for the first Mac test**, not a
claim that AAE/UEM co-op works. Step 4 has not run.

The branch starts at BO3MacFix PR #76 commit
`fcd8694a6fc65e8e50dbc06ceadeb6c91ac0be44`. Original authors and licensing are
preserved. The public fork is [supagood-oss/BO3MacFix](https://github.com/supagood-oss/BO3MacFix/tree/codex/aae-uem-compatibility).

## Requested steps

| Step | Implementation / evidence | Remaining qualification |
| --- | --- | --- |
| 1. Shared APIs and pools | File I/O; named HTTP GET/POST/poll; Foundation transport; stable Mac identity; asset pools; generation cancellation | Real mod backend/identity acceptance and HavokScript integration |
| 2. Native mapping | Exact licensed Mac hash/UUID; FX, VM, movement, pause, lifecycle and aim-state bindings; 133 selected actual-code cases; all 47 generated entry hooks pass fallback/dispatch checks | Live layouts, thread context and Rosetta behavior |
| 3. Port and installer | All eleven movement options, live commands/DVARs, strafe/omni, pause, FX cache, five CSC operations, unload/reset, aim status; Mac compilation, HTTP/installer fixtures, automatic downloads and converter startup pass on both Intel and Apple Silicon | Real Mac install/launch and gameplay exercise |
| 4. Windows-Mac co-op | Not run | Test both host/guest roles, both mods, map changes and unload/reload |

The ordinary build keeps `MainLUA.Revision()` at 0. An explicit
`DEV_MOD_HOOKS=1 EXPERIMENTAL_MOD_TEST=1` build returns revision 651 **only after
all six required engine adapter groups activate** (FX, CSC, movement, pause,
unload and aim state). This is a source activation gate, not proof of co-op.
`ready_for_coop` and each build's `verified` remain false. Production packaging
still rejects this profile; `--test-candidate` is a separate first-test route.

## Shared native work

The layer reproduces the required APIs; it does not execute the original
Windows DLL initialization. FX uses two 2048-record client caches, independently
of DB pool sizes, covering both registration paths, lookup, unregister/reset.
The CSC bridge preserves zero-argument IsProfileBuild and implements the five
AAE hash/view-origin/animation operations. Pause respects native session/demo/
pending-work restrictions. Unload uses the correct by-name loader and resets
movement, pause, CSC, aim state, HTTP generations.

All eleven movement settings have Mac counterparts with per-option changes,
`movement_patch`, `movement_patch_all`, live DVAR updates and mod-switch reset.
Timing, float loads and branches use prepatched dispatch into mutable DATA;
inline landing callbacks preserve flags, registers, SIMD and the red zone.
See [gameplay bindings and caveats](GAMEPLAY_PORTING.md) and
[VM/FX bindings](MAC_BINDINGS.md).

AAE's Encrypted APIs substitute four selectors. Their values are decoded only
in RAM from the player's own unchanged, exact-hash Workshop DLL. They are not
published, logged or saved in source. URL/body selectors are separate. JSON
POST matches the native contract; missing/invalid selectors fail closed.

Optional bullet-depletion model/bone visuals have not been ported. The
`aim_status` producer is implemented, but does not reproduce those effects.
Windows authoring/hot-reload/Discord features are also outside this candidate.
UEM's pause policy and shared API/settings were compared; not every option of
its native movement callbacks has an independent whole-DLL parity proof.
The captured Windows engine layout also differs from some pinned DLL addresses;
a working ordinary-Steam Windows baseline must be checked during step 4.

## Checks actually completed

- Eleven portable C programs pass: core, codegen, payload, FX, CSC, pause,
  movement, lifecycle, presentation, converter process handling and stream recovery.
- Nineteen Python/Lupa tests pass, including packaging and candidate/production
  gates. Linux CI passes the complete portable target.
- 754 synthetic machine-code checks cover emitted stubs and the assembled
  SysV inline bridges (registers, flags, SIMD, stack/red-zone preservation).
- 133 bounded actual-Mac-code cases passed for selected VM/FX/animation/pause/
  strafe routines. No original game or OS initialization was executed.
- The private extracted prepatch builder accepts the pinned Mac image:
  47 hooks, 21 settings, 2 calls, 41 static patches; 4,880/6,272 padding bytes.
  All 47 entry hooks pass redirect and fallback checks: 94 cases. Platform
  hash/permission functions are substituted only in this private fixture.
- The isolated original AAE momentum helper and the portable replacement agree
  bit-for-bit in 96 finite-input differential cases.
- macOS CI passed full Intel dylib/universal app compilation, Foundation HTTP
  fixtures, installer update/remove/rollback/recovery, internal-link copying,
  external-link rejection and verification of downloaded helpers after restart.
- The pinned vendor archives were hash/size checked and inspected without
  execution: 25 upstream entries, 5,174 Apple Silicon Wine entries, 5,131 Intel Wine entries and 1,074 support-template entries,
  no escaping links, special files or duplicated paths.

The automatic download/converter startup integration passed on both Mac runner
architectures. A missing-input converter check does not qualify real Havok conversion. GUI visual review, power-loss durability,
real external drives, clean-machine Rosetta, real mod/backend sessions and
Windows-Mac co-op remain untested.

## Player installation

The intended test flow is:

1. Download and open **BO3MacFix Setup**.
2. Click **Install**. The app finds Steam's game (or offers a folder picker),
   downloads about 340 MB of exact-hash helpers, and installs them automatically.
3. Keep Steam open and click **Play** in the app.

No Homebrew, Python, compilation, Terminal, launch-option editing or manual
copying is required. Allow roughly 4 GB free space for helper preparation and
recovery copies, plus the game's own conversion cache. Wine is private to this
installation. Helpers come directly from the original publishers; they are not
rebundled in the small app. First install requires internet access.

The test app is **ad-hoc signed, not Apple notarized**. macOS may require
Privacy & Security > Open Anyway on first launch. Developer ID signing and
notarization are still needed for a polished public installation experience.
Intel and Apple Silicon run the Intel game; Apple Silicon needs Rosetta. Play checks for it and opens Apple's standard
installation prompt through an Intel helper if needed; no Terminal command or
silent license acceptance is used. The prompt still needs a clean-Mac walkthrough. The universal installer targets macOS 10.15 or later;
that minimum has not been exercised on an old OS.

Install stages backups and a journal before game-directory writes. An
interrupted transaction offers **Repair**. **Remove** restores owned files and
preserves saves, Workshop downloads, settings, Physics cache and Wine prefix.
Unexpected file edits are kept for review. **Save Report** omits account IDs,
hardware IDs, URLs, request bodies and logs. Update restores missing owned helpers from the verified download. Play verifies
every installed helper in the background, including after reopening the small
installer app. See the [pre-test review](FINAL_REVIEW.md) for the retained checks.

## Developer build

```sh
python3 -m venv build/checks-env
build/checks-env/bin/pip install -r requirements-dev.txt
make test PYTHON=build/checks-env/bin/python
make test-macos
make -C installer test
make -j2 DEV_MOD_HOOKS=1 EXPERIMENTAL_MOD_TEST=1
make -C launcher
make -C installer gui
```

Sign the dylib and launcher before generating payload hashes. Then use
`tools/package_setup.py --test-candidate --output 'build/BO3MacFix Setup.app'`,
sign the outer app, verify it and archive with ditto. The Actions workflow does
this and exercises the same automatic downloader as the GUI. End users do not
need any of these commands. Production packaging requires live qualification;
diagnostics use `--development` and keep Install/Play disabled.

[Mac test procedure](MAC_TEST.md) · [Publication and dependency boundaries](PUBLISHING.md)
