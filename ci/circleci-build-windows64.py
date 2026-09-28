"""Build against the pinned upstream OpenCPN 5.14.2 AMD64 testing host.

Generate import libraries from that host's actual export tables rather than
reusing the x86 libraries shipped in opencpn-libs. The host is only extracted,
never installed, so the executor's existing OpenCPN profile is unaffected.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tarfile
import urllib.request
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / '.windows64'
ARTIFACTS = ROOT / 'artifacts/windows-x64'


def run(*args, cwd=ROOT):
    print('+', ' '.join(map(str, args)), flush=True)
    subprocess.run(list(map(str, args)), cwd=cwd, check=True)


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def download(url, path, expected):
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists():
        partial = path.with_suffix(path.suffix + '.part')
        with urllib.request.urlopen(url, timeout=120) as response, partial.open('wb') as output:
            shutil.copyfileobj(response, output)
        if digest(partial) != expected:
            raise RuntimeError(f'Checksum mismatch: {path.name}')
        partial.replace(path)
    if digest(path) != expected:
        raise RuntimeError(f'Checksum mismatch: {path.name}')


def pe_sections(path):
    data = path.read_bytes()
    if data[:2] != b'MZ':
        raise ValueError(f'Not a PE image: {path}')
    offset = struct.unpack_from('<I', data, 0x3c)[0]
    if data[offset:offset + 4] != b'PE\0\0':
        raise ValueError(f'Invalid PE signature: {path}')
    machine, count = struct.unpack_from('<HH', data, offset + 4)
    optional_size = struct.unpack_from('<H', data, offset + 20)[0]
    magic = struct.unpack_from('<H', data, offset + 24)[0]
    if machine != 0x8664 or magic != 0x20b:
        raise ValueError(f'Expected AMD64 PE32+: {path}')
    sections = []
    for index in range(count):
        at = offset + 24 + optional_size + index * 40
        size, rva = struct.unpack_from('<II', data, at + 8)
        flags = struct.unpack_from('<I', data, at + 36)[0]
        sections.append((rva, rva + size, bool(flags & 0x20000000)))
    return sections


def exports(path):
    sections = pe_sections(path)
    text = subprocess.check_output(['dumpbin', '/exports', str(path)], text=True)
    result = {}
    for line in text.splitlines():
        match = re.match(r'\s*\d+\s+[0-9A-Fa-f]+\s+([0-9A-Fa-f]+)\s+(\S+)', line)
        if match:
            rva = int(match[1], 16)
            code = any(start <= rva < end and executable for start, end, executable in sections)
            result[match[2]] = '' if code else ' DATA'
    if not result:
        raise RuntimeError(f'No named exports: {path}')
    return result


def import_library(image, destination):
    names = exports(image)
    definition = destination.with_suffix('.def')
    definition.write_text('LIBRARY ' + image.name + '\nEXPORTS\n' +
                          '\n'.join(name + kind for name, kind in sorted(names.items())) + '\n')
    run('lib', '/nologo', '/machine:x64', '/def:' + str(definition), '/out:' + str(destination))
    return names


def prepare_sdk():
    pin = json.loads((ROOT / 'ci/windows64-sdk.json').read_text())
    cache = WORK / 'cache'
    host_archive = cache / 'opencpn-5.14.2-x64.exe'
    download(pin['host_url'], host_archive, pin['host_sha256'])
    host = WORK / 'host'
    run('7z', 'x', '-y', '-o' + str(host), host_archive)
    sdk = WORK / 'sdk'
    sdk.mkdir(parents=True, exist_ok=True)
    host_exports = import_library(host / 'opencpn.exe', sdk / 'opencpn.lib')
    import_library(host / 'z.dll', sdk / 'z.lib')
    wx = WORK / 'wxWidgets-3.2.9'
    for filename, expected in pin['wx_archives'].items():
        path = cache / filename
        download('https://github.com/wxWidgets/wxWidgets/releases/download/v3.2.9/' + filename,
                 path, expected)
        run('7z', 'x', '-y', '-o' + str(wx), path)
    wxlib = wx / 'lib/vc14x_x64_dll'
    for image in wxlib.glob('*.dll'):
        pe_sections(image)
        # CI tests use the same wx runtime bytes as the matched upstream host.
        counterpart = host / image.name
        if counterpart.is_file() and digest(image) != digest(counterpart):
            raise RuntimeError(f'Host/wxWidgets runtime mismatch: {image.name}')
    os.environ['PATH'] = str(host) + os.pathsep + str(wxlib) + os.pathsep + os.environ['PATH']
    os.environ['WX_VER'] = '32'
    os.environ['OCPN_TARGET'] = 'MSVC-x64'
    (ARTIFACTS / 'sdk-provenance.json').write_text(json.dumps(pin, indent=2) + '\n')
    return sdk, wx, wxlib, host_exports


def verify_host_imports(library, host_exports):
    pe_sections(library)
    text = subprocess.check_output(['dumpbin', '/imports', str(library)], text=True)
    (ARTIFACTS / 'plugin-imports.txt').write_text(text)
    active = False
    names = []
    for line in text.splitlines():
        if line.strip().lower().endswith(('.dll', '.exe')):
            active = line.strip().lower() == 'opencpn.exe'
        elif active:
            match = re.match(r'\s*[0-9A-Fa-f]+\s+(\S+)', line)
            if match and match[1].startswith('?'):
                names.append(match[1])
    if not names:
        raise RuntimeError('No OpenCPN host imports were found')
    missing = sorted(set(names) - host_exports.keys())
    if missing:
        raise RuntimeError(f'Imports missing from matched host: {missing}')
    print(f'All {len(names)} OpenCPN imports resolve against upstream 5.14.2 x64', flush=True)


def main():
    if sys.platform != 'win32':
        raise SystemExit('Run this script in a native Windows x64 MSVC environment')
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    sdk, wx, wxlib, host_exports = prepare_sdk()
    work = WORK / 'build'
    identity = os.environ.get('WEATHER_ROUTING_CI_XWEATHER', 'true').lower() == 'true'
    package = 'xweather_routing_pi' if identity else 'weather_routing_pi'
    headers = os.environ.get('WEATHER_ROUTING_API_HEADER_VERSION', '1.21')
    run('cmake', '-S', ROOT, '-B', work, '-G', 'Visual Studio 17 2022', '-A', 'x64',
        '-DCMAKE_SYSTEM_VERSION=10.0', '-DCMAKE_BUILD_TYPE=Release', '-DOCPN_BUILD_TEST=ON',
        '-DWEATHER_ROUTING_STANDALONE_API=ON',
        '-DWEATHER_ROUTING_API_HEADER_VERSION=' + headers,
        '-DWEATHER_ROUTING_XWEATHER_IDENTITY=' + ('ON' if identity else 'OFF'),
        '-DWEATHER_ROUTING_WINDOWS_IMPORT_LIBRARY=' + str(sdk / 'opencpn.lib'),
        '-DWEATHER_ROUTING_WINDOWS_ZLIB_LIBRARY=' + str(sdk / 'z.lib'),
        '-DwxWidgets_ROOT_DIR=' + str(wx), '-DwxWidgets_LIB_DIR=' + str(wxlib),
        '-DwxWidgets_CONFIGURATION=mswu', '-DCMAKE_CXX_FLAGS=/we4302 /we4311 /we4312')
    run('cmake', '--build', work, '--config', 'Release', '--parallel', '4')
    tests = ARTIFACTS / 'tests'
    tests.mkdir(exist_ok=True)
    run('ctest', '--test-dir', work, '-C', 'Release', '--output-on-failure',
        '--no-tests=error', '--timeout', '180', '--output-junit', tests / 'ctest.xml')
    run('cmake', '--install', work, '--config', 'Release', '--prefix', work / 'stage')
    run('cpack', '-G', 'TGZ', '-C', 'Release', '--config', 'CPackConfig.cmake', cwd=work)
    archives = list(work.glob(package + '-*.tar.gz'))
    metadata = list(work.glob(package + '-*.xml'))
    if len(archives) != 1 or len(metadata) != 1:
        raise RuntimeError('Expected exactly one archive and metadata pair')
    root = ET.parse(metadata[0]).getroot()
    actual = [(root.findtext(k) or '').strip() for k in ('target', 'target-version', 'target-arch', 'api-version')]
    if actual != ['msvc-64', '10.0', 'x86_64', '1.21']:
        raise RuntimeError(f'Incorrect x64 package metadata: {actual}')
    # Normalize the upstream Windows target's version to its catalogue value.
    root.find('target-version').text = '10'
    ET.ElementTree(root).write(metadata[0], encoding='utf-8', xml_declaration=True)
    run(sys.executable, ROOT / 'ci/embed-package-metadata.py', archives[0], metadata[0], archives[0])
    run(sys.executable, ROOT / 'ci/verify-shoreline-package.py', archives[0])
    with tarfile.open(archives[0]) as archive:
        dlls = [m for m in archive.getmembers() if m.name.endswith('.dll')]
        if len(dlls) != 1 or Path(dlls[0].name).name != package + '.dll':
            raise RuntimeError('Package contains unexpected Windows libraries')
        image = WORK / (package + '.dll')
        image.write_bytes(archive.extractfile(dlls[0]).read())
        verify_host_imports(image, host_exports)
    destination = ARTIFACTS / 'package'
    destination.mkdir(exist_ok=True)
    for path in (archives[0], metadata[0]):
        shutil.copy2(path, destination)
    (destination / 'SHA256SUMS').write_text('\n'.join(
        digest(path) + '  ' + path.name for path in sorted(destination.iterdir()) if path.name != 'SHA256SUMS') + '\n')
    shutil.copy2(work / 'CMakeCache.txt', ARTIFACTS)
    run(sys.executable, ROOT / 'ci/test-windows-host.py', WORK / 'host',
        archives[0], ARTIFACTS / 'runtime')


if __name__ == '__main__':
    main()
