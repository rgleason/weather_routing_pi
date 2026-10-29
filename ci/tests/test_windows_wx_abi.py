"""Reject a Windows plugin linked to the other host's wxWidgets ABI."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location(
    'windows_sdk', Path(__file__).parents[1] / 'circleci-build-windows64.py')
sdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sdk)


class WindowsWxAbi(unittest.TestCase):
    def test_each_runtime_accepts_only_its_own_dependencies(self):
        for version, family, wrong in [('3.2.9', '32', '334'), ('3.3.4', '334', '32')]:
            with self.subTest(version=version):
                sdk.validate_wx_dependencies([
                    f'wxbase{family}u_vc14x_x64.dll',
                    f'wxbase{family}u_net_vc14x_x64.dll',
                    f'wxmsw{family}u_core_vc14x_x64.dll',
                ], version)
                with self.assertRaisesRegex(RuntimeError, 'ABI dependency'):
                    sdk.validate_wx_dependencies([f'wxmsw{wrong}u_core_vc14x_x64.dll'], version)
                with self.assertRaisesRegex(RuntimeError, 'ABI dependency'):
                    sdk.validate_wx_dependencies([f'wxmsw{family}ud_core_vc14x_x64.dll'], version)
        with self.assertRaisesRegex(RuntimeError, 'No wxWidgets imports'):
            sdk.validate_wx_dependencies([], '3.3.4')

    def test_dumpbin_symbols_remain_associated_with_their_runtime(self):
        imports = sdk.imported_symbols('''
    opencpn.exe
        123 ?GetRoute_Plugin@@signature
    wxmsw334u_core_vc14x_x64.dll
        456 ??0wxWindow@@signature
    KERNEL32.dll
        789 GetLastError
''')
        self.assertEqual(imports['opencpn.exe'], ['?GetRoute_Plugin@@signature'])
        self.assertEqual(imports['wxmsw334u_core_vc14x_x64.dll'], ['??0wxWindow@@signature'])
        self.assertEqual(imports['kernel32.dll'], [])
