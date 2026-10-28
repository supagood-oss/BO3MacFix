from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from inspect_mac_build import inspect, InvalidMachO


def minimal_game():
    ident = bytes.fromhex("12345678123456781234567812345678")
    header = struct.pack("<8I", 0xfeedfacf, 0x01000007, 3, 2, 1, 24, 0, 0)
    return header + struct.pack("<II", 0x1b, 24) + ident


class InspectBuild(unittest.TestCase):
    def test_thin_uuid_and_identity(self):
        report = inspect(minimal_game())
        self.assertEqual(report["uuid"], "12345678-1234-5678-1234-567812345678")
        self.assertFalse(report["mapping_verified"])
        self.assertEqual(len(report["sha256"]), 64)

    def test_fat_selects_intel_without_executing_anything(self):
        game = minimal_game()
        offset = 4096
        fat = struct.pack(">II", 0xcafebabe, 1) + struct.pack(">IIIII", 0x01000007, 3, offset, len(game), 12)
        fat += bytes(offset - len(fat)) + game
        result = inspect(fat)
        self.assertEqual(result["slice_offset"], offset)
        self.assertEqual(result["intel_slice_sha256"], inspect(game)["sha256"])

    def test_corrupt_commands_and_wrong_platform_are_rejected(self):
        data = bytearray(minimal_game())
        struct.pack_into("<I", data, 36, 4096)
        for bad in [bytes(data), b"MZ" + bytes(100), minimal_game()[:40]]:
            with self.subTest(bad=bad[:8]):
                with self.assertRaises(InvalidMachO):
                    inspect(bad)

    def test_universal_out_of_range_slice_is_rejected(self):
        data = struct.pack(">II", 0xcafebabe, 1) + struct.pack(">IIIII", 0x01000007, 3, 4096, 500, 12)
        with self.assertRaises(InvalidMachO):
            inspect(data)


if __name__ == "__main__":
    unittest.main()
