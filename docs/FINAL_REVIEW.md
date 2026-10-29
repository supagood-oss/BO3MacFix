> [Mac Test 1.5 is now available](TEST_1_5.md), with checkpoint disk persistence,
> mantle hints and the newer native adapters. Intel/Apple Silicon builds and
> archive checks passed; physical Mac gameplay and Windows co-op remain unqualified.
> The review below records the earlier 1.4 installer baseline.

> Earlier public test downloads were withdrawn. Use the clean 1.4 test installer;
> do not reuse archives 1 through 1.3. Credential-dependent online features
> are disabled, while gameplay and ordinary HTTP support are retained.

# Pre-test complexity review

**Current candidate: [Mac Test 1.4](https://github.com/supagood-oss/BO3MacFix/releases/tag/aae-uem-mac-test-1.4).**
Expanded's settings, dvar flags and glyph mapping are implemented. Ordinary HTTP
response names are retained; embedded-service credential recovery and encrypted
HTTP exports have been removed. The mesh repair passes eleven actual pairs,
including a derived Herrenhaus pack preserving all original English content.
See [the support record](MAP_SUPPORT.md).
Exact hashes select save-codec layouts or engine patch addresses;
there is no general displayed-mod-version allowlist. Current AAE's new plugin
remains a separate feature port. Gameplay and Windows co-op remain unqualified.

The installer review below describes the retained 1.1 improvements.

The review before the first physical Mac test found two installer problems,
corrected in Mac Test 1.1:

- Update rejected a missing installed helper even though Play instructed the
  user to update to repair it. Update now restores missing files from its
  verified payload, retaining the original uninstall backups. Existing edited
  files remain preserved. A missing helper without a supplied replacement is
  still refused. Fixtures cover repair, failed repair rollback and saved edits.
- Every window refresh verified the entire downloaded runtime on the UI
  thread, and Play repeated the work. Full verification now runs once per Play
  request on a worker queue with a visible checking message. Rosetta completion
  continues directly without repeating that full scan. Core payload/game checks
  remain; the expensive helper scan no longer runs for routine window refreshes.

No game hooks, movement algorithms, API revision values or dependency runtimes
were changed by this review. The remaining restrictions have concrete purposes:

| Check | Reason it remains |
| --- | --- |
| Exact original Mac executable hash and hook bytes | Native functions, field layouts and generated patches use addresses from that executable. Allowing another binary would make those addresses unsafe. This is not a Mac chip/model restriction. |
| Helper archive size/hash and chip-specific Wine builds | Verify the actual downloaded files. Intel and Apple Silicon passed startup checks with different runtimes; earlier alternatives failed those checks. |
| Owned-file edits and recovery backup verification | Avoid losing existing edits or restoring an unverified backup. Missing replaceable files can now be repaired. |
| Experimental release status | Build checks do not establish gameplay or Windows-Mac co-op. It does not disable Install or Play in this test candidate. |

There is no general runtime allowlist of AAE/UEM displayed version labels, and
UEM does not have an added exact-DLL-hash rejection. Its requested native API
revision is advertised only after the implemented engine adapter groups are
available. Matching mod/map versions between players is still required by the
mods/network game; the listed profiles record the packages analyzed, rather
than claiming future builds have passed testing.

The app remains ad-hoc signed. Actual launch, map conversion, mod backend
acceptance, game integration and co-op still require the physical Mac test.

Mac Test 1.4 [checks passed on both Intel and Apple Silicon](https://github.com/supagood-oss/BO3MacFix/actions/runs/40781871389), including recovery fixtures, compilation, automatic downloads and converter startup. The universal GUI, Intel game patch and all four payload hashes were inspected in both generated archives. The credential decoder and encrypted helper markers are absent. Physical Mac gameplay and Windows co-op still need the first test.
