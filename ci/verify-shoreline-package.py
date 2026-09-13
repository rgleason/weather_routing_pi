#!/usr/bin/env python3
"""Verify the actual packaged offline GSHHG payload on every platform."""
import gzip
import hashlib
import json
import sys
import tarfile
from pathlib import Path

ARCHIVE_HASH = 'a36cb8c4fda7d56cfd851d92ed70a315d0a2ff81e10e99e8a0141ce9dc4e6d60'
PAYLOAD_HASH = '8d4d73897c82dd0e8df63f33e4dab9dd3aea7a26459b923bf404cb9299f1cf04'
NAME = 'poly-f-2.3.7.dat.gz'


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
        payloads = [m for m in members if Path(m.name).name == NAME]
        assert len(payloads) == 1, 'Full shoreline payload missing or duplicated'
        payload = payloads[0]
        assert payload.isfile() and '/data/shoreline/' in payload.name, payload.name
        assert digest(package.extractfile(payload)) == (57754863, ARCHIVE_HASH)
        with gzip.GzipFile(fileobj=package.extractfile(payload)) as data:
            assert digest(data) == (171582632, PAYLOAD_HASH)
        directory = payload.name.rsplit('/', 1)[0]
        manifest = json.load(package.extractfile(directory + '/manifest.json'))
        full = next(d for d in manifest['datasets'] if d['quality'] == 'f')
        assert full['version'] == '2.3.7' and full['sha256'] == PAYLOAD_HASH
        assert full['archive_sha256'] == ARCHIVE_HASH
        assert package.getmember(directory + '/LICENSE.LGPL-3').isfile()
    print('Verified packaged full-resolution GSHHG 2.3.7:', path)


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
