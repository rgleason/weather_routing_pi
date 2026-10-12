"""Regression coverage for the version literals in pinned official hosts."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    'installed_host', Path(__file__).resolve().parents[1] / 'test-installed-host.py')
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)


class StockHostVersion(unittest.TestCase):
    def test_linux_windows_and_official_universal_mac_formats(self):
        for version, date in [('5.14.2', '2026-10-12'),
                              ('5.14.2-0+bd6986a', '2026-09-24'),
                              ('5.14.2-universal+bd6986a08', '2026-09-24')]:
            with self.subTest(version=version):
                binary = (b'5.14.2.html">\0older 2010-08-17\0' +
                          version.encode() + b'\0' + date.encode() + b'\0')
                self.assertEqual(host.host_build(binary), (version, date))

    def test_other_release_or_documentation_link_cannot_identify_the_host(self):
        for binary in [b'5.14.1-0+abcd\0', b'5.14.2.html\0']:
            with self.assertRaises(RuntimeError):
                host.host_build(binary + b'2026-09-24\0')

    def test_missing_build_date_is_rejected(self):
        with self.assertRaises(RuntimeError):
            host.host_build(b'5.14.2-universal+bd6986a08\0')
