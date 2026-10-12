#!/usr/bin/env python3
"""Download a checksum-pinned upstream OpenCPN test host or source archive."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request


def fetch(target, directory):
    pin = json.loads(Path(__file__).with_name('stock-hosts.json').read_text())[target]
    directory.mkdir(parents=True, exist_ok=True)
    destination = directory / pin['filename']
    if not destination.exists():
        temporary = destination.with_suffix(destination.suffix + '.download')
        urllib.request.urlretrieve(pin['url'], temporary)
        temporary.replace(destination)
    checksum = hashlib.sha256()
    with destination.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            checksum.update(chunk)
    if checksum.hexdigest() != pin['sha256']:
        raise RuntimeError('Pinned upstream host checksum mismatch: ' + destination.name)
    return destination.resolve()


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('target', choices=('linux-source', 'macos-arm64'))
    parser.add_argument('directory', type=Path)
    arguments = parser.parse_args()
    print(fetch(arguments.target, arguments.directory))
