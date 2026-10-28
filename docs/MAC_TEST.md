> Earlier public test downloads were withdrawn. The clean installer is being
> rebuilt; do not reuse an earlier archive. Credential-dependent online features
> are disabled, while gameplay and ordinary HTTP support are retained.

# First Mac test

This is an experimental compatibility build. Source implementation and build
checks do not prove Windows-Mac co-op. Use matching Steam Workshop versions:
[AAE](https://steamcommunity.com/sharedfiles/filedetails/?id=2631943123),
[UEM](https://steamcommunity.com/sharedfiles/filedetails/?id=2942053577), and any maps required
by the chosen mod. AAE and UEM are tested separately, one loaded mod at a time.
Let Steam finish Workshop updates on every player's computer. The installer
checks the supported Mac executable automatically. For current AAE, subscribe
to both full AAE and Lite if Steam lists the other as required; load only your
chosen one. Native data decoders check
their DLLs automatically; players do not need to calculate hashes.

## For the Mac player

1. Download [**BO3MacFix-Mac-Test-1.4.zip**](https://github.com/supagood-oss/BO3MacFix/releases/download/aae-uem-mac-test-1.4/BO3MacFix-Mac-Test-1.4.zip), unzip it and open **BO3MacFix Setup**.
   Move the app somewhere you will keep it, such as Applications.
2. If macOS blocks the unnotarized test app, use System Settings > Privacy &
   Security > Open Anyway, then confirm Open. Do not change global security
   settings. The first test app uses ad-hoc signing.
3. Leave Steam open. Close Black Ops III and click **Install** in Setup.
   Allow about 340 MB of downloads and roughly 4 GB free space for setup.
   The app normally finds the game; otherwise choose its folder. An existing
   BO3MacFix installation can use Install or Update without uninstalling first.
4. Click **Play** in Setup. Continue to launch using this button during the test.
   Workshop subscriptions/downloads are still managed by Steam.
5. Start with one short solo session, then join the agreed Windows friend's
   session. Send the exact visible error if anything fails, and use **Save
   Report**. Do not send passwords, codes, account IDs or full game logs.

**Fresh BO3 install:** open the ordinary game once from Steam, then close it
and follow the steps above. Subscribe to the test map in Steam and wait for its
download before Play.

**Already have BO3MacFix:** close the game, open this new Setup app, and click
Update (or Install if that is the button shown). Keep the old installation in
place. Then launch with this new app's Play button. Saves and Workshop downloads
are preserved.

## Simple map test with Windows friends

1. Everyone subscribes to the same map in Steam and lets it finish downloading.
   Windows players use their usual working BO3 setup. They do not install this
   Mac app. First make sure the map works for them on Windows.
2. Start with **no extra gameplay mod loaded**. The Windows friend hosts a
   private Zombies lobby and invites the Mac player through Steam.
3. Try **Call of the Dead** first. Play past round 1 and through several rounds,
   checking zombie models, weapons, doors and effects. Then try Der Riese
   Declassified, Karma Rezort, Vae Victis and Super Mario 64 separately.
   The same mesh fix also covers Wanted, Europa, Atonement, Modern Warfare
   Zombies and Reaper Collection Rebuilt.
4. Try **Dead High separately**. Check that its menus load, play a short game,
   return to the menu, quit BO3 completely, reopen using Setup's Play, and check
   that earned progress is still there. Test Die Rise's bank/fridge the same way.
   Existing progress should remain; report an unexpected reset immediately.
5. Repeat a successful map with the Mac hosting. Then test **AAE and UEM one at
   a time**, with the same mod loaded on every computer. Do not stack them or
   load either on Dead High for the initial test.
6. If something fails, note the map, loaded mod, who hosted, round and exact
   visible error. Use Setup's **Save Report** and share that with the developer.

The [map support record](MAP_SUPPORT.md) explains the fixes and remaining gaps.
Nuketown Legacy/Town stutter, Herrenhaus's differing asset packs, Workshop
Stack and full AAE 4.0 AAEUnified feature parity remain unresolved. Expanded's
startup/settings fixes are included; optional cursor/lean/crosshair and
Windows-only experiments are not a complete native port. AAE and UEM are optional gameplay mods, not a requirement
for most standalone maps.

Apple Silicon needs Rosetta for the Intel game. If it is missing, Play opens
Apple's standard installation prompt; choose Install and complete its steps.
Actual installation, Rosetta,
launch and conversion are being qualified in this test. The game executable
must match SHA256
`932f0918fb9871bc625b69a587135a5c2976e9f9e6b7289cfadda8ab996e1cd0`.
The installer checks it automatically; players do not need to calculate hashes.

**Remove** restores this installer's owned files; saves, Workshop downloads,
settings and Wine data are preserved. **Repair** appears after an interrupted
transaction. Update also restores missing installed helpers. An unexpected
edited file is preserved for review. Play checks the installed helpers in the
background and shows a checking message while the window stays responsive.

## Qualification with the developer

Proceed through these gates in order; record pass/fail, Mac model/chip, macOS,
mod/map version and the exact symptom. No engine address entry is required from
the player. A failure at one gate is debugged before expanding the test.

| Gate | Exercise | Evidence needed |
| --- | --- | --- |
| Install / restore | Fresh install, reopen Setup, Play enabled; Remove; reinstall | No manual file copying, no Homebrew, original launcher kept; helpers verify after app restart |
| Base game | Launch to menu, solo Zombies load and exit | No crash, map conversion completes, normal movement/FX |
| AAE solo | Load AAE and one map, exercise menu/perks, movement, ADS/swap, jumps/slides, animations, aim states and FX-heavy play | Native API initialization succeeds; backend menus progress; no script/runtime errors |
| UEM solo | Load UEM, verify revision/identity-dependent startup, menus, pause/unpause and FX-heavy play | No compatibility rejection, backend errors or hang |
| Mod lifecycle | Return to menu, unload, load the other mod, change map, repeat | No stale HTTP results, tokens, movement toggles, script callbacks or aim state |
| Windows host | Mac joins ordinary-Steam Windows host; test AAE then UEM separately | Matching mod/map versions; session starts, movement/perks/FX work, no desync/disconnect |
| Mac host | Windows joins Mac host, same two separate mod tests | Same checks in the other direction, including pause/restart and map transition |

The Windows friends' actual working setup must be confirmed: ordinary Steam,
BOIII or another patch, exact mod versions and baseline behavior. Several
captured engine routines differ in address from the pinned DLL targets; no
working Windows mod session has yet qualified that baseline.

For developer movement checks, exercise each of the eleven options both off
and on, with `hook_movement_perks` disabled/re-enabled, individual AAE perk
toggles, `movement_patch_all`, and a mod switch. Pay special attention to
slideslowdown's zero-friction interpretation, instant slide at low/zero speed,
crouched jumping, landing XY/Z, the 67 ms fastjump window, 9/JG sprint lerp,
fastswap's four Mac consumers, live ADS scale and slide pitch/clamp exceptions.
AAE normally disables fastswap after enabling the patch family.

Optional bullet-depletion model/bone visuals, Windows authoring/hot reload
and Discord integration are not ported. Current AAE 4.0 also ships a new native
plugin; its extra free-aim, traversal, host-migration, co-op-pause and larger-memory
features are a separate remaining port. Do not use those features as evidence
that ordinary startup or the original shared hooks passed. Their absence must not be reported as
full native-feature parity. Live tests may expose further required fixes;
record them before marking any profile `verified` or `ready_for_coop`.
