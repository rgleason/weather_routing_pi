"""Publication must distinguish DLL runtimes sharing the msvc-64 label."""
import importlib.util
from pathlib import Path
import unittest

from windows_pe_fixture import pe_image

spec = importlib.util.spec_from_file_location(
    "windows_package", Path(__file__).parents[1] / "windows_package_abi.py")
abi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(abi)


class WindowsPackageAbi(unittest.TestCase):
    def test_catalogue_and_legacy_packages_require_their_own_runtime(self):
        for target, family, wrong in (("windows-x64", "334", "32"),
                                      ("windows-x64-wx329", "32", "334")):
            with self.subTest(target=target):
                abi.validate(pe_image(family), target)
                with self.assertRaisesRegex(ValueError, "wxWidgets ABI"):
                    abi.validate(pe_image(wrong), target)

    def test_x86_binary_cannot_be_published_as_x64(self):
        with self.assertRaisesRegex(ValueError, "AMD64"):
            abi.validate(pe_image(machine=0x14c), "windows-x64")

    def test_malformed_or_truncated_binaries_fail_closed(self):
        for data in (b"fixture", pe_image()[:300], pe_image()[:670]):
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                abi.validate(data, "windows-x64")


if __name__ == "__main__":
    unittest.main()
