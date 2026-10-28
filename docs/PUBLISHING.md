# Publication and dependency boundaries

The owner created **supagood-oss** and authorized GitHub CLI for this project.
The public fork is [supagood-oss/BO3MacFix](https://github.com/supagood-oss/BO3MacFix),
branch `codex/aae-uem-compatibility`.
[Experimental Mac Test 1.1](https://github.com/supagood-oss/BO3MacFix/releases/tag/aae-uem-mac-test-1.1)
is published with a universal setup app. Both Mac runner architectures passed
build/helper checks; real gameplay and co-op remain unverified. The original authors, history and LGPL 2.1
license are preserved. A separate local CLI credential configuration is used;
business repository credentials were not changed.

The branch depends on PR #76 commit
`fcd8694a6fc65e8e50dbc06ceadeb6c91ac0be44`. No upstream PR has been opened for
this port. After Mac qualification, choose the PR base against the actual
upstream merge state so #76's work is not misrepresented as new changes.

## What is public

Source, tests, exact-hash profiles and sanitized evidence descriptions are
published. Mac Actions runs compile the Intel patch and universal GUI, test
HTTP and filesystem fixtures, and exercise automatic helper preparation.
CI never signs into Steam or contacts mod backends.

Never publish game executables, Workshop packages or native mod DLLs, raw
research images, decoded service values, credentials, account stores or private
logs. Licensed inputs and private analysis remain outside the checkout. Credential recovery and substitution are prohibited in published source and binaries.

## Test app and dependency handling

`--test-candidate` packages an explicit experimental app with `ready_for_coop`
false. `--development` produces diagnostics with Install/Play disabled.
Production packaging still requires live-verified profiles and no release
blockers. A test download must not be advertised as proven AAE/UEM support.

The small app contains only the built patch, launcher, setup GUI, manifest,
license and pinned dependency catalog. Clicking Install downloads original
archives directly from their publishers, verifies exact size/SHA256, extracts
into a fresh private directory, and installs the selected helpers. The app
fetches zone/cod2map/natives from the upstream 1.2-beta3 release and chip-specific Wine runtimes from
Sikarugir (10.0_6 for Apple Silicon; CX24.0.7_7 for Intel), with loose support
libraries from Template 1.0.11. The app chooses automatically, including when
Setup itself runs under Rosetta. It does not rebundle those third-party archives or upload
them as public build artifacts. This avoids manufacturing missing license
notices for the original vendor archives. Rebundling those components later requires
review of rights, notices and corresponding-source obligations first.

`tools/verify_dependency_archives.py` checks the pinned inputs for path/link
escapes, special entries, duplicate paths and expansion limits. Internal Wine
aliases are dereferenced into regular files, with the source boundary checked.
Only the loose .dylib support files are copied from the Template archive; no
renderer packs, Apple GPTK files, wrapper app or authoring SDK is installed.
The Mac CI downloader path is the same code used by the GUI. Both Intel and
Apple Silicon passed download/preparation and converter startup. The bounded
converter check reaches the patched missing-input path without game/map data;
it does not prove real Havok conversion or clean-machine Rosetta behavior.

Nested patch/launcher code is signed before payload hashes are generated;
then the outer app is signed and its deep signature checked. Current signing
is ad-hoc. There is **no Developer ID identity or Apple notarization** configured.
A smooth public Gatekeeper experience still needs signing, notarization,
stapling and a clean-machine install/update/repair/remove walkthrough.

Release notes must identify exact supported hashes/mod versions, actual tested
host/guest roles, optional-feature gaps and the experimental status. Only after
[step 4](MAC_TEST.md) passes may the live-verified profile and release claims
be updated. No token, signing key or notarization credential belongs in Git.
