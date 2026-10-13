#!/usr/bin/env python3
"""Exercise the final package in the checksum-pinned official macOS host."""
import importlib.util
from pathlib import Path
import plistlib
import shutil
import subprocess


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


root = Path(__file__).resolve().parents[1]
work = root / '.macos-host'
package = module('fetch_stock_host', root / 'ci/fetch-stock-host.py').fetch('macos-arm64', work / 'cache')
expanded = work / 'expanded'
if not expanded.exists():
    subprocess.run(['pkgutil', '--expand-full', str(package), str(expanded)], check=True)
apps = []
for info in expanded.rglob('Info.plist'):
    if info.parent.name != 'Contents':
        continue
    with info.open('rb') as stream:
        properties = plistlib.load(stream)
    executable = properties.get('CFBundleExecutable')
    if executable and executable.lower() == 'opencpn' and (
            info.parent / 'MacOS' / executable).is_file():
        apps.append((info.parent.parent, executable))
if len(apps) != 1:
    raise RuntimeError('Official package did not contain one OpenCPN application payload; candidates=' +
                       repr([str(path.relative_to(expanded)) for path, _ in apps]))
payload, executable = apps[0]
# A component package can install directly into /Applications/OpenCPN.app;
# pkgutil then calls the expanded bundle root Payload rather than OpenCPN.app.
app = work / 'host/OpenCPN.app'
if not app.exists():
    app.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(payload, app, symlinks=True)
host = app / 'Contents/MacOS' / executable
cli = app / 'Contents/MacOS/opencpn-cmd'
archives = list((root / 'build-package').glob('*weather_routing_pi-*.tar.gz'))
if len(archives) != 1:
    raise RuntimeError('Expected exactly one final macOS plugin archive')
module('installed_host', root / 'ci/test-installed-host.py').test(
    host, cli, archives[0], root / 'artifacts/macos-arm64/normal-runtime', root)
