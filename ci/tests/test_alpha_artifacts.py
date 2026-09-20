import importlib.util
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
import xml.etree.ElementTree as ET


spec = importlib.util.spec_from_file_location(
    "prepare", Path(__file__).parents[1] / "prepare-alpha-artifacts.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class AlphaArtifacts(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def pair(self, name, version="1.17.4.0", plugin="xWeatherRouting"):
        directory = self.root / "artifacts" / name / "package"
        directory.mkdir(parents=True)
        archive = directory / f"xweather_routing_pi-{version}-{name}.tar.gz"
        library = ("xweather_routing_pi.dll" if name == "windows-x86" else
                   "libxweather_routing_pi.dylib" if name == "macos-arm64" else
                   "libxweather_routing_pi.so")
        with tarfile.open(archive, "w:gz") as package:
            content = b"fixture"
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
            "target": name, "target-version": "13", "target-arch": "x86_64",
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
        self.assertEqual(len(uploads), 18)
        self.assertTrue(all(item["version"] == "1.17.4.0+23.abcdef1" for item in uploads))
        for archive in output.glob("*.tar.gz"):
            with tarfile.open(archive, "r:gz") as package:
                self.assertEqual(package.getnames().count("metadata.xml"), 1)
                embedded = ET.fromstring(package.extractfile("metadata.xml").read())
            self.assertEqual(embedded.findtext("summary"), prepare.SUMMARY)
            self.assertIn("/pob220/xweather-routing-alpha-oss/",
                          embedded.findtext("tarball-url"))
            self.assertNotIn("--pkg_repo--", embedded.findtext("tarball-url"))


if __name__ == "__main__":
    unittest.main()
