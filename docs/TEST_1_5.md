# Mac Test 1.5 — install and test

[Download Mac Test 1.5](https://github.com/supagood-oss/BO3MacFix/releases/download/aae-uem-mac-test-1.5/BO3MacFix-Mac-Test-1.5.zip) · [Shareable offline guide](https://github.com/supagood-oss/BO3MacFix/releases/download/aae-uem-mac-test-1.5/BO3MacFix-1.5-START-HERE.html) · [Release](https://github.com/supagood-oss/BO3MacFix/releases/tag/aae-uem-mac-test-1.5)

The same download supports Intel and M-series Macs. Runtime revision
`f12af1dbcc90f953798a750133dde10a5d94fe01` passed the
[Intel and Apple Silicon CI checks](https://github.com/supagood-oss/BO3MacFix/actions/runs/40822820940).
Both archives were inspected and the public download was independently re-read
and hash-verified. ZIP SHA256:
`fa5d50afa5b74e0d07b4af9aac4c87388808c5482ae3cc8230237d923ca5eb0d`.

This is a first-Mac gameplay test candidate, not qualified AAE/UEM co-op support.

## Fresh BO3 installation

1. Install BO3 in Steam. Open it once, then quit.
2. Subscribe to AAE or UEM and the dependencies Steam lists. Wait for downloads.
3. Unzip the 1.5 download and open **BO3MacFix Setup**.
4. Click **Install**, accept Apple's Rosetta prompt on an M-series Mac if shown,
   and let the app finish.
5. Click **Play** in Setup.

## Already have BO3MacFix

1. Quit BO3. Open the supplied new Setup app.
2. Click **Update** (or **Install** if that is the button shown).
3. When it finishes, click **Play**. Keep your existing saves and Workshop files.

## Test with Windows friends

1. Everyone updates through Steam and loads the same mod/map. Windows friends
   use their usual working Steam installation. Test AAE and UEM separately.
2. Try a short solo game on a built-in Zombies map first.
3. A Windows friend hosts a private lobby and invites the Mac. Play several
   rounds; check movement, shooting, prompts, doors, effects and pause/unpause.
4. Repeat with the Mac hosting. Try custom maps after the basic session works.
5. For AAE, use the mod's save-slot menu. Save, note the round, quit BO3
   completely, reopen it, and resume the same slot with the same mod/map and
   player capacity. First use a Mac host with Windows guests. Check the round,
   inventory, player positions, camera transitions and continued play. Then
   repeat with a Windows host using that host's normal AAE save support.
6. Check mantle hints at low/high ledges, then actually mantle/vault. Hint-only
   probes must not move the player. Check both host and guest views.
7. On a failure, record who hosted, mod/map, round, action and visible error.
   Use Setup's **Save Report** button. Checkpoint files remain under
   `~/Library/Application Support/BO3MacFix/AAESavegames`; do not move Windows
   save files into that folder.

The installer is intended to need no Terminal, Homebrew or manual file copying.
Live gameplay, cross-session engine restoration and Windows co-op are tests,
not verified compatibility claims.


If macOS cannot verify the developer, after trying to open the app use
**System Settings → Privacy & Security → Open Anyway**, then confirm **Open**.
This app is ad-hoc signed and not notarized. [Apple instructions](https://support.apple.com/en-us/102445).
