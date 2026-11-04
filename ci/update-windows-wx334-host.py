#!/usr/bin/env python3
"""Pin an official wx3.3.4 host only after it contains OpenCPN PR #5499."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import tempfile
from urllib.parse import urlparse
from urllib.request import urlopen

PIN = Path(__file__).with_name('windows64-sdk.json')
API = 'https://api.github.com/repos/OpenCPN/OpenCPN/'
# The maintainer cherry-picked #5499 and closed the PR, so GitHub's `merged`
# flag is false. Verify ancestry of the actual accepted upstream commit.
FIX_COMMIT = 'e41f0de5d579310dfd33ad1be36525c3c1a036aa'


def github(path):
    with urlopen(API + path, timeout=30) as response:
        return json.load(response)


def verify_upstream(revision, request=github):
    if not re.fullmatch(r'[0-9a-f]{40}', revision):
        raise ValueError('Use the full upstream core commit SHA')
    comparison = request('compare/' + FIX_COMMIT + '...' + revision)
    if comparison['status'] not in ('identical', 'ahead'):
        raise ValueError('The selected core revision does not contain the accepted #5499 fix')


def verify_url(url):
    parsed = urlparse(url)
    if parsed.scheme != 'https' or parsed.username or parsed.password:
        raise ValueError('Use an official HTTPS installer URL')
    cloudsmith = (parsed.netloc == 'dl.cloudsmith.io' and
                  re.match(r'^/public/david-register/opencpn-unstable(?:-x64)?/raw/files/',
                           parsed.path))
    release = (parsed.netloc == 'github.com' and
               parsed.path.startswith('/OpenCPN/OpenCPN/releases/download/'))
    if not (cloudsmith or release) or not parsed.path.endswith('_setup_x64.exe'):
        raise ValueError('Use an official OpenCPN x64 installer, not a validation rebuild')


def verify_image(host, revision):
    data = (host / 'opencpn.exe').read_bytes()
    if data[:2] != b'MZ':
        raise ValueError('Installer does not contain a Windows host')
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    if (data[offset:offset + 4] != b'PE\0\0' or
            struct.unpack_from('<H', data, offset + 4)[0] != 0x8664 or
            struct.unpack_from('<H', data, offset + 24)[0] != 0x20b):
        raise ValueError('Installer host is not AMD64 PE32+')
    if revision[:7].encode() not in data:
        raise ValueError('Installer version does not identify the selected core revision')
    runtimes = list(host.glob('wx*.dll'))
    if not runtimes or any(not re.fullmatch(
            r'wx(?:base|msw)334u(?:_[a-z0-9]+)?_vc14x_x64\.dll', p.name)
            for p in runtimes):
        raise ValueError('Installer does not exclusively contain the wxWidgets 3.3.4 x64 runtime')
    versions = re.findall(rb'5\.\d+\.\d+', data)
    if not versions:
        raise ValueError('Cannot identify the OpenCPN version in the installer')
    return versions[0].decode()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', required=True)
    parser.add_argument('--revision', required=True)
    parser.add_argument('--sha256', required=True)
    parser.add_argument('--check', action='store_true', help='Validate without changing the pin')
    args = parser.parse_args()
    verify_url(args.url)
    if not re.fullmatch(r'[0-9a-f]{64}', args.sha256):
        raise ValueError('Use the lowercase SHA256 of the official installer')
    verify_upstream(args.revision)
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        installer = root / 'host.exe'
        checksum = hashlib.sha256()
        with urlopen(args.url, timeout=120) as source, installer.open('wb') as target:
            for chunk in iter(lambda: source.read(1024 * 1024), b''):
                checksum.update(chunk)
                target.write(chunk)
        if checksum.hexdigest() != args.sha256:
            raise ValueError('Official installer checksum mismatch')
        host = root / 'host'
        subprocess.run(['7z', 'x', '-y', '-o' + str(host), str(installer)],
                       check=True, stdout=subprocess.DEVNULL)
        version = verify_image(host, args.revision)
    pin = json.loads(PIN.read_text())
    pin.update(host_url=args.url, core_revision=args.revision,
               host_sha256=args.sha256, host_version=version)
    text = json.dumps(pin, indent=2) + '\n'
    if args.check:
        print(text, end='')
    else:
        with tempfile.NamedTemporaryFile(mode='w', dir=PIN.parent, delete=False) as output:
            output.write(text)
            name = output.name
        os.replace(name, PIN)
        print('Updated official host pin. Commit it and rerun both wx3.3.4 jobs; '
              'their normal-shutdown checks remain required.')


if __name__ == '__main__':
    try:
        main()
    except ValueError as error:
        raise SystemExit(str(error))
