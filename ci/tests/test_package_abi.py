import importlib.util
import io
from pathlib import Path
import tarfile
import tempfile
import unittest
import xml.etree.ElementTree as ET

spec = importlib.util.spec_from_file_location(
    "abi", Path(__file__).parents[1] / "verify-package-abi.py")
abi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(abi)


class PackageAbi(unittest.TestCase):
    def test_delivered_archive_is_checked_without_cpack_staging_copies(self):
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder)
            metadata = ET.tostring(self.metadata("darwin-wx32", "15.3.2", "arm64"))
            stem = "xweather_routing_pi-1.29.3-macos-arm64"
            (directory / (stem + ".xml")).write_bytes(metadata)
            with tarfile.open(directory / (stem + ".tar.gz"), "w:gz") as archive:
                member = tarfile.TarInfo("metadata.xml")
                member.size = len(metadata)
                archive.addfile(member, io.BytesIO(metadata))
            staging = directory / "_CPack_Packages/Darwin/TGZ"
            staging.mkdir(parents=True)
            with tarfile.open(staging / (stem + ".tar.gz"), "w:gz"):
                pass  # CPack's intermediate archive has no embedded metadata.
            abi.verify(directory, "macos-arm64")

    def metadata(self, label, version, arch):
        root = ET.Element("plugin")
        for key, value in zip(("target", "target-version", "target-arch", "api-version"),
                              (label, version, arch, "1.21")):
            ET.SubElement(root, key).text = value
        return root

    def test_flatpak_host_labels_for_both_architectures(self):
        for arch in ("x86_64", "aarch64"):
            with self.subTest(arch=arch):
                abi.validate(self.metadata("flatpak-" + arch, "25.08", arch), "flatpak-" + arch)
                with self.assertRaisesRegex(ValueError, "ABI mismatch"):
                    abi.validate(self.metadata("flatpak-32-" + arch, "25.08", arch), "flatpak-" + arch)

    def test_jammy_requires_wx32_but_noble_does_not(self):
        abi.validate(self.metadata("ubuntu-wx32-x86_64", "22.04", "x86_64"), "jammy")
        abi.validate(self.metadata("ubuntu-x86_64", "24.04", "x86_64"), "noble")
        with self.assertRaisesRegex(ValueError, "ABI mismatch"):
            abi.validate(self.metadata("ubuntu-x86_64", "22.04", "x86_64"), "jammy")

    def test_runtime_and_architecture_must_match(self):
        with self.assertRaisesRegex(ValueError, "runtime version"):
            abi.validate(self.metadata("flatpak-x86_64", "24.08", "x86_64"), "flatpak-x86_64")
        with self.assertRaisesRegex(ValueError, "ABI mismatch"):
            abi.validate(self.metadata("android-arm64", "16", "armhf"), "android-arm64")

    def test_debian_versions_and_windows_architectures_are_distinct(self):
        for label, version, arch, target in (
                ("debian-x86_64", "12", "x86_64", "bookworm"),
                ("debian-x86_64", "13", "x86_64", "trixie"),
                ("debian-arm64", "12", "arm64", "bookworm-arm64"),
                ("msvc-wx32", "10.0.20348", "x86", "MSVC"),
                ("msvc-64", "10", "x86_64", "MSVC-x64")):
            abi.validate(self.metadata(label, version, arch), target)
        with self.assertRaisesRegex(ValueError, "ABI mismatch"):
            abi.validate(self.metadata("msvc-wx32", "10", "x86",), "MSVC-x64")
