"""Cold-process persistence checks, using synthetic engine bytes only."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CheckpointStoreTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get("CC", "cc")
        if not shutil.which(compiler):
            raise unittest.SkipTest("Host C compiler unavailable")
        cls.directory = tempfile.TemporaryDirectory(prefix="bo3-checkpoint-fixture-")
        cls.binary = Path(cls.directory.name) / ("store.exe" if os.name == "nt" else "store")
        subprocess.run([compiler, "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-O1", "-Wall", "-Wextra", "-Werror",
                        "tests/test_t7o_checkpoint_store.c", "source/t7o_checkpoint_store.c", "source/mod_fileio.c",
                        "-o", str(cls.binary)], cwd=ROOT, check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        cls.directory.cleanup()

    def test_fresh_process_reads_saved_payload(self):
        path = Path(self.directory.name) / "cold.macsav"
        subprocess.run([str(self.binary), "write", str(path)], check=True, capture_output=True)
        subprocess.run([str(self.binary), "read", str(path)], check=True, capture_output=True)

    def test_fresh_process_rejects_damaged_payload(self):
        path = Path(self.directory.name) / "damaged.macsav"
        subprocess.run([str(self.binary), "write", str(path)], check=True, capture_output=True)
        data = bytearray(path.read_bytes())
        data[-1] ^= 1
        path.write_bytes(data)
        result = subprocess.run([str(self.binary), "read", str(path)], capture_output=True)
        self.assertNotEqual(result.returncode, 0)
