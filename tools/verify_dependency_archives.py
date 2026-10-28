#!/usr/bin/env python3
"""Inspect pinned vendor archives without extracting or executing them."""
import argparse
import hashlib
import json
import posixpath
from pathlib import Path, PurePosixPath
import tarfile


def inspect(path: Path, record: dict) -> dict:
    if path.stat().st_size != record["bytes"] or hashlib.file_digest(path.open("rb"), "sha256").hexdigest() != record["sha256"]:
        raise ValueError("Vendor archive hash/size mismatch")
    total = 0
    names = set()
    links = 0
    with tarfile.open(path) as archive:
        for member in archive:
            name = member.name.rstrip("/")
            parts = PurePosixPath(name).parts
            if not parts or name.startswith("/") or ".." in parts or "\\" in name or name in names:
                raise ValueError(f"Unsafe or duplicated archive path: {name}")
            names.add(name)
            if member.issym() or member.islnk():
                target = member.linkname
                # Tar hardlink targets are archive-relative; symlinks are parent-relative.
                resolved = posixpath.normpath(posixpath.join(posixpath.dirname(name), target) if member.issym() else target)
                if target.startswith("/") or "\\" in target or resolved.startswith("../") or resolved.split("/")[0] != parts[0]:
                    raise ValueError(f"Escaping archive link: {name}")
                links += 1
            elif not member.isfile() and not member.isdir():
                raise ValueError(f"Special archive entry: {name}")
            total += member.size
    if total > 2_000_000_000 or len(names) > 20_000:
        raise ValueError("Unexpected archive expansion")
    return {"kind": record["kind"], "entries": len(names), "internal_links": links, "expanded_bytes": total}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("upstream", type=Path)
    parser.add_argument("wine_arm64", type=Path)
    parser.add_argument("wine_x86_64", type=Path)
    parser.add_argument("frameworks", type=Path)
    args = parser.parse_args()
    catalog = json.loads((Path(__file__).resolve().parents[1] / "profiles/installer-dependencies.json").read_text())
    print(json.dumps([inspect(getattr(args, record["kind"] + ("_" + record["host_arch"] if record.get("host_arch") else "")), record)
                      for record in catalog["archives"]], indent=2))
