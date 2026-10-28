#!/usr/bin/env python3
"""Bounded Wine + SDK startup check, using no game files or valid map data."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="bo3-converter-check-") as temp:
    work = Path(temp)
    env = dict(os.environ, WINEDEBUG="-all,err+all", WINEPREFIX=str(work / "prefix"),
               MVK_CONFIG_LOG_LEVEL="0", BO3_CONVERTER_ENV_FIXTURE="A" * 12000,
               WINEDLLOVERRIDES="winemenubuilder.exe=d;mscoree=d;mshtml=d")
    # Background Wine services inherit stdout. A regular file avoids waiting
    # indefinitely for every service to close a captured pipe after exit.
    log = work / "startup.log"
    returncode = None
    try:
        with log.open("wb") as stream:
            process = subprocess.Popen([str(Path(sys.argv[2]).resolve()), str(root / "runtime/wine/bin/wine"), str(root / "cod2map/cod2map64.exe"),
                                        str(work / "deliberately-missing.hkt"), str(work / "unused.hkt")],
                                       env=env, cwd=root / "cod2map", stdout=stream, stderr=stream)
            try:
                returncode = process.wait(timeout=165)
            except subprocess.TimeoutExpired:
                snapshot = subprocess.run(["ps", "-Ao", "pid,ppid,comm"], capture_output=True, text=True, timeout=5)
                for line in snapshot.stdout.splitlines():
                    if any(name in line.lower() for name in ("wine", "rundll32", "services.exe", "winedevice")):
                        print(line)
                    if "rundll32.exe" in line.lower():
                        child_sample = work / "rundll-sample.txt"
                        subprocess.run(["sample", line.split()[0], "1", "-file", str(child_sample)],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=8)
                        if child_sample.exists():
                            print(child_sample.read_text(errors="replace")[:15000])
                sample = work / "sample.txt"
                subprocess.run(["sample", str(process.pid), "1", "-file", str(sample)],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=8)
                if sample.exists():
                    print(sample.read_text(errors="replace")[:12000])
                process.kill()
                process.wait()
    finally:
        # Only the fixture's unique prefix; never touch a user's Wine session.
        subprocess.run([str(root / "runtime/wine/bin/wineserver"), "-k"], env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
    output = log.read_bytes().decode("utf8", "replace")
    # This confirms Wine, the converter and its patched entrypoint loaded. It
    # deliberately stops at file-open failure; it cannot qualify map conversion.
    if returncode is None or returncode == 0 or "Mac navmesh convert requested" not in output or "FAILED to open input file" not in output:
        print(f"Converter exit status: {returncode!r}")
        print(output[:8000])
        print(output[-10000:])
        raise SystemExit("The pinned converter did not reach the expected missing-input path")
    print("Wine and patched converter startup passed (missing-input fixture; no map converted)")
