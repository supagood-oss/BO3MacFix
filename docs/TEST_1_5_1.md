# Mac Test 1.5.1 — install and test

[Download the installer](https://github.com/supagood-oss/BO3MacFix/releases/download/aae-uem-mac-test-1.5.1/BO3MacFix-Mac-Test-1.5.1.zip)
· [Release notes](https://github.com/supagood-oss/BO3MacFix/releases/tag/aae-uem-mac-test-1.5.1)

For Intel and M-series Macs. This fixes an installation failure reproduced on
both Mac build runners: the helper file list contained incorrect paths after
macOS resolved its temporary-folder aliases. The AAE/UEM gameplay code is
unchanged from 1.5. Windows–Mac gameplay testing is still needed.

## Fresh BO3 install

1. Install BO3 through Steam. Open it once, then quit.
2. Download the ZIP above, unzip it, and open **BO3MacFix Setup**.
3. Click **Install** and wait for the helper downloads to finish.
4. Subscribe to AAE **or** UEM and the required Workshop items Steam lists.
   Let all downloads finish. You can do this before installing the fix too.
5. Leave Steam open and click **Play** in Setup. Accept Apple's Rosetta prompt
   on an M-series Mac if shown.

## Already have BO3MacFix, or 1.5 failed to install

1. Quit BO3. Download and unzip **1.5.1**, then open the newly downloaded Setup.
2. Click **Update**, or **Install** if shown. If it says **Repair**, click that
   first, then Install/Update.
3. Keep your existing MacFix files, saves and Workshop downloads in place.
4. Once Setup and Steam's Workshop downloads finish, click **Play** in Setup.

If macOS blocks opening Setup, first try opening it, then use **System Settings
→ Privacy & Security → Open Anyway** and confirm Open. The app is ad-hoc signed,
not notarized. [Apple's instructions](https://support.apple.com/en-us/102445).

## Test with Windows friends

Windows friends use their normal working Steam installation; they do not install
this Mac download. Everyone needs the same mod and map versions.

1. Test **AAE and UEM separately**. Load one gameplay mod at a time.
2. First try a short solo game on the Mac using a built-in Zombies map.
3. Have a Windows friend host a private lobby and invite the Mac through Steam.
   Play a few rounds and check movement, shooting, doors and effects.
4. Repeat with the Mac hosting. Then test the other mod and any custom maps.
5. For AAE, also check co-op pause/unpause and checkpoint save, full game exit,
   reopen and restore. Use the same map, mod and lobby settings. Windows saves
   cannot be imported into the Mac checkpoint files.

If Setup fails, click **Save Report** and send that file with the error message.
The report now includes the failed operation and helper/error codes, without raw
logs, account IDs or request URLs. If gameplay fails, also note the mod/map,
host, round and action that triggered it. Restart the map after a failed restore.

## Build evidence

[Intel, Apple Silicon and portable checks](https://github.com/supagood-oss/BO3MacFix/actions/runs/43119404342)
run against source `ac2a7813150d51af00466d1ff9e5c49860cfaf68`.
The Mac checks include full helper downloads, converter startup and complete
installation, repeat update, missing-file repair and removal with synthetic
fresh/legacy game folders. They do not establish real-game co-op compatibility.
