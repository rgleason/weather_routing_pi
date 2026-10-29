"""The official pin must never silently become an unmerged validation build."""
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import Mock

spec = importlib.util.spec_from_file_location(
    'host_update', Path(__file__).parents[1] / 'update-windows-wx334-host.py')
host_update = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host_update)


class WindowsHostUpdate(unittest.TestCase):
    def test_requires_the_accepted_upstream_fix(self):
        request = Mock(return_value={'status': 'behind'})
        with self.assertRaisesRegex(ValueError, 'does not contain'):
            host_update.verify_upstream('a' * 40, request)
        self.assertEqual(request.call_count, 1)
        request.assert_called_once_with('compare/' + host_update.FIX_COMMIT + '...' + 'a' * 40)

    def test_requires_a_descendant_of_the_merged_fix(self):
        for status in ('identical', 'ahead', 'behind', 'diverged'):
            request = Mock(return_value={'status': status})
            if status in ('identical', 'ahead'):
                host_update.verify_upstream('a' * 40, request)
            else:
                with self.assertRaisesRegex(ValueError, 'does not contain'):
                    host_update.verify_upstream('a' * 40, request)

    def test_rejects_validation_artifacts_and_nonofficial_hosts(self):
        host_update.verify_url('https://dl.cloudsmith.io/public/david-register/'
                               'opencpn-unstable/raw/files/opencpn_setup_x64.exe')
        for url in ['https://output.circle-artifacts.com/host_setup_x64.exe',
                    'https://example.com/opencpn_setup_x64.exe',
                    'http://dl.cloudsmith.io/public/david-register/opencpn-unstable/'
                    'raw/files/opencpn_setup_x64.exe']:
            with self.assertRaises(ValueError):
                host_update.verify_url(url)

    def test_checks_architecture_revision_and_toolkit(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data = bytearray(256)
            data[:2] = b'MZ'
            struct.pack_into('<I', data, 0x3c, 64)
            data[64:68] = b'PE\0\0'
            struct.pack_into('<H', data, 68, 0x8664)
            struct.pack_into('<H', data, 88, 0x20b)
            data[128:143] = b'5.15.0 abcdef0\0\0'
            (root / 'opencpn.exe').write_bytes(data)
            (root / 'wxbase334u_vc14x_x64.dll').touch()
            self.assertEqual(host_update.verify_image(root, 'abcdef0' + 'a' * 33), '5.15.0')
            with self.assertRaisesRegex(ValueError, 'revision'):
                host_update.verify_image(root, 'b' * 40)
            (root / 'wxbase32u_vc14x_x64.dll').touch()
            with self.assertRaisesRegex(ValueError, 'exclusively'):
                host_update.verify_image(root, 'abcdef0' + 'a' * 33)
            struct.pack_into('<H', data, 68, 0x14c)
            (root / 'opencpn.exe').write_bytes(data)
            with self.assertRaisesRegex(ValueError, 'AMD64'):
                host_update.verify_image(root, 'abcdef0' + 'a' * 33)


if __name__ == '__main__':
    unittest.main()
