#!/usr/bin/env python3
"""Bundle the graphical Mac installer; never install into a game directory.

Production packaging requires co-op qualification. --test-candidate enables the
explicit first-Mac-test build; --development disables Install/Play. No signing,
notarization or gameplay test is implied by this tool.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import shutil

REPO = Path(__file__).resolve().parents[1]
REQUIRED = {"fileio", "http", "identity", "mod_unload", "movement", "csc_bridge", "client_fx", "pause", "aim_status"}


class NotReady(ValueError):
    pass


def qualified_builds(profile: dict) -> list[str]:
    if not profile.get("ready_for_coop") or profile.get("release_blockers"):
        raise NotReady("AAE/UEM compatibility is incomplete; production packaging is disabled")
    builds = profile.get("mac_builds", [])
    if not builds:
        raise NotReady("No qualified Mac executable profiles")
    hashes = []
    for build in builds:
        digest = build.get("executable_sha256", "")
        if len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            raise NotReady("A Mac build is missing its exact executable SHA256")
        if not build.get("verified") or not REQUIRED.issubset(build.get("verified_capabilities", [])):
            raise NotReady("A Mac build is missing required verified capabilities")
        if not build.get("binding_evidence"):
            raise NotReady("A Mac build is missing its native binding qualification record")
        hashes.append(digest)
    return hashes


def candidate_builds(profile: dict) -> list[str]:
    """Implementation gate for testing, deliberately separate from qualification."""
    builds = profile.get("mac_builds", [])
    if not profile.get("source_implementation_complete") or not builds:
        raise NotReady("Required source adapters are incomplete")
    hashes = []
    for build in builds:
        digest = build.get("executable_sha256", "")
        if len(digest) != 64 or any(c not in "0123456789abcdef" for c in digest):
            raise NotReady("A candidate requires an exact Mac executable SHA256")
        if not REQUIRED.issubset(build.get("candidate_capabilities", [])) or not build.get("binding_evidence"):
            raise NotReady("A candidate is missing source adapters or binding evidence")
        hashes.append(digest)
    return hashes


def file_records(root: Path) -> list[dict]:
    records = []
    for path in sorted(root.rglob("*")):
        if path.is_symlink():
            raise ValueError(f"Payload contains a symbolic link: {path.relative_to(root)}")
        if not path.is_file() or path.name == "manifest.json":
            continue
        relative = path.relative_to(root).as_posix()
        first = relative.split("/")[0]
        if first.startswith(".setup-") or first in {"pfx", "Physics", "settings.ini", "setup-receipt.json", "journal.json"}:
            raise ValueError(f"Payload must not overwrite runtime/user data: {relative}")
        records.append({"path": relative, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
    return records


def bundle(app: Path, output: Path, dylib: Path, launcher: Path, assets: Path | None,
           wine: Path | None, profile: dict, development: bool, test_candidate: bool = False) -> dict:
    if development and test_candidate:
        raise ValueError("Choose either diagnostics or a test candidate")
    game_hashes = [] if development else candidate_builds(profile) if test_candidate else qualified_builds(profile)
    # New destinations only: never recursively delete/reuse an existing bundle.
    if output.exists():
        raise FileExistsError(f"Output already exists; choose a new build path: {output}")
    if not (app / "Contents/MacOS/BO3MacFixSetup").is_file():
        raise ValueError("Build installer/BO3MacFix Setup.app on macOS first")
    if not dylib.is_file() or not launcher.is_file():
        raise ValueError("The compiled Mac dylib and launcher are required")
    if (app / "Contents/Resources/payload").exists():
        raise ValueError("The source app already contains a payload; build a clean source app")
    for source in [app, assets, wine]:
        if source and output.resolve().is_relative_to(source.resolve()):
            raise ValueError("The output must be outside every source directory")
    if not development and not test_candidate and (assets is None or not (assets / "cod2map/cod2map64.exe").is_file()):
        raise NotReady("The complete upstream conversion/zone asset payload is required")
    if not development and not test_candidate and (wine is None or not any((wine / "bin" / name).is_file() for name in ["wine", "wine64"])):
        raise NotReady("Bundle the qualified Wine runtime; end users must not need Homebrew or Terminal")
    if not development and not test_candidate and not any(path.is_file() for path in wine.glob("*LICENSE*")):
        raise NotReady("The bundled Wine runtime must include its license and distribution notices")
    # Validate source trees before copying. Resolve dependencies explicitly; do
    # not follow external symlinks hidden in a supplied release directory.
    for source in [app, assets, wine]:
        if source:
            for path in source.rglob("*"):
                if path.is_symlink():
                    raise ValueError(f"Resolve and review the runtime symlink before bundling: {path}")
    output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(app, output)
    payload = output / "Contents/Resources/payload"
    payload.mkdir(parents=True)
    shutil.copy2(dylib, payload / "BO3MacFix.dylib")
    shutil.copy2(launcher, payload / "AppBundleExe")
    if assets:
        for source in assets.iterdir():
            if source.name in {"BO3MacFix.dylib", "AppBundleExe", "Install_BO3MacFix", "settings.ini", "pfx", "Physics", "manifest.json"}:
                continue
            destination = payload / source.name
            if source.is_dir():
                shutil.copytree(source, destination)
            elif source.is_file():
                shutil.copy2(source, destination)
    if wine:
        shutil.copytree(wine, payload / "runtime/wine")
    shutil.copy2(REPO / "LICENSE.txt", payload / "LICENSE.txt")
    manifest = {"schema": 1, "version": "aae-uem-development" if development else "aae-uem-test-1.5" if test_candidate else "aae-uem-qualified",
        "ready_for_coop": not development and not test_candidate, "test_candidate": test_candidate,
        "game_sha256": game_hashes, "requires_bundled_wine": not development and not test_candidate}
    if test_candidate:
        if assets or wine:
            raise ValueError("Test candidates use pinned automatic downloads")
        catalog = REPO / "profiles/installer-dependencies.json"
        manifest["dependencies_id"] = json.loads(catalog.read_text(encoding="utf8"))["id"]
        shutil.copy2(catalog, payload / "dependencies.json")
    manifest["files"] = file_records(payload)
    (payload / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf8")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, default=REPO / "installer/BO3MacFix Setup.app")
    parser.add_argument("--dylib", type=Path, default=REPO / "BO3MacFix.dylib")
    parser.add_argument("--launcher", type=Path, default=REPO / "launcher/AppBundleExe")
    parser.add_argument("--assets", type=Path)
    parser.add_argument("--wine-runtime", type=Path)
    parser.add_argument("--profile", type=Path, default=REPO / "profiles/mod-compatibility.json")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--development", action="store_true")
    parser.add_argument("--test-candidate", action="store_true")
    args = parser.parse_args()
    try:
        profile = json.loads(args.profile.read_text(encoding="utf8"))
        manifest = bundle(args.app, args.output, args.dylib, args.launcher, args.assets, args.wine_runtime, profile, args.development, args.test_candidate)
    except (ValueError, OSError) as error:
        parser.exit(1, f"Setup bundle was not released: {error}\n")
    print(json.dumps({"output": str(args.output), "files": len(manifest["files"]),
        "ready_for_coop": manifest["ready_for_coop"], "signed_and_notarized": False}))


if __name__ == "__main__":
    main()
