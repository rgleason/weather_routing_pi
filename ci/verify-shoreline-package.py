#!/usr/bin/env python3
"""Verify the actual packaged offline GSHHG payload on every platform."""
import gzip
import hashlib
import json
import sys
import tarfile
from pathlib import Path

EXPECTED = json.loads((Path(__file__).resolve().parents[1] / 'data/shoreline/manifest.json').read_text())

def digest(stream):
    h = hashlib.sha256()
    size = 0
    while chunk := stream.read(1024 * 1024):
        h.update(chunk)
        size += len(chunk)
    return size, h.hexdigest()


def verify(path):
    with tarfile.open(path, 'r:*') as package:
        members = package.getmembers()
        assert [s['quality'] for s in EXPECTED['datasets']] == list('clihf')
        for spec in EXPECTED['datasets']:
            payloads = [m for m in members if Path(m.name).name == spec['file']]
            assert len(payloads) == 1, f"Shoreline payload missing or duplicated: {spec['file']}"
            payload = payloads[0]
            assert payload.isfile() and '/data/shoreline/' in payload.name
            assert digest(package.extractfile(payload)) == (spec['archive_bytes'], spec['archive_sha256'])
            with gzip.GzipFile(fileobj=package.extractfile(payload)) as data:
                assert digest(data) == (spec['uncompressed_bytes'], spec['sha256'])
            directory = payload.name.rsplit('/', 1)[0]
        assert json.load(package.extractfile(directory + '/manifest.json')) == EXPECTED
        assert package.getmember(directory + '/LICENSE.LGPL-3').isfile()
    print('Verified all five packaged GSHHG 2.3.7 resolutions:', path)



if __name__ == '__main__':
    paths = []
    for argument in sys.argv[1:]:
        path = Path(argument)
        paths.extend(sorted(path.glob('*weather_routing_pi-*.tar.gz'))
                     if path.is_dir() else [path])
    if not paths:
        raise SystemExit('No plugin packages found for shoreline verification')
    for path in paths:
        verify(path)
