#!/usr/bin/env python3
"""Import the final package and exercise a normal stock OpenCPN installation.

Linux uses a private HOME. Windows/macOS run in disposable CI VMs, with the
normal system/user config locations (never on a personal PC).
No portable flag, plugin path override or plugin data/cache override is used.
"""
import argparse
from contextlib import contextmanager
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import selectors
import shutil
import subprocess
import sys
import tarfile
import xml.etree.ElementTree as ET


def digest(path):
    checksum = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            checksum.update(chunk)
    return checksum.hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def host_build(binary):
    versions = re.findall(rb'5\.14\.2(?:-[A-Za-z0-9._]+)?(?:\+[A-Za-z0-9._]+)?\x00', binary)
    dates = re.findall(rb'20\d{2}-\d{2}-\d{2}\x00', binary)
    require(versions and dates, 'Cannot determine the pinned stock host version/build date')
    return max(versions, key=len)[:-1].decode(), max(dates)[:-1].decode()


@contextmanager
def display(environment, output):
    if sys.platform != 'linux':
        yield environment
        return
    read_fd, write_fd = os.pipe()
    with (output / 'xvfb.log').open('w') as log:
        server = subprocess.Popen(['Xvfb', '-displayfd', str(write_fd),
                                   '-screen', '0', '1280x900x24', '-nolisten', 'tcp', '-ac'],
                                  pass_fds=(write_fd,), stdout=log, stderr=subprocess.STDOUT)
        os.close(write_fd)
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(read_fd, selectors.EVENT_READ)
                require(bool(selector.select(10)), 'Private Xvfb did not start')
            number = os.read(read_fd, 128).decode().strip()
            require(number.isdigit(), 'Private Xvfb returned no display number')
            yield dict(environment, DISPLAY=':' + number)
        finally:
            os.close(read_fd)
            server.terminate()
            server.wait(timeout=5)


def profile(output, identity):
    environment = dict(os.environ)
    for name in ('WR_HEADLESS_DATA_DIR', 'WR_SHORELINE_DATA_HOME',
                 'OPENCPN_PLUGIN_DIRS', 'OPENCPN_COMPAT_TARGET',
                 'FLATPAK_ID', 'OCPN_TEST_HOMEDIR', 'WR_HEADLESS_ROUTE_TEST',
                 'WR_HEADLESS_SCENARIO', 'WR_HEADLESS_OUTPUT', 'WR_HEADLESS_GRIB_FILE'):
        environment.pop(name, None)
    if sys.platform == 'darwin':
        require(os.environ.get('CIRCLECI') == 'true',
                'Native macOS normal-profile tests require a disposable CircleCI VM')
        # Cocoa/wx standard paths can use the account's home independently of
        # HOME. Exercise the real account in the disposable VM.
        home = Path.home()
    else:
        home = output / 'user-home'
        home.mkdir()
    environment.update(HOME=str(home), XDG_CONFIG_HOME=str(home / '.config'),
                       XDG_DATA_HOME=str(home / '.local/share'),
                       XDG_CACHE_HOME=str(home / '.cache'),
                       XDG_DATA_DIRS='/usr/local/share:/usr/share', NO_AT_BRIDGE='1')
    if sys.platform == 'win32':
        require(os.environ.get('CIRCLECI') == 'true',
                'Native Windows normal-profile tests require a disposable CircleCI VM')
        local = output / 'local-appdata'
        roaming = output / 'roaming-appdata'
        environment.update(LOCALAPPDATA=str(local), APPDATA=str(roaming))
        buffer = ctypes.create_unicode_buffer(32768)
        status = ctypes.windll.shell32.SHGetFolderPathW(None, 35, None, 0, buffer)
        require(status == 0, 'Cannot locate the default Windows common-app-data directory')
        config = Path(buffer.value) / 'opencpn'
        library = local / 'opencpn/plugins' / (identity + '.dll')
        data = library.parent / identity
        config_file = config / 'opencpn.ini'
    elif sys.platform == 'darwin':
        config = home / 'Library/Preferences/opencpn'
        base = home / 'Library/Application Support/OpenCPN/Contents'
        library = base / 'PlugIns' / ('lib' + identity + '.dylib')
        data = base / 'SharedSupport/plugins' / identity
        config_file = config / 'opencpn.ini'
    else:
        config = home / '.opencpn'
        library = home / '.local/lib/opencpn' / ('lib' + identity + '.so')
        data = home / '.local/share/opencpn/plugins' / identity
        config_file = config / 'opencpn.conf'
    require(not config_file.exists(), 'Refusing to replace an existing OpenCPN profile')
    config.mkdir(parents=True, exist_ok=True)
    return environment, config, config_file, library, data


def test(host, cli, archive_path, output, source):
    host, cli, archive_path, output, source = (p.resolve() for p in
                                             (host, cli, archive_path, output, source))
    output.mkdir(parents=True, exist_ok=False)
    with tarfile.open(archive_path) as archive:
        metadata = ET.fromstring(archive.extractfile('metadata.xml').read())
        identity = ('xweather_routing_pi' if metadata.findtext('name').strip() ==
                    'xWeatherRouting' else 'weather_routing_pi')
        assets = {}
        for entry in archive.getmembers():
            if not entry.isfile():
                continue
            marker = '/plugins/' + identity + '/data/'
            if marker in entry.name:
                assets[entry.name.split(marker, 1)[1]] = hashlib.sha256(
                    archive.extractfile(entry).read()).hexdigest()
        catalogs = {entry.name: hashlib.sha256(archive.extractfile(entry).read()).hexdigest()
                    for entry in archive.getmembers() if entry.isfile() and entry.name.endswith('.mo')}
    require(assets and catalogs, 'Final package lacks bundled data or translations')
    environment, config, config_file, library, data = profile(output, identity)
    environment['PATH'] = str(host.parent) + os.pathsep + environment.get('PATH', '')
    version, build_date = host_build(host.read_bytes())
    config_version = int(metadata.findtext('version').strip().split('.')[0]) * 100 + int(
        metadata.findtext('version').strip().split('.')[1])
    plugin_key = library.name
    grib_key = 'grib_pi.dll' if sys.platform == 'win32' else 'libgrib_pi.' + (
        'dylib' if sys.platform == 'darwin' else 'so')
    header = ('[Settings]\nConfigVersionString=Version ' + version + ' Build ' + build_date +
              '\nNavMessageShown=1\nOpenGL=0\nDisableOpenGL=1\nShowMenuBar=1\nDataConnections=\n'
              '[PlugIns/' + plugin_key + ']\nbEnabled=1\n[PlugIns/' + grib_key + ']\nbEnabled=1\n'
              '[Plugins/WeatherRouting]\nConfigVersion=' + str(config_version) + '\n'
              '[PlugIns/WeatherRouting]\nConfigVersion=' + str(config_version) + '\n')
    config_file.write_text(header, encoding='utf-8')

    def command(name, arguments, command_environment=None):
        result = subprocess.run([str(cli)] + arguments,
                                env=command_environment or environment, cwd=host.parent,
                                text=True, capture_output=True, timeout=90)
        (output / (name + '.log')).write_text(result.stdout + result.stderr, encoding='utf-8')
        require(result.returncode == 0, 'Stock host command failed: ' + name)
        return result.stdout.strip()

    abi = command('host-abi', ['print-abi'])
    expected_abi = metadata.findtext('target').strip() + ':' + metadata.findtext('target-version').strip()
    # Host compatibility checks permit Windows build-number and older macOS
    # target versions. Import must make that decision; do not override its ABI.
    if sys.platform == 'linux':
        require(abi == expected_abi, 'Package ABI differs from the actual Linux host: ' + abi)
    command('import', ['import-plugin', str(archive_path)])
    require(library.is_file(), 'Normal import did not install the plugin library')
    for relative, expected in assets.items():
        installed = data / 'data' / relative
        require(installed.is_file() and digest(installed) == expected,
                'Normal import lost or changed bundled asset: ' + relative)
    for name, expected in catalogs.items():
        if sys.platform == 'darwin':
            relative = name.split('/Contents/Resources/', 1)[1]
            installed = data.parents[2] / 'Resources' / relative
        else:
            relative = name.split('/share/locale/', 1)[1]
            share = (library.parent.parent / 'share' if sys.platform == 'win32'
                     else output / 'user-home/.local/share')
            installed = share / 'locale' / relative
        require(installed.is_file() and digest(installed) == expected,
                'Normal import lost or changed translation catalog: ' + relative)
    require((data / 'data/WeatherRoutingInformation.html').is_file(), 'Installed help is missing')

    private_data = config / 'plugins/weather_routing'
    private_data.mkdir(parents=True)
    # Empty route configurations keep demo voyages out of the scenario. Boats
    # and polars remain absent, so the normal first-launch copy must populate them.
    ET.ElementTree(ET.Element('OpenCPNWeatherRoutingConfiguration', version='1.10')).write(
        private_data / 'WeatherRoutingConfiguration.xml')
    polar = private_data / 'polars/Example/Test-TWS-0-20+60.pol'
    boat = output / 'scenario-boat.xml'
    boat_xml = ET.Element('OpenCPNWeatherRoutingBoat', version='1.10', creator='Normal installation CI')
    ET.SubElement(boat_xml, 'Polar', FileName=str(polar), CrossOverPercentage='0')
    ET.ElementTree(boat_xml).write(boat, encoding='utf-8', xml_declaration=True)
    receipts = []
    with display(environment, output) as gui_environment:
        def route(engine, land, label):
            scenario = dict(schemaVersion=1, name='Normal stock host ' + label,
                start=dict(name='offshore west', lat=53.4, lon=-5.5),
                end=dict(name='offshore east', lat=53.4, lon=-5.3),
                startTime='2026-09-28T00:00:00Z', departureOptimization=dict(enabled=False),
                environment=dict(useGrib=True, useCurrents=False, allowClimatologyFallback=False),
                route=dict(routingEngine=engine, shorelineResolution=2, boatFile=str(boat),
                           timeStepSeconds=900, headingStepDegrees=5, maxSwellMeters=0),
                safety=dict(mode='gshhs' if land else 'none', enforce=False))
            scenario_file, result_file = output / (label + '-scenario.json'), output / (label + '-result.json')
            scenario_file.write_text(json.dumps(scenario, indent=2) + '\n')
            route_environment = dict(gui_environment, WR_HEADLESS_ROUTE_TEST='scenario',
                WR_HEADLESS_SCENARIO=str(scenario_file), WR_HEADLESS_OUTPUT=str(result_file),
                WR_HEADLESS_GRIB_FILE=str(source / 'ci/testdata/irish-sea-uniform-wind.grb'),
                WR_HEADLESS_TIMEOUT_MS='90000')
            with (output / (label + '-stdout.log')).open('w') as stdout:
                process = subprocess.Popen([str(host), '--no_opengl'], env=route_environment,
                                           cwd=host.parent, stdout=stdout, stderr=subprocess.STDOUT)
                try:
                    code = process.wait(timeout=120)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    if (config / 'opencpn.log').exists():
                        shutil.copy2(config / 'opencpn.log', output / (label + '-opencpn.log'))
                    raise RuntimeError('Normal installed host route timed out: ' + label)
            log_file = config / 'opencpn.log'
            if log_file.exists():
                shutil.copy2(log_file, output / (label + '-opencpn.log'))
            require(result_file.is_file(), 'Normal host produced no route result: ' + label)
            result = json.loads(result_file.read_text())
            require(code == 0 and result.get('status') == 'complete', 'Normal host route failed: ' + label)
            require(bool(result.get('candidates')), 'Normal host route has no candidate: ' + label)
            shoreline = result['candidates'][0].get('shoreline', {})
            require(shoreline.get('enabled') == land and (not land or
                    (shoreline.get('resolution') == 2 and bool(shoreline.get('dataset')))),
                    'Normal host did not use the requested shoreline checks: ' + label)
            log = log_file.read_text(encoding='utf-8', errors='replace').replace('\\', '/')
            require('using data dir: ' + str(data).replace('\\', '/') in log,
                    'Stock GetPluginDataDir did not resolve the normal installation: ' + label)
            lifecycle = route_environment.get('WR_HEADLESS_LIFECYCLE_CONTRACT') == '1'
            require(not lifecycle or 'WR_ROUTE_LIFECYCLE recompute_cycles=3 result=passed' in log,
                    'Normal host route lifecycle failed: ' + label)
            require(polar.is_file() and digest(polar) == assets['polars/Example/Test-TWS-0-20+60.pol'],
                    'Normal first launch did not populate user polars')
            require((private_data / 'boats/Boat.xml').is_file(), 'Normal first launch did not populate user boats')
            if land:
                require(any(path.stat().st_size > 0 for path in
                            (private_data / 'shoreline/2.3.7').glob('poly-i-*.dat')),
                        'Shoreline data was not written to the normal private data directory')
            receipts.append(dict(case=label, engine=engine, detect_land=land, exit_code=code,
                                 status=result['status'], route_lifecycle_checked=lifecycle))

        for engine in ('original', 'quick', 'main'):
            for land in (False, True):
                label = engine + ('-land' if land else '-offshore')
                route(engine, land, label)
                print('Normal installed host passed ' + label, flush=True)

        personal = [private_data / 'boats/Personal.xml', private_data / 'polars/Personal.pol',
                    private_data / 'boats/Boat.xml', private_data / 'polars/J29.pol']
        personal[0].write_bytes(boat.read_bytes())
        personal[1].write_bytes(polar.read_bytes())
        with personal[2].open('a') as stream:
            stream.write('\n<!-- CI personal changes to a bundled boat -->\n')
        with personal[3].open('a') as stream:
            stream.write('\n')
        snapshots = {str(path): digest(path) for path in personal}
        command('reimport', ['import-plugin', str(archive_path)])
        route('main', True, 'reinstall-restart')
        require(all(path.is_file() and digest(path) == snapshots[str(path)] for path in personal),
                'Reinstall/restart overwrote personal boats or polars')

    (output / 'acceptance.json').write_text(json.dumps(dict(
        stock_host=version, host_build_date=build_date, host_abi=abi,
        package=archive_path.name, package_sha256=digest(archive_path), identity=identity,
        normal_profile=True, portable=False, imported_by_stock_host=True,
        data_lookup_checked=True, bundled_assets_checked=len(assets),
        translation_catalogs_installed=len(catalogs), help_installed=True,
        private_data_overrides=False, private_data=str(private_data),
        reinstall_preserves_personal_data=True, uninstall_checked=False,
        uninstall_limitation='Stock 5.14.2 CLI has an unset plugin activation callback',
        routes=receipts), indent=2) + '\n')
    print('Normal installation, shoreline routes and reinstall passed', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('host', type=Path)
    parser.add_argument('cli', type=Path)
    parser.add_argument('archive', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    test(args.host, args.cli, args.archive, args.output, Path(__file__).resolve().parents[1])
