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


class LinuxHostAbi(unittest.TestCase):
    def test_exact_host_targets(self):
        for abi in ['debian-x86_64:12', 'debian-arm64:12',
                    'debian-x86_64:13', 'ubuntu-x86_64:24.04']:
            self.assertTrue(host.linux_abi_matches(abi, abi))

    def test_stock_jammy_print_abi_uses_its_compile_time_label(self):
        self.assertTrue(host.linux_abi_matches('ubuntu-x86_64:22.04',
                                              'ubuntu-wx32-x86_64:22.04'))

    def test_other_distribution_architecture_version_and_wx_variants_are_rejected(self):
        for package_abi in ['debian-x86_64:12', 'ubuntu-wx32-arm64:22.04',
                            'ubuntu-wx32-x86_64:24.04', 'ubuntu-gtk3-x86_64:22.04']:
            self.assertFalse(host.linux_abi_matches('ubuntu-x86_64:22.04', package_abi))
