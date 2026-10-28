from pathlib import Path
import json
import sys
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from package_setup import qualified_builds, candidate_builds, file_records, bundle, NotReady


class PackageChecks(unittest.TestCase):
    def test_current_source_cannot_be_released_as_ready(self):
        profile = json.loads((Path(__file__).resolve().parents[1] / "profiles/mod-compatibility.json").read_text())
        with self.assertRaises(NotReady):
            qualified_builds(profile)

    def test_claiming_ready_without_bindings_is_rejected(self):
        with self.assertRaises(NotReady):
            qualified_builds({"ready_for_coop": True, "mac_builds": [{"executable_sha256": "a" * 64, "verified": True}]})

    def test_candidate_requires_implemented_adapters_and_keeps_coop_gate_closed(self):
        profile = json.loads((Path(__file__).resolve().parents[1] / "profiles/mod-compatibility.json").read_text())
        self.assertEqual(len(candidate_builds(profile)), 1)
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            app = root / "source.app"
            binary = app / "Contents/MacOS/BO3MacFixSetup"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"synthetic app")
            dylib, launcher = root / "test.dylib", root / "test-launcher"
            dylib.write_bytes(b"synthetic patch")
            launcher.write_bytes(b"synthetic launcher")
            result = bundle(app, root / "candidate.app", dylib, launcher, None, None, profile, False, True)
            self.assertFalse(result["ready_for_coop"])
            self.assertTrue(result["test_candidate"])
            self.assertIn("dependencies_id", result)
        profile["mac_builds"][0]["candidate_capabilities"].remove("movement")
        with self.assertRaises(NotReady):
            candidate_builds(profile)

    def test_user_data_is_excluded_from_deployable_payload(self):
        for path in ("settings.ini", "setup-receipt.json", "journal.json", "pfx/save", "Physics/cache", ".setup-pending/journal.json"):
            with self.subTest(path=path), tempfile.TemporaryDirectory() as temp:
                root = Path(temp)
                file = root / path
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_text("owner data")
                with self.assertRaises(ValueError):
                    file_records(root)

    def test_development_bundle_is_explicitly_not_playable(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            app = root / "source.app"
            binary = app / "Contents/MacOS/BO3MacFixSetup"
            binary.parent.mkdir(parents=True)
            binary.write_bytes(b"fixture only")
            dylib, launcher = root / "test.dylib", root / "test-launcher"
            dylib.write_bytes(b"fixture patch")
            launcher.write_bytes(b"fixture launcher")
            output = root / "diagnostics.app"
            result = bundle(app, output, dylib, launcher, None, None, {}, True)
            self.assertFalse(result["ready_for_coop"])
            self.assertEqual(result["game_sha256"], [])
            self.assertEqual({x["path"] for x in result["files"]}, {"BO3MacFix.dylib", "AppBundleExe", "LICENSE.txt"})
            with self.assertRaises(FileExistsError):
                bundle(app, output, dylib, launcher, None, None, {}, True)


if __name__ == "__main__":
    unittest.main()
