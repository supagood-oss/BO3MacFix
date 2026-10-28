"""Real temporary packs/saves; no proprietary assets or game process required."""
import ctypes
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def pack(path, entries, refs=(1, 2), kind="mesh", names=None):
    header = bytearray(120)
    struct.pack_into("<4sHH", header, 0, b"KAPI", 0, 10)
    payload = b"".join(entries.values())
    table = bytearray()
    offset = 0
    for key, data in entries.items():
        table += struct.pack("<QQQ", key, offset, len(data))
        offset += len(data)
    reference = struct.pack("<" + "Q" * len(refs), *refs)
    if names is None:
        names = b""
        for key in refs:
            text = f"name: asset_{key}\ntype: {kind if key == 2 else 'image'}\n".encode()
            names += struct.pack("<QQ", key, len(text)) + text
    data_offset = 120 + len(table)
    ref_offset = data_offset + len(payload)
    name_offset = ref_offset + len(reference)
    fields = {16: name_offset + len(names), 24: len(entries), 32: data_offset,
              40: len(payload), 48: len(entries), 56: 120, 64: len(table),
              72: len(refs), 80: ref_offset, 88: len(reference),
              96: len(refs), 104: name_offset, 112: len(names)}
    for start, value in fields.items():
        struct.pack_into("<Q", header, start, value)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + table + payload + reference + names)


class Key(ctypes.Structure):
    _fields_ = [("seed", ctypes.c_uint64), ("factor", ctypes.c_uint64)]


class MapSupport(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        library = Path(cls.build.name) / ("support.dll" if os.name == "nt" else "support.so")
        compiler = os.environ.get("CC", "cc")
        if os.name == "nt":
            cls.dll_directory = os.add_dll_directory(str(Path(compiler).resolve().parent))
        subprocess.run([compiler, "-std=c11", "-O1", "-Wall", "-Wextra", "-Werror",
                        "-shared", *([] if os.name == "nt" else ["-fPIC"]), "-pthread",
                        "source/xpak_fallback.c", "source/mod_fileio.c", "source/t7o_save_codec.c",
                        "-o", str(library)], cwd=ROOT, check=True)
        cls.lib = ctypes.CDLL(str(library))
        cls.lib.bo3_xpak_fallback.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]
        cls.lib.bo3_xpak_resolve.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]
        cls.lib.bo3_lua_path.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_size_t]
        cls.lib.bo3_prepare_save.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p]
        cls.lib.t7o_save_encode.argtypes = [ctypes.c_char_p, Key]
        cls.lib.t7o_save_encode.restype = ctypes.c_void_p
        cls.lib.t7o_save_decode.argtypes = [ctypes.c_char_p, ctypes.POINTER(Key)]
        cls.lib.t7o_save_decode.restype = ctypes.c_void_p
        # Free with the same runtime that allocated the returned string.
        cls.runtime = ctypes.CDLL("msvcrt" if os.name == "nt" else None)
        cls.runtime.free.argtypes = [ctypes.c_void_p]

    @classmethod
    def tearDownClass(cls):
        if os.name == "nt":
            unload = ctypes.WinDLL("kernel32").FreeLibrary
            unload.argtypes = [ctypes.c_void_p]
            unload(cls.lib._handle)
            cls.dll_directory.close()
        cls.build.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.en = self.root / "workshop/content/311210/123/en_map.xpak"
        self.ge = self.en.with_name("ge_map.xpak")

    def select(self, path=None):
        out = ctypes.create_string_buffer(4096)
        result = self.lib.bo3_xpak_fallback(os.fsencode(path or self.en), out, len(out))
        return os.fsdecode(out.value) if result else None

    def text(self, pointer):
        if not pointer:
            return None
        value = ctypes.string_at(pointer)
        self.runtime.free(pointer)
        return value

    def resolve(self, directory=None):
        out = ctypes.create_string_buffer(4096)
        result = self.lib.bo3_xpak_resolve(os.fsencode(self.en),
                     os.fsencode(directory or self.root / "patch-cache"), out, len(out))
        return Path(os.fsdecode(out.value)) if result else None

    def contents(self, path):
        raw = path.read_bytes()
        h = struct.unpack_from("<15Q", raw)
        entries = {}
        for i in range(h[6]):
            key, offset, size = struct.unpack_from("<QQQ", raw, h[7] + 24*i)
            entries[key] = raw[h[4]+offset:h[4]+offset+size]
        return entries, raw[h[10]:h[10]+h[11]], raw[h[13]:h[13]+h[14]]

    def test_derived_pack_preserves_language_and_only_copies_missing_mesh(self):
        pack(self.en, {1: b"shared", 3: b"English only"}, refs=(1, 2, 3))
        pack(self.ge, {1: b"shared", 2: b"missing mesh", 4: b"German only"}, refs=(4, 2, 1))
        before = self.en.read_bytes(), self.ge.read_bytes()
        self.assertIsNone(self.select())
        target = self.resolve()
        self.assertIsNotNone(target)
        self.assertEqual(target.parent, (self.root / "patch-cache").resolve())
        self.assertEqual(self.contents(target)[0], {1: b"shared", 2: b"missing mesh", 3: b"English only"})
        self.assertEqual(self.contents(target)[1:], self.contents(self.en)[1:])
        original_data_offset = struct.unpack_from("<Q", before[0], 32)[0]
        self.assertEqual(target.read_bytes()[120:original_data_offset], before[0][120:original_data_offset])
        self.assertEqual(self.resolve(), target)
        other_cache = self.root / "other-cache"
        self.assertEqual(self.resolve(other_cache).parent, other_cache.resolve())
        # The read-only API must not start returning derived cache files.
        self.assertIsNone(self.select())
        self.assertEqual(before, (self.en.read_bytes(), self.ge.read_bytes()))
        # A donor update invalidates the in-memory selection and repairs the cache.
        previous_time = self.ge.stat().st_mtime
        pack(self.ge, {1: b"shared", 2: b"updated mesh", 4: b"German only"}, refs=(4, 2, 1))
        os.utime(self.ge, (previous_time + 2, previous_time + 2))
        updated = self.resolve()
        self.assertEqual(self.contents(updated)[0][2], b"updated mesh")
        # Cache corruption is recovered atomically; source files remain untouched.
        updated.write_bytes(b"incomplete interrupted cache")
        fixed = self.resolve()
        self.assertEqual(self.contents(fixed)[0][2], b"updated mesh")
        self.assertFalse(list(fixed.parent.glob("*.tmp")))

    def test_derived_pack_requires_exact_mesh_metadata_and_common_payloads(self):
        pack(self.en, {1: b"shared", 3: b"English"}, refs=(1, 2, 3))
        for entries, kind in [({1: b"changed", 2: b"mesh"}, "mesh"),
                              ({1: b"shared", 2: b"voice"}, "sound"),
                              ({1: b"shared"}, "mesh")]:
            pack(self.ge, entries, kind=kind)
            self.assertIsNone(self.resolve())
        pack(self.ge, {1: b"shared", 2: b"mesh"})
        self.ge.write_bytes(self.ge.read_bytes().replace(b"asset_2", b"other_2"))
        self.assertIsNone(self.resolve())
        # An unwritable/invalid cache location is a normal fallback, not a crash.
        pack(self.ge, {1: b"shared", 2: b"mesh"})
        blocker = self.root / "file"
        blocker.write_text("preserve")
        self.assertIsNone(self.resolve(blocker / "cache"))
        self.assertEqual(blocker.read_text(), "preserve")

    def test_complete_companion_and_cached_read_preserve_originals(self):
        pack(self.en, {1: b"existing"})
        pack(self.ge, {1: b"existing", 2: b"missing mesh"})
        before = (self.en.read_bytes(), self.ge.read_bytes())
        self.assertEqual(Path(self.select()), self.ge.resolve())
        self.assertEqual(Path(self.select()), self.ge.resolve())
        self.assertIsNone(self.select(self.ge))
        self.assertEqual(before, (self.en.read_bytes(), self.ge.read_bytes()))
        # A Workshop update invalidates a previously cached selection.
        pack(self.ge, {1: b"changed existing data", 2: b"missing mesh"})
        self.assertIsNone(self.select())

    def test_partial_different_metadata_and_different_common_payload_are_refused(self):
        pack(self.en, {1: b"existing"})
        for entries, refs, kind in [({1: b"existing"}, (1, 2), "mesh"),
                                    ({1: b"changed", 2: b"mesh"}, (1, 2), "mesh"),
                                    ({1: b"existing", 2: b"mesh"}, (2, 1), "mesh"),
                                    ({1: b"existing", 2: b"mesh"}, (1, 2), "sound")]:
            pack(self.ge, entries, refs, kind)
            self.assertIsNone(self.select())

    def test_missing_nonmesh_no_sibling_and_nonworkshop_are_untouched(self):
        pack(self.en, {1: b"existing"}, kind="sound")
        pack(self.ge, {1: b"existing", 2: b"voice"}, kind="sound")
        self.assertIsNone(self.select())
        other = self.root / "base/en_map.xpak"
        pack(other, {1: b"existing"})
        pack(other.with_name("ge_map.xpak"), {1: b"existing", 2: b"mesh"})
        self.assertIsNone(self.select(other))
        self.ge.unlink()
        self.assertIsNone(self.select())

    def test_corrupt_header_offsets_counts_and_duplicate_keys_are_refused(self):
        pack(self.en, {1: b"existing"})
        pack(self.ge, {1: b"existing", 2: b"mesh"})
        valid = self.ge.read_bytes()
        for field, value in [(16, 0), (32, 2**64-1), (48, 2**64-1), (80, 2**64-1), (112, 2**64-1)]:
            data = bytearray(valid)
            struct.pack_into("<Q", data, field, value)
            self.ge.write_bytes(data)
            self.assertIsNone(self.select())
        duplicate = bytearray(valid)
        struct.pack_into("<Q", duplicate, 144, 1)
        self.ge.write_bytes(duplicate)
        self.assertIsNone(self.select())

    def test_save_parent_creation_existing_bytes_absolute_paths_and_scope(self):
        base = os.fsencode(str(self.root).replace("\\", "/") + "/")
        filename = b"players/311210/885119667/deadhigh_save_data.cgp"
        self.assertTrue(self.lib.bo3_prepare_save(base, filename, b"w"))
        save = self.root / filename.decode()
        save.write_bytes(b"existing encoded save")
        self.assertTrue(self.lib.bo3_prepare_save(base, filename, b"a+"))
        self.assertEqual(save.read_bytes(), b"existing encoded save")
        self.assertTrue(self.lib.bo3_prepare_save(base, b"players/not-created/file", b"r+"))
        self.assertFalse((self.root / "players/not-created").exists())
        for name in [b"somewhere/file", b"players/../elsewhere/file"]:
            self.assertTrue(self.lib.bo3_prepare_save(base, name, b"w"))
        self.assertFalse((self.root / "elsewhere").exists())
        out = ctypes.create_string_buffer(4096)
        self.assertTrue(self.lib.bo3_lua_path(base, b"/Volumes/Game/players/save", out, len(out)))
        self.assertEqual(out.value, b"/Volumes/Game/players/save")
        blocker = self.root / "players/blocker"
        blocker.write_text("keep")
        self.assertFalse(self.lib.bo3_prepare_save(base, b"players/blocker/save", b"w"))
        self.assertEqual(blocker.read_text(), "keep")

    def test_save_codec_current_and_previous_generations_and_invalid_data(self):
        keys = (Key * 2)(Key(0x1c1b35e, 0x17ff), Key(123456, 4567))
        for text in [b"{}", b'{"bank":123,"name":"test"}', '{"name":"café ✓"}'.encode()]:
            for key in keys:
                encoded = self.text(self.lib.t7o_save_encode(text, key))
                self.assertEqual(self.text(self.lib.t7o_save_decode(encoded, keys)), text)
        for invalid in [b"", b"a", b"zz", b"{}", b"0000"]:
            self.assertIsNone(self.text(self.lib.t7o_save_decode(invalid, keys)))
        fixture = json.loads((ROOT / "tests/fixtures/dead_high_codec.json").read_text())
        for row in fixture:
            self.assertEqual(self.text(self.lib.t7o_save_encode(row["text"].encode(), keys[0])), row["hex"].encode())


if __name__ == "__main__":
    unittest.main()
