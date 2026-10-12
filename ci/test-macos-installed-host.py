#!/usr/bin/env python3
"""Exercise the final package in the checksum-pinned official macOS host."""
import importlib.util
from pathlib import Path
import plistlib
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
apps = list(expanded.rglob('OpenCPN.app'))
if len(apps) != 1:
    raise RuntimeError('Official host package did not contain exactly one OpenCPN.app')
app = apps[0]
with (app / 'Contents/Info.plist').open('rb') as stream:
    executable = plistlib.load(stream)['CFBundleExecutable']
host = app / 'Contents/MacOS' / executable
cli = app / 'Contents/MacOS/opencpn-cmd'
archives = list((root / 'build-package').glob('*weather_routing_pi-*.tar.gz'))
if len(archives) != 1:
    raise RuntimeError('Expected exactly one final macOS plugin archive')
module('installed_host', root / 'ci/test-installed-host.py').test(
    host, cli, archives[0], root / 'artifacts/macos-arm64/normal-runtime', root)
