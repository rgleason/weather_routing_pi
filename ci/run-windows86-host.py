"""Run the existing Win32 package in the official OpenCPN 5.14.2 runtime."""
import hashlib
import importlib.util
from pathlib import Path
import subprocess
import urllib.request

root = Path(__file__).resolve().parents[1]
work = root / '.windows86-host'
work.mkdir(exist_ok=True)
installer = work / 'opencpn-5.14.2-x86.exe'
url = 'https://github.com/OpenCPN/OpenCPN/releases/download/Release_5.14.2/opencpn_5.14.2-0%2B4824.bd6986a_setup.exe'
expected = '99ed2ec5c0f6ae1b4fd680b271c59980f5cea962b3deac527dad17764330dc72'
if not installer.exists():
    urllib.request.urlretrieve(url, installer)
with installer.open('rb') as stream:
    if hashlib.file_digest(stream, 'sha256').hexdigest() != expected:
        raise RuntimeError('Official Win32 host checksum mismatch')
host = work / 'host'
subprocess.run(['7z', 'x', '-y', '-o' + str(host), str(installer)], check=True)
archives = list((root / 'build').glob('xweather_routing_pi-*.tar.gz'))
if len(archives) != 1:
    raise RuntimeError('Expected exactly one Win32 xWeatherRouting archive')
spec = importlib.util.spec_from_file_location('runtime', root / 'ci/test-windows-host.py')
runtime = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime)
runtime.test(host, archives[0], root / 'artifacts/windows-x86/runtime', root)
