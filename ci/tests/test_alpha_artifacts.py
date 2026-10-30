import importlib.util
import io
import json
import re
from pathlib import Path
import tarfile
import tempfile
import unittest
import xml.etree.ElementTree as ET

from windows_pe_fixture import pe_image


spec = importlib.util.spec_from_file_location(
    "prepare", Path(__file__).parents[1] / "prepare-alpha-artifacts.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class AlphaArtifacts(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def pair(self, name, version="1.18.3", plugin="xWeatherRouting"):
        directory = self.root / "artifacts" / name / "package"
        directory.mkdir(parents=True)
        archive = directory / f"xweather_routing_pi-{version}-{name}.tar.gz"
        library = ("xweather_routing_pi.dll" if name.startswith("windows-") else
                   "libxweather_routing_pi.dylib" if name == "macos-arm64" else
                   "libxweather_routing_pi.so")
        with tarfile.open(archive, "w:gz") as package:
            content = (pe_image("32" if name.endswith("wx329") else "334")
                       if name.startswith("windows-x64") else b"fixture")
            member = tarfile.TarInfo(f"plugin/lib/{library}")
            member.size = len(content)
            package.addfile(member, io.BytesIO(content))
        metadata = directory / (f"xweather_routing_pi-{version}-metadata-{name}.xml"
                                if name.startswith("flatpak-") else archive.name[:-7] + ".xml")
        root = ET.Element("plugin", version="1")
        fields = {
            "name": plugin, "version": version, "api-version": "1.21",
            "summary": prepare.SUMMARY,
            "source": "https://github.com/pob220/xweather_routing_pi",
            "target": prepare.EXPECTED_TARGETS[name][0],
            "target-version": prepare.EXPECTED_TARGETS[name][1],
            "target-arch": prepare.EXPECTED_TARGETS[name][2],
            "tarball-url": "https://invalid.example/placeholder",
        }
        for key, value in fields.items():
            ET.SubElement(root, key).text = value
        ET.ElementTree(root).write(metadata)
        embed_spec = importlib.util.spec_from_file_location(
            "embed_fixture",
            Path(__file__).parents[1] / "embed-package-metadata.py")
        embed = importlib.util.module_from_spec(embed_spec)
        embed_spec.loader.exec_module(embed)
        embed.embed(archive, metadata, archive)
        return directory, archive, metadata

    def test_rejects_standard_identity(self):
        directory, _, _ = self.pair("trixie", plugin="WeatherRouting")
        with self.assertRaisesRegex(ValueError, "Wrong plugin name"):
            prepare.inspect_pair(directory)

    def test_catalogue_summary_matches_build_and_fits_schema(self):
        cmake = (Path(__file__).parents[2] / "CMakeLists.txt").read_text()
        summaries = re.findall(r'set\(SHORT_DESCRIPTION\s+"([^"]+)"\)', cmake)
        self.assertIn(prepare.SUMMARY, summaries)
        self.assertLessEqual(len(prepare.SUMMARY), 72)

    def test_rejects_mismatched_catalogue_target(self):
        directory, _, metadata = self.pair("flatpak-aarch64")
        root = ET.parse(metadata).getroot()
        root.find("target").text = "flatpak-32-aarch64"
        ET.ElementTree(root).write(metadata)
        with self.assertRaisesRegex(ValueError, "Unexpected target"):
            prepare.inspect_pair(directory)

    def test_rejects_four_part_version(self):
        directory, _, _ = self.pair("trixie", version="1.18.3.0")
        with self.assertRaisesRegex(ValueError, "Archive/XML version mismatch"):
            prepare.inspect_pair(directory)

    def test_incomplete_matrix_writes_nothing(self):
        self.pair("trixie")
        output = self.root / "release"
        with self.assertRaisesRegex(ValueError, "matrix"):
            prepare.prepare(self.root / "artifacts", output, "abcdef1", "1")
        self.assertFalse(output.exists())

    def test_complete_matrix_embeds_resolved_metadata(self):
        for name in prepare.TARGETS:
            self.pair(name)
        output = self.root / "release"
        prepare.prepare(self.root / "artifacts", output, "abcdef1", "23")
        uploads = json.loads((output / "uploads.json").read_text())
        self.assertEqual(len(uploads), 2 * len(prepare.TARGETS))
        self.assertTrue(all(item["version"] == "1.18.3+23.abcdef1" for item in uploads))
        for archive in output.glob("*.tar.gz"):
            with tarfile.open(archive, "r:gz") as package:
                self.assertEqual(package.getnames().count("metadata.xml"), 1)
                embedded = ET.fromstring(package.extractfile("metadata.xml").read())
            self.assertEqual(embedded.findtext("summary"), prepare.SUMMARY)
            self.assertIn("/pob220/xweather-routing-alpha-oss/",
                          embedded.findtext("tarball-url"))
            self.assertNotIn("--pkg_repo--", embedded.findtext("tarball-url"))

    def test_legacy_wx329_download_cannot_collide_with_the_wx334_catalogue_package(self):
        for name in prepare.TARGETS:
            self.pair(name)
        directory, _, _ = self.pair('windows-x64-wx329')
        compatibility = self.root / 'compatibility'
        compatibility.mkdir()
        directory.parent.rename(compatibility / directory.parent.name)
        output = self.root / 'release'
        prepare.prepare(self.root / 'artifacts', output, 'abcdef1', '23', compatibility_root=compatibility)
        uploads = json.loads((output / 'uploads.json').read_text())
        self.assertEqual(len({item['name'] for item in uploads}), len(uploads))
        supplemental = [item for item in uploads if not item['catalogue']]
        self.assertEqual(len(supplemental), 2)
        self.assertTrue(all(Path(item['file']).parent == output / 'windows-x64-wx329'
                            for item in supplemental))
        # Ordinary catalogue readers enumerate only the top-level metadata.
        self.assertEqual(len(list(output.glob('*.xml'))), len(prepare.TARGETS))
        self.assertEqual(len(list(output.rglob('*.xml'))), len(prepare.TARGETS) + 1)

    def test_requested_compatibility_matrix_must_be_complete_before_staging(self):
        for name in prepare.TARGETS:
            self.pair(name)
        output = self.root / 'release'
        with self.assertRaisesRegex(ValueError, 'compatibility matrix'):
            prepare.prepare(self.root / 'artifacts', output, 'abcdef1', '23',
                            compatibility_root=self.root / 'compatibility')
        self.assertFalse(output.exists())

    def test_compatibility_variant_must_have_the_same_release_version(self):
        for name in prepare.TARGETS:
            self.pair(name)
        directory, _, _ = self.pair('windows-x64-wx329', version='1.18.4')
        compatibility = self.root / 'compatibility'
        compatibility.mkdir()
        directory.parent.rename(compatibility / directory.parent.name)
        output = self.root / 'release'
        with self.assertRaisesRegex(ValueError, 'Mixed versions'):
            prepare.prepare(self.root / 'artifacts', output, 'abcdef1', '23', compatibility_root=compatibility)
        self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()
