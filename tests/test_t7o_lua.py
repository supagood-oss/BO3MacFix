"""Offline contract checks, not HavokScript/game qualification.

Requires lupa (install into a development environment, never the game).
The filesystem tests use real temporary files. HTTP transport is substituted;
NSURLSession itself is exercised only by the separate macOS target.
"""
from pathlib import Path
import tempfile
import unittest
from lupa import LuaRuntime

SOURCE = Path(__file__).resolve().parents[1] / "source/t7overcharged.lua"


class Contracts(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.lua = LuaRuntime(unpack_returned_tuples=True)
        self.requests = []
        self.jobs = {}
        self.messages = []
        self.encrypted = False
        self.legacy = False
        self.unload = None

        def backend(operation, *args):
            if operation == "capability":
                return "1" if args[0] in ("http", "fileio") or args[0] == "encrypted_http" and self.encrypted or args[0] == "save_codec" and self.legacy else "0"
            if operation == "revision":
                return "0"
            if operation == "status":
                return "unqualified test bindings"
            if operation == "http.submit":
                self.requests.append(args)
                self.jobs[args[1]] = ""
                return "1"
            if operation == "http.submit_encrypted":
                self.requests.append(("opaque", *args))
                return "1"
            if operation == "http.retrieve":
                return self.jobs.get(args[0], "")
            if operation == "http.get":
                return "response"
            if operation.startswith("legacy."):
                self.requests.append((operation, *args))
                return {"legacy.post_queued": "12", "legacy.result": "PENDING"}.get(operation, "legacy response")
            if operation == "file.delete":
                try:
                    Path(args[0]).unlink()
                    return "1"
                except OSError:
                    return "0"
            if operation.startswith("save."):
                return {"save.encode": "fixture encoded", "save.decode": '{"bank":12}'}[operation]
            if operation.startswith("json."):
                self.requests.append((operation, *args))
                return "0" if operation == "json.read" else "1"
            if operation.startswith("system."):
                return "test-identity"
            if operation == "module.loaded":
                return "0"
            if operation == "mod.unload":
                return self.unload
            raise AssertionError(operation)

        wrap = self.lua.eval("function(cb) return function(...) return cb(...) end end")
        def mkdir(path):
            Path(path).mkdir(parents=True, exist_ok=True)
            return True
        self.helpers = [wrap(mkdir), wrap(lambda p: Path(p).is_dir()),
            wrap(lambda: "clipboard"), wrap(lambda _: None), wrap(self.messages.append), wrap(backend)]
        result = self.lua.execute(SOURCE.read_text(encoding="utf8"), *self.helpers)
        self.assertTrue(result, self.messages)

    def filename(self, name):
        return str(self.root / name)

    def test_binary_write_parent_creation_append_and_fourth_argument(self):
        self.lua.globals().filename = self.filename("nested/data.bin").replace("/", "\\")
        self.assertTrue(self.lua.execute('''
            local bytes = string.char(0, 255, 1, 13, 10)
            assert(FileIO.WriteFile(filename, bytes, 0, 1))
            assert(FileIO.DirectoryExists(filename:match("^(.*)\\\\[^\\\\]+$")))
            assert(FileIO.ReadFile(filename) == bytes)
            assert(FileIO.FileSize(filename) == 5)
            assert(FileIO.WriteFile(filename, "A", "true", 0))
            assert(FileIO.FileSize(filename) == 6)
            -- Numeric 2 is false in the native parser, not an append request.
            assert(FileIO.WriteFile(filename, "B", 2, 1))
            return FileIO.ReadFile(filename) == "B"
        '''))

    def test_copy_failure_preserves_destination(self):
        source, destination = self.filename("source"), self.filename("destination")
        self.lua.globals().source = source
        self.lua.globals().destination = destination
        self.assertTrue(self.lua.execute('''
            assert(FileIO.WriteFile(source, "new"))
            assert(FileIO.WriteFile(destination, "keep"))
            assert(not FileIO.Copy(source, destination, 1))
            assert(FileIO.ReadFile(destination) == "keep")
            assert(FileIO.Copy(source, destination, 0))
            return FileIO.ReadFile(destination) == "new"
        '''))

    def test_named_http_submission_and_polling(self):
        self.assertTrue(self.lua.execute('return Axios.GetUpdate("job", "https://example.invalid/one", 3)'))
        self.assertEqual(self.requests, [("GET", "job", "https://example.invalid/one", "", "3")])
        self.assertEqual(self.lua.execute('return Axios.GetRetrieve("job")'), "")
        self.jobs["job"] = "done"
        self.assertEqual(self.lua.execute('return Axios.GetRetrieve("job")'), "done")
        self.assertEqual(len(self.requests), 1)  # Polling must not submit again.
        self.assertTrue(self.lua.execute('return Axios.Post("save", "https://example.invalid/save", "a=1")'))
        self.assertEqual(self.requests[1], ("POST", "save", "https://example.invalid/save", "a=1", "5"))

    def test_unimplemented_capabilities_are_absent_and_revision_is_truthful(self):
        self.assertTrue(self.lua.execute('''
            return Axios.PostEncrypted == nil and HotReload.Start == nil
                and MainLUA.Unknown == nil and not T7Overcharged.HasCapability("movement")
                and MainLUA.Revision() == 0 and winOs == nil
        '''))
        self.assertTrue(self.lua.execute('local ok = pcall(MainLUA.UnloadMod); return not ok'))

    def test_mod_unload_uses_native_result(self):
        self.unload = "0"
        self.assertFalse(self.lua.execute('return MainLUA.UnloadMod()'))
        self.unload = "1"
        self.assertTrue(self.lua.execute('return MainLUA.UnloadMod()'))

    def test_reused_vm_loses_old_generic_fallback(self):
        self.lua.execute('setmetatable(Axios, {__index = function() return function() end end})')
        self.lua.execute('MainLUA.StaleNoOp = function() end')
        self.assertTrue(self.lua.execute(SOURCE.read_text(encoding="utf8"), *self.helpers))
        self.assertTrue(self.lua.execute('return Axios.DoesNotExist == nil and MainLUA.StaleNoOp == nil'))

    def test_credential_methods_stay_absent_despite_stale_exports_or_capability(self):
        self.encrypted = True  # Even a stale backend capability cannot restore them.
        self.lua.execute('''
            Axios.GetEncrypted = function() error("stale get") end
            Axios.GetUpdateEncrypted = function() error("stale update") end
            Axios.PostEncrypted = function() error("stale post") end
            setmetatable(Axios, {__index = function() return function() error("fallback") end end})
        ''')
        self.assertTrue(self.lua.execute(SOURCE.read_text(encoding="utf8"), *self.helpers))
        self.assertTrue(self.lua.execute('''
            local submitted = false
            if Axios and Axios.PostEncrypted then submitted = Axios.PostEncrypted("job", "url", "body") end
            return not submitted and Axios.GetEncrypted == nil
                and Axios.GetUpdateEncrypted == nil and Axios.PostEncrypted == nil
        '''))
        self.assertEqual(self.requests, [])

    def test_setup_error_is_returned_to_native_initializer(self):
        helpers = list(self.helpers)
        helpers[4] = self.lua.eval('function(message) end')
        helpers[5] = self.lua.eval('function() error("transport initialization failed") end')
        result = self.lua.execute(SOURCE.read_text(encoding="utf8"), *helpers)
        self.assertFalse(result[0])
        self.assertIn("transport initialization failed", result[1])

    def test_expanded_json_defaults_are_strings_and_preserve_map_json_helpers(self):
        self.lua.execute('JSON.encode = function() return "map encoder" end')
        self.assertTrue(self.lua.execute(SOURCE.read_text(encoding="utf8"), *self.helpers))
        self.assertTrue(self.lua.execute('''
            assert(JSON.encode() == "map encoder")
            assert(JSON.ReadJson("players/t7.json", "", "discord_enabled", "bool", "") == "0")
            assert(JSON.ReadJson("players/t7.json", "developer", "custom", "bool", false, false) == "0")
            return JSON.WriteJson("players/t7.json", "developer", "custom", "bool", false)
        '''))
        self.assertEqual(self.requests[-3:], [
            ("json.read", "players/t7.json", "", "discord_enabled", "bool", "", "1"),
            ("json.read", "players/t7.json", "developer", "custom", "bool", "false", "0"),
            ("json.write", "players/t7.json", "developer", "custom", "bool", "false")])

    def test_legacy_files_save_codec_queue_and_api_shapes(self):
        self.legacy = True
        self.assertTrue(self.lua.execute(SOURCE.read_text(encoding="utf8"), *self.helpers))
        self.lua.globals().filename = self.filename("players/dead_high/save.cgp")
        self.assertTrue(self.lua.execute('''
            assert(Files.MakeDir(filename:match("^(.*)/[^/]+$")))
            assert(Files.IsDir(filename:match("^(.*)/[^/]+$")))
            assert(FileIO.WriteFile(filename, "keep"))
            assert(Files.Exists(filename))
            assert(Crypto.Encode('{}') == "fixture encoded")
            assert(Crypto.Decode("fixture encoded") == '{"bank":12}')
            assert(Files.Delete(filename))
            assert(not Files.Exists(filename))
            assert(not Files.Delete(filename))
            -- The map calls Remove unconditionally and ignores its result.
            assert(UIErrorHash.Remove() == false)
            assert(Axios.Get("https://example.invalid/get", "1.25") == "legacy response")
            assert(Axios.Post("https://example.invalid/post", "body", "1.25") == "legacy response")
            assert(Axios.PostAndResponse("https://example.invalid/post", "body", "1.25") == "legacy response")
            assert(Axios.PostQueued("https://example.invalid/post", "body", "1.25", 30000) == 12)
            return Axios.Result(12) == "PENDING"
        '''))
        self.assertEqual(self.requests[-5:], [
            ("legacy.get", "https://example.invalid/get", "1.25"),
            ("legacy.post", "https://example.invalid/post", "body", "1.25"),
            ("legacy.post_response", "https://example.invalid/post", "body", "1.25"),
            ("legacy.post_queued", "https://example.invalid/post", "body", "1.25", "30000"),
            ("legacy.result", "12")])
        self.legacy = False
        self.assertTrue(self.lua.execute(SOURCE.read_text(encoding="utf8"), *self.helpers))
        self.assertTrue(self.lua.execute('return Crypto.Encode == nil and Crypto.Decode == nil'))


if __name__ == "__main__":
    unittest.main()
