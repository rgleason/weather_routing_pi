import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest


spec = importlib.util.spec_from_file_location(
    "gettext_source", Path(__file__).resolve().parents[1] / "fetch-macos-gettext-source.py")
source = importlib.util.module_from_spec(spec)
spec.loader.exec_module(source)


class GettextSourceIntegrity(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.cache = Path(self.directory.name) / "gettext.tar.gz"
        self.payload = b"the checksum-verified source archive"
        self.formula = {"urls": {"stable": {
            "url": "https://ftp.gnu.org/gnu/gettext/gettext-0.25.tar.gz",
            "checksum": hashlib.sha256(self.payload).hexdigest(),
        }}}

    def test_valid_cache_needs_no_network(self):
        self.cache.write_bytes(self.payload)
        def unexpected_fetch(*args):
            self.fail("A valid source cache must not be downloaded again")
        source.ensure_source(self.formula, self.cache, unexpected_fetch)

    def test_bad_mirror_cannot_replace_cache_before_verified_fallback(self):
        self.cache.write_bytes(b"old cached archive")
        calls = []
        def fetch(url, destination):
            self.assertEqual(self.cache.read_bytes(), b"old cached archive")
            calls.append(url)
            destination.write_bytes(b"corrupted download" if len(calls) == 1 else self.payload)
        source.ensure_source(self.formula, self.cache, fetch)
        self.assertEqual(len(calls), 2)
        self.assertEqual(self.cache.read_bytes(), self.payload)
        self.assertEqual(list(self.cache.parent.glob("*.partial")), [])

    def test_all_bad_mirrors_preserve_cache_and_fail_closed(self):
        self.cache.write_bytes(b"old cached archive")
        with self.assertRaises(RuntimeError):
            source.ensure_source(self.formula, self.cache,
                                 lambda url, destination: destination.write_bytes(b"bad checksum"))
        self.assertEqual(self.cache.read_bytes(), b"old cached archive")
        self.assertEqual(list(self.cache.parent.glob("*.partial")), [])

    def test_unexpected_formula_source_is_rejected(self):
        self.formula["urls"]["stable"]["url"] = "https://example.com/gettext-0.25.tar.gz"
        with self.assertRaises(ValueError):
            source.ensure_source(self.formula, self.cache)
        self.assertFalse(self.cache.exists())
