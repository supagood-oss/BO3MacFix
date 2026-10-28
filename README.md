> Earlier public test downloads were withdrawn. Use the clean 1.4 test installer;
> do not reuse archives 1 through 1.3. Credential-dependent online features
> are disabled, while gameplay and ordinary HTTP support are retained.

# BO3MacFix

Fixes Black Ops 3 on Mac so custom maps from the Steam Workshop can be played and online matches can be played
with people running the Windows version of the game.

by Emma / InvoxiPlayGames and Anthony / serious.

## AAE/UEM development branch

This branch implements shared AAE/UEM native APIs and all eleven movement
options, with an experimental graphical installer for the first Mac test.
**Windows-Mac co-op is unverified.** The ordinary production release remains
blocked until that test passes. See [implementation status](docs/MOD_COMPATIBILITY.md)
and the [Mac test procedure](docs/MAC_TEST.md).

[Download the experimental Mac Test 1.4 installer](https://github.com/supagood-oss/BO3MacFix/releases/tag/aae-uem-mac-test-1.4).
It retains Expanded JSON settings, dvar flags and controller glyphs, with credential recovery removed. The shared missing-mesh repair covers eleven inspected
packages, including an English-preserving Herrenhaus cache; Dead High API/save support is retained. See [map support and remaining gaps](docs/MAP_SUPPORT.md).
Gameplay and Windows-Mac co-op remain unverified. AAE 4.0 ships a new native
plugin whose additional features are not yet ported; see the support record.

### Any issues you run into with custom maps or workshop mods should be reported here, and *not* to the mod's author!

## Install Guide: [Installing BO3MacFix](https://github.com/InvoxiPlayGames/BO3MacFix/wiki/Installing-BO3MacFix)

## FAQ: [Frequently Asked Questions](https://github.com/InvoxiPlayGames/BO3MacFix/wiki/Frequently-Asked-Questions-(FAQ))

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
