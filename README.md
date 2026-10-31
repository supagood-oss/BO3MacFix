# BO3MacFix — Mac Test 1.5

A community test build for Steam Black Ops III on Mac, including AAE/UEM and
custom-map compatibility work. Original BO3MacFix by Emma / InvoxiPlayGames and
Anthony / serious.

**[Download the Mac Test 1.5 installer](https://github.com/supagood-oss/BO3MacFix/releases/download/aae-uem-mac-test-1.5/BO3MacFix-Mac-Test-1.5.zip)**

The same installer is for **Intel and M-series Macs**. It downloads the required
helpers automatically; no Homebrew, Terminal or manual game-file copying is
intended. Earlier downloads 1 through 1.3 were withdrawn; do not reuse them.

**This is an experimental first-Mac test candidate.** Intel and Apple Silicon
builds, installer/dependency checks and archive verification passed. Physical
Mac gameplay, checkpoint resume after quitting BO3, and Windows–Mac co-op have
not yet been verified. [Release and build evidence](https://github.com/supagood-oss/BO3MacFix/releases/tag/aae-uem-mac-test-1.5).

## Fresh installation

1. Install BO3 in Steam. Open it once, then quit.
2. Subscribe to [AAE](https://steamcommunity.com/sharedfiles/filedetails/?id=2631943123)
   or [UEM](https://steamcommunity.com/sharedfiles/filedetails/?id=2942053577).
   Accept the required Workshop items Steam lists and wait for all downloads.
3. Download the ZIP above, double-click it to unzip, and open **BO3MacFix Setup**.
4. Click **Install**. Leave Steam open and let Setup finish. On an M-series Mac,
   accept Apple's Rosetta prompt if shown.
5. Click **Play** in Setup.

## Already have BO3MacFix

1. Quit BO3. Download and unzip 1.5, then open the **new BO3MacFix Setup** app.
2. Click **Update**, or **Install** if that is the button shown. Keep the existing
   installation, saves and Workshop files in place.
3. Let Steam finish any mod/dependency updates, then click **Play** in Setup.

If macOS cannot verify the developer, after trying to open Setup use
**System Settings → Privacy & Security → Open Anyway**, then confirm **Open**.
The test app is ad-hoc signed and not notarized.
[Apple's instructions](https://support.apple.com/en-us/102445).

## Test with Windows friends

Windows players keep their usual working Steam installation; this installer is
only for the Mac players.

1. Everyone updates through Steam and loads the **same mod and map**. Test
   **AAE and UEM separately**, with one gameplay mod loaded at a time.
2. Try a short solo game on the Mac using a built-in Zombies map first.
3. Have a Windows friend host a private lobby and invite the Mac player. Play a
   few rounds and check movement, shooting, doors, effects and prompts.
4. Repeat with the **Mac hosting**, then repeat with the other mod. Try custom
   maps after the basic sessions work.
5. For AAE, check pause/unpause, camera transitions and mantle hints. A hint by
   itself must not move the player.
6. Use AAE's save-slot menu, note the round, quit BO3 completely, reopen it and
   resume the same slot with the same mod, map and lobby settings. Check the
   round, inventory, positions and cameras with Windows guests, then keep playing.
   Repeat with a Windows host using its normal AAE save support. Windows save
   files cannot be imported into the Mac checkpoint container.

If something fails, record **mod/map, who hosted, round, action and error**.
Use Setup's **Save Report** button. Restart the map after a failed restore.
Please report issues with this test build in this fork, not to the mod authors.
[Full test guide](https://github.com/supagood-oss/BO3MacFix/blob/codex/native-test-1.5/docs/TEST_1_5.md)
· [Shareable offline guide](https://github.com/supagood-oss/BO3MacFix/releases/download/aae-uem-mac-test-1.5/BO3MacFix-1.5-START-HERE.html).

## Does disabling credential-dependent online features affect co-op?

**It does not disable Steam login, Steam friends, lobby networking or the game's
multiplayer transport.** The removed helpers recovered third-party service
credentials for mod integrations. In the inspected AAE package, their guarded
call sites handle Discord report/chat forwarding and related Steam Web API
profile/Workshop lookups. Those integrations are unavailable; the mod checks
whether the helpers exist before calling them.

Ordinary mod HTTP requests, Steam client identity and the gameplay adapters are
retained. UEM's ordinary HTTP/cloud API has not been disabled by this removal,
but actual backend acceptance and cloud progress still need testing. Private
Steam co-op is therefore expected to remain possible; this is a code-path
assessment, not a successful Windows–Mac gameplay test. Use Steam friends/private
lobby invitations for the first test. AAE's new LAN/System Link browser is a
separate unported feature.

## Current mod versions and 1.5 changes

On **October 31, 2026**, Steam's current Workshop content IDs matched the exact
packages already inspected for 1.5: **AAE 4.0.0**, **AAE Lite 4.0.0**,
**UEM 1.3.4 Build-001** and **Expanded v4.62**. The published
[AAE change notes](https://steamcommunity.com/sharedfiles/filedetails/changelog/2631943123)
and [UEM change notes](https://steamcommunity.com/sharedfiles/filedetails/changelog/2942053577)
show no newer update than those packages. This is a package match, not a claim
that every option or future update works.

1.5 includes the newer checkpoint disk slots/date menus/camera persistence,
mantle hints, co-op pause, free aim, interactive prompts, traversal and migration
adapters. Shared asset fixes, Expanded settings and Dead High save/API support
are retained. Some optional native features remain outside this candidate,
including LAN browsing and Windows-specific authoring/cursor/debug options.

Developers: the 1.5 runtime source is revision
`f12af1dbcc90f953798a750133dde10a5d94fe01` on the
[test branch](https://github.com/supagood-oss/BO3MacFix/tree/codex/native-test-1.5).
See its [native implementation evidence and limits](https://github.com/supagood-oss/BO3MacFix/blob/codex/native-test-1.5/docs/NATIVE_DEVELOPMENT.md).
The upstream [installation wiki](https://github.com/InvoxiPlayGames/BO3MacFix/wiki/Installing-BO3MacFix)
and [FAQ](https://github.com/InvoxiPlayGames/BO3MacFix/wiki/Frequently-Asked-Questions-(FAQ))
remain available for background on the original project.

## Settings

BO3MacFix reads `BO3MacFix/settings.ini` in the game's folder when the game starts, one `Name = value` per line
(lines starting with `//` are comments). All of them are optional.

| Setting | Default | What it does |
| --- | --- | --- |
| `PrePatchedExe` | 1 | Starts a copy of the game with BO3MacFix's changes already made in the file (`CoDBlkOps3_Exe.bo3macfix`, remade after game or BO3MacFix updates), so Rosetta runs it from its ahead-of-time translation instead of its slower JIT. 0 starts the original. |
| `JobThreads` | 2 | How many job worker threads the game uses, 2 to 4. |
| `ThreadQoS` | 1 | Runs the game's main, render and job threads at the highest scheduling priority. |
| `JobIdleFix` | 1 | Lets idle job threads sleep instead of spinning. |
| `YieldSleep0` | 0 | Makes `Sleep(0)` yield like on Windows. |
| `FramesInFlight` | 2 | How many frames the CPU may queue ahead of the GPU, 1 to 3 (`MaxFrameLatency` in config.ini does nothing on Mac). |
| `FramePacingFPS` | 0 | Keeps each frame on screen for at least 1/N of a second, 30 to 240, for even pacing below the refresh rate. |
| `MetalFXUpscale` | 0 | Upscales with MetalFX when the game's resolution is lower than the screen's. Needs macOS 13. |
| `ControllerMappings` | 1 | Lets DualShock 4, DualSense and DualSense Edge controllers work over USB. |
| `ControllerBridge` | 0 | Reads DualShock 4 and DualSense controllers through macOS instead: Bluetooth and USB, rumble and lightbar. Turn Steam Input off for the game. Needs macOS 11.3. |
| `ButtonPrompts` | 0 | 0 shows PlayStation buttons with a PlayStation controller, 1 always Xbox, 2 always PlayStation. |
| `LightbarColor` | -1 | The lightbar colour as RRGGBB, with `ControllerBridge`. -1 leaves it alone. |
| `NoCodePatches` | 0 | For testing only: leaves the game's code exactly as shipped, without any of the fixes. Solo play only. |

Most can also be set with an environment variable, `BO3MACFIX_` and the name in capitals. When the game is started
from Steam, BO3MacFix's log goes to `BO3MacFix/log.txt` and the launcher's to `BO3MacFix/launcher.log`.

## Credits

- [Alyssa / alltheamps](https://github.com/shiversoftdev) - guidance, help, t7patch and the cod2map64 patch DLL.
- [The Dobby project](https://github.com/jmpews/Dobby) - code hooking on macOS. (used under the MIT license)
- [Mickael DE LA ROQUE / mickadlr](https://github.com/mickadlr/BO3MacFix) - the custom map stream-stall workaround.

## What does it do? (Nerd stuff)

### Custom Maps

There are two big issues with Steam Workshop map loading in the current (v98, from 2019) Mac build of BO3:

* Texture (.png) conversion crashes the game when hovering or selecting a workshop map
* Havok navmesh/navvolume files are a different format across PC and Mac

This project hooks into the game to solve both these problems. It solves the texture problem by stubbing out
the faulty function, and solves the navmesh problem by converting it on-the-fly using a modded version of the
modding SDK. We call Wine from within the native Mac game process to call upon the modding SDK, after saving
a snapshot of the Havok navmesh/navvolume data from the map, and the patch to the SDK's cod2map tool takes
that input and outputs in the Mac-encoded format.

A campaign mission with broken navigation meshes is also fixed in the same way.

### Online Play

A macOS update updated the version of libcurl that was included with the OS, which makes online login fail
in Black Ops 3 due to improper usage of the cURL library that's technically "undefined behaviour". This
is fixed here by removing one of the HTTP headers the game manually sets and letting cURL do its thing.

Version numbers are spoofed when signing in to match the Windows version's to enable matchmaking between the
two, along with using updated scripts and assets from the Windows version.
