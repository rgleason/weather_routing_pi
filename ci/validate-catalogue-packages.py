#!/usr/bin/env python3
"""Validate external and embedded metadata in a finished publication directory."""
import argparse
from pathlib import Path, PurePosixPath
import subprocess
import tarfile
import tempfile
from urllib.parse import unquote, urlparse
import xml.etree.ElementTree as ET

SCHEMA = Path(__file__).with_name('catalogue') / 'ocpn-plugin.xsd'


def validate_xml(data):
    with tempfile.NamedTemporaryFile(suffix='.xml') as stream:
        stream.write(data)
        stream.flush()
        checked = subprocess.run(['xmllint', '--noout', '--schema', str(SCHEMA),
                                  stream.name], capture_output=True, text=True)
    if checked.returncode:
        raise ValueError('Catalogue schema validation failed: ' + checked.stderr.strip())
    return ET.fromstring(data)


def validate(directory):
    directory = Path(directory)
    archives = sorted(directory.rglob('*.tar.gz'))
    if not archives:
        raise ValueError('No finished plugin archives found')
    paired = set()
    for archive in archives:
        # Each finished pair shares a directory, including Flatpak XML whose
        # filename differs from its tarball. Match the final URL, not a glob.
        candidates = []
        for external in archive.parent.glob('*.xml'):
            data = external.read_bytes()
            root = validate_xml(data)
            url = (root.findtext('tarball-url') or '').strip()
            if urlparse(url).scheme != 'https' or '--' in url:
                raise ValueError('Unresolved or non-HTTPS download URL: ' + url)
            if unquote(PurePosixPath(urlparse(url).path).name) == archive.name:
                candidates.append((external, data))
        if len(candidates) != 1:
            raise ValueError('Expected one external metadata file for ' + archive.name)
        external, data = candidates[0]
        with tarfile.open(archive, 'r:gz') as package:
            metadata = [m for m in package if m.isfile() and
                        PurePosixPath(m.name).as_posix() == 'metadata.xml']
            if len(metadata) != 1:
                raise ValueError('Expected exactly one root metadata.xml: ' + archive.name)
            embedded = package.extractfile(metadata[0]).read()
        validate_xml(embedded)
        if embedded != data:
            raise ValueError('Embedded/external metadata differ: ' + archive.name)
        paired.add(external)
    external_files = set(directory.rglob('*.xml'))
    if external_files != paired:
        raise ValueError('Unpaired publication metadata: ' +
                         ', '.join(str(p) for p in external_files - paired))
    print(f'Validated {len(archives)} finished archives and matching catalogue XML files')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    validate(parser.parse_args().directory)
