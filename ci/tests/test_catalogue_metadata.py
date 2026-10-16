"""Exercise the catalogue schema on finished external/embedded package pairs."""
import importlib.util
import io
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).parents[2]
spec = importlib.util.spec_from_file_location('catalogue_validator', ROOT / 'ci/validate-catalogue-packages.py')
validator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(validator)


def metadata(target='msvc-64', build_target='msvc'):
    root = ET.Element('plugin', version='1')
    for key, value in [('name', 'WeatherRouting'), ('version', '1.30.1'), ('release', '0'),
                       ('summary', 'Weather routing'), ('api-version', '1.21'),
                       ('open-source', 'yes'), ('author', 'Team'),
                       ('source', 'https://github.com/rgleason/weather_routing_pi'),
                       ('description', 'Weather routing'), ('target', target),
                       ('build-target', build_target), ('target-version', '10'),
                       ('target-arch', 'x86_64'),
                       ('tarball-url', 'https://example.test/plugin.tar.gz')]:
        ET.SubElement(root, key).text = value
    return ET.tostring(root, encoding='utf-8', xml_declaration=True)


@unittest.skipUnless(shutil.which('xmllint'), 'Offline publication gate requires xmllint')
class CatalogueMetadata(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)

    def pair(self, external, embedded=None):
        (self.root / 'plugin.xml').write_bytes(external)
        with tarfile.open(self.root / 'plugin.tar.gz', 'w:gz') as archive:
            data = embedded if embedded is not None else external
            member = tarfile.TarInfo('./metadata.xml')
            member.size = len(data)
            archive.addfile(member, io.BytesIO(data))

    def test_correct_windows64_pair_passes(self):
        self.pair(metadata())
        validator.validate(self.root)

    def test_published_windows64_build_target_regression_is_rejected(self):
        self.pair(metadata(build_target='msvc-64'))
        with self.assertRaisesRegex(ValueError, 'schema validation'):
            validator.validate(self.root)

    def test_celnav_unsupported_windows64_target_is_rejected(self):
        self.pair(metadata(target='msvc-wx32-x64'))
        with self.assertRaisesRegex(ValueError, 'schema validation'):
            validator.validate(self.root)

    def test_fixing_only_external_xml_does_not_pass(self):
        self.pair(metadata(), metadata(build_target='msvc-64'))
        with self.assertRaisesRegex(ValueError, 'schema validation'):
            validator.validate(self.root)

    def test_schema_valid_but_different_embedded_source_is_rejected(self):
        self.pair(metadata(), metadata().replace(b'rgleason', b'pob220'))
        with self.assertRaisesRegex(ValueError, 'metadata differ'):
            validator.validate(self.root)

    def test_windows64_cmake_generates_separate_compatibility_and_build_targets(self):
        source = (ROOT / 'cmake/PluginSetup.cmake').read_text()
        branch = source.split('elseif (MSVC)\n', 1)[1].split('elseif (APPLE)', 1)[0]
        script = self.root / 'windows64.cmake'
        script.write_text('set(CMAKE_SIZEOF_VOID_P 8)\nset(ENV{WX_VER} 32)\n' + branch +
                          '\nif(NOT PKG_TARGET STREQUAL "msvc-64" OR NOT PKG_BUILD_TARGET STREQUAL "msvc")\n'
                          'message(FATAL_ERROR "Incorrect Windows x64 metadata generators")\nendif()\n')
        subprocess.run(['cmake', '-P', str(script)], check=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
