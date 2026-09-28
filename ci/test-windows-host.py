"""Exercise the packaged plugin in an isolated genuine OpenCPN host process.

No installer, registry, existing app profile, or user interface automation is
used. The plugin's existing headless scenario interface performs a real route.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tarfile
import xml.etree.ElementTree as ET


def test(host, archive_path, output, source):
    host = host.resolve()
    output.mkdir(parents=True, exist_ok=True)
    package = 'xweather_routing_pi'
    with tarfile.open(archive_path) as archive:
        for member in archive.getmembers():
            parts = Path(member.name).parts
            if 'plugins' not in parts or not member.isfile():
                continue
            relative = Path(*parts[parts.index('plugins'):])
            if '..' in relative.parts:
                raise ValueError('Unsafe plugin archive')
            destination = host / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(archive.extractfile(member).read())
    if not (host / 'plugins' / (package + '.dll')).is_file():
        raise ValueError('Packaged plugin DLL is missing')
    binary = (host / 'opencpn.exe').read_bytes()
    version = re.search(rb'5\.14\.2-0\+[a-f0-9]+\x00', binary)
    if not version:
        raise ValueError('Unexpected host version string')
    full_version = version[0][:-1].decode()
    (host / 'opencpn.ini').write_text(
        '[Settings]\nConfigVersionString=Version ' + full_version + ' Build 2026-09-24\n'
        'NavMessageShown=1\nOpenGL=0\nDisableOpenGL=1\nShowMenuBar=1\n'
        '[PlugIns/grib_pi.dll]\nbEnabled=1\n'
        '[PlugIns/xweather_routing_pi.dll]\nbEnabled=1\n', encoding='ascii')
    # Absolute paths make the fixture independent of user polar directories.
    polar = host / 'plugins' / package / 'data/polars/Example/Test-TWS-0-20+60.pol'
    if not polar.is_file():
        raise ValueError('Packaged example polar is missing')
    boat = output / 'boat.xml'
    root = ET.Element('OpenCPNWeatherRoutingBoat', version='1.10', creator='CircleCI')
    ET.SubElement(root, 'Polar', FileName=str(polar), CrossOverPercentage='0')
    ET.ElementTree(root).write(boat, encoding='utf-8', xml_declaration=True)
    scenario = {
        'schemaVersion': 1, 'name': 'Upstream Windows host route acceptance',
        'start': {'name': 'Irish Sea offshore west', 'lat': 53.4, 'lon': -5.5},
        'end': {'name': 'Irish Sea offshore east', 'lat': 53.4, 'lon': -5.3},
        'startTime': '2026-09-28T00:00:00Z',
        'departureOptimization': {'enabled': False},
        'environment': {'useGrib': True, 'useCurrents': False, 'allowClimatologyFallback': False},
        'route': {'routingEngine': 'original', 'boatFile': str(boat.resolve()),
                  'timeStepSeconds': 900, 'headingStepDegrees': 5},
        'safety': {'mode': 'none', 'enforce': False},
    }
    scenario_path = output / 'scenario.json'
    scenario_path.write_text(json.dumps(scenario, indent=2) + '\n')
    result_path = output / 'route-result.json'
    env = dict(os.environ, WR_HEADLESS_ROUTE_TEST='scenario',
               WR_HEADLESS_SCENARIO=str(scenario_path.resolve()),
               WR_HEADLESS_OUTPUT=str(result_path.resolve()),
               WR_HEADLESS_GRIB_FILE=str((source / 'ci/testdata/irish-sea-uniform-wind.grb').resolve()),
               WR_HEADLESS_TIMEOUT_MS='90000',
               WR_HEADLESS_DATA_DIR=str((output / 'plugin-profile').resolve()))
    (output / 'plugin-profile').mkdir(exist_ok=True)
    log = host / 'opencpn.log'
    try:
        with (output / 'host-stdout.log').open('w') as stdout:
            process = subprocess.Popen([str(host / 'opencpn.exe'), '-p'],
                                       cwd=host, env=env, stdout=stdout, stderr=subprocess.STDOUT)
            try:
                code = process.wait(timeout=120)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                raise RuntimeError('Genuine OpenCPN host route test timed out')
        if not result_path.is_file():
            raise RuntimeError(f'Host produced no route result (exit {code})')
        result = json.loads(result_path.read_text())
        if code != 0 or result.get('status') != 'complete':
            raise RuntimeError(f'Host route failed: exit={code}; result={result}')
        (output / 'acceptance.json').write_text(json.dumps({
            'host': full_version, 'isolated_portable_profile': True,
            'package': archive_path.name, 'result_status': result['status'],
            'minimum_api': '1.21', 'exit_code': code,
        }, indent=2) + '\n')
        print('Genuine upstream host loaded the packaged plugin and completed its route', flush=True)
    finally:
        if log.is_file():
            shutil.copy2(log, output / 'opencpn.log')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', type=Path)
    parser.add_argument('archive', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    test(args.host, args.archive, args.output, Path(__file__).resolve().parents[1])
