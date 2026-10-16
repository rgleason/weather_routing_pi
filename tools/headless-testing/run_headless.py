#!/usr/bin/env python3
"""Run single-route xWeatherRouting JSON scenarios in a prepared test profile."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import copy
import hashlib
import json
import math
import os
import selectors
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path
from extract_timings import extract

ENGINES = {'quick': 'original', 'quick2': 'quick2', 'alternative': 'alternative',
           'standard': 'quick', 'professional': 'main',
           'auto': 'auto', 'all': 'all', 'atlas': 'atlas'}


def stop(process):
    if process.poll() is not None:
        return
    if os.name == 'posix':
        os.killpg(process.pid, signal.SIGTERM)
    else:
        process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        if os.name == 'posix':
            os.killpg(process.pid, signal.SIGKILL)
        else:
            process.kill()
        process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--opencpn', required=True, type=Path,
                        help='Executable or launcher which forwards command-line arguments')
    parser.add_argument('--profile', required=True, type=Path,
                        help='Existing, separately prepared test profile containing opencpn.conf')
    parser.add_argument('--scenario', required=True, type=Path)
    parser.add_argument('--grib', required=True, type=Path)
    parser.add_argument('--boat', type=Path, help='Override route.boatFile')
    parser.add_argument('--output', required=True, type=Path, help='New output directory')
    parser.add_argument('--engines', nargs='+', choices=ENGINES, default=['all'])
    parser.add_argument('--timeout', type=int, default=900,
                        help='Plugin time limit in seconds, including preparation (default: 900)')
    parser.add_argument('--plugin-dir', action='append', type=Path,
                        help='Optional plugin-library search directory; repeat as required')
    parser.add_argument('--xvfb', action='store_true', help='Use a private virtual X server on Linux')
    parser.add_argument('--xvfb-binary', type=Path, help='Optional explicit Xvfb executable')
    args = parser.parse_args()
    for key in ['opencpn', 'profile', 'scenario', 'grib', 'output']:
        setattr(args, key, getattr(args, key).expanduser().resolve())
    if not args.opencpn.is_file() or not os.access(args.opencpn, os.X_OK):
        parser.error('--opencpn must be an executable file')
    if not (args.profile / 'opencpn.conf').is_file():
        parser.error('Prepare a separate test profile before running this script')
    if args.profile == (Path.home() / '.opencpn').resolve():
        parser.error('Use a separate test profile, not ~/.opencpn')
    if not args.grib.is_file() or not args.scenario.is_file():
        parser.error('Scenario and GRIB files must exist')
    if args.output.exists():
        parser.error('Output directory must be new, to preserve earlier evidence')
    if not 1 <= args.timeout <= 86400:
        parser.error('--timeout must be between 1 and 86400 seconds')
    scenario = json.loads(args.scenario.read_text())
    if scenario.get('departureOptimization', {}).get('enabled', False):
        parser.error('This wrapper compares single routes; disable departureOptimization')
    for label in ['start', 'end']:
        point = scenario.get(label, {})
        lat, lon = point.get('lat'), point.get('lon')
        if not isinstance(lat, (int, float)) or not isinstance(lon, (int, float)) \
                or not math.isfinite(lat) or not math.isfinite(lon) \
                or not -90 <= lat <= 90 or not -180 <= lon <= 180:
            parser.error('Scenario requires finite start/end lat/lon coordinates')
    if not scenario.get('startTime', '').endswith('Z'):
        parser.error('Use an explicit UTC startTime ending in Z')
    route = scenario.setdefault('route', {})
    boat = args.boat or route.get('boatFile')
    if not boat:
        parser.error('Provide --boat or route.boatFile; do not rely on an implicit polar')
    boat = Path(boat).expanduser()
    if not boat.is_absolute():
        boat = (Path.cwd() if args.boat else args.scenario.parent) / boat
    boat = boat.resolve()
    if not boat.is_file():
        parser.error('Boat XML must exist; its polar paths must also be valid')
    route['boatFile'] = str(boat)
    digest = hashlib.sha256()
    with args.grib.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(chunk)
    grib_sha256 = digest.hexdigest()
    scenario.setdefault('departureOptimization', {})['enabled'] = False
    args.output.mkdir(parents=True)
    env = {key: value for key, value in os.environ.items()
           if not key.startswith('WR_HEADLESS_')}
    env.update(WR_USE_LEGACY_ENGINE='0', WR_ROUTING_PREVIEW='0', TZ='UTC')
    if args.plugin_dir:
        env['OPENCPN_PLUGIN_DIRS'] = os.pathsep.join(
            str(path.expanduser().resolve()) for path in args.plugin_dir)
    server = None
    records = []
    try:
        if args.xvfb:
            executable = str(args.xvfb_binary.expanduser().resolve()) \
                if args.xvfb_binary else shutil.which('Xvfb')
            if not executable:
                parser.error('Xvfb is required for --xvfb; install it or specify --xvfb-binary')
            if os.name != 'posix':
                parser.error('--xvfb requires a Unix platform')
            read_fd, write_fd = os.pipe()
            with (args.output / 'xvfb.log').open('wb') as log:
                server = subprocess.Popen(
                    [executable, '-displayfd', str(write_fd), '-screen', '0',
                     '1440x1000x24', '-nolisten', 'tcp'], pass_fds=(write_fd,),
                    stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
            os.close(write_fd)
            try:
                with selectors.DefaultSelector() as selector:
                    selector.register(read_fd, selectors.EVENT_READ)
                    if not selector.select(10):
                        raise RuntimeError('Private X server did not start within 10 seconds')
                    display = os.read(read_fd, 128).decode().strip()
                if not display.isdigit():
                    raise RuntimeError('Private X server returned an invalid display number')
                env['DISPLAY'] = ':' + display
            finally:
                os.close(read_fd)
        for index, engine in enumerate(args.engines, 1):
            directory = args.output / f'{index:02d}-{engine}'
            directory.mkdir()
            case = copy.deepcopy(scenario)
            case['route']['routingEngine'] = ENGINES[engine]
            input_path = directory / 'scenario.json'
            input_path.write_text(json.dumps(case, indent=2) + '\n')
            result_path = directory / 'result.json'
            run_env = dict(env)
            run_env.update(WR_HEADLESS_ROUTE_TEST='single',
                           WR_HEADLESS_SCENARIO=str(input_path),
                           WR_HEADLESS_OUTPUT=str(result_path),
                           WR_HEADLESS_GRIB_FILE=str(args.grib),
                           WR_HEADLESS_TIMEOUT_MS=str(args.timeout * 1000),
                           WR_HEADLESS_NO_EXIT='0')
            log_path = args.profile / 'opencpn.log'
            old_log = log_path.read_bytes() if log_path.exists() else b''
            start = time.monotonic()
            process = None
            deadline_hit = False
            try:
                with (directory / 'stdout.log').open('wb') as log:
                    process = subprocess.Popen(
                        [str(args.opencpn), '--configdir=' + str(args.profile), '--no_opengl'],
                        env=run_env, stdout=log, stderr=subprocess.STDOUT,
                        start_new_session=os.name == 'posix')
                    try:
                        process.wait(timeout=args.timeout + 60)
                    except subprocess.TimeoutExpired:
                        deadline_hit = True
                        stop(process)
            finally:
                if process is not None:
                    stop(process)
            elapsed = time.monotonic() - start
            new_log = log_path.read_bytes() if log_path.exists() else b''
            delta = new_log[len(old_log):] if new_log.startswith(old_log) else new_log
            text = delta.decode(errors='replace')
            (directory / 'opencpn.log').write_text(text)
            timings = extract(text, single_route=True)
            (directory / 'timings.json').write_text(json.dumps(timings, indent=2) + '\n')
            result = json.loads(result_path.read_text()) if result_path.exists() else None
            record = {'engine_requested': engine, 'engine_id': ENGINES[engine],
                      'process_wall_seconds': round(elapsed, 3),
                      'process_exit': process.returncode,
                      'external_deadline_hit': deadline_hit,
                      'result_status': result.get('status') if result else 'missing_result',
                      'build_records': [line for line in text.splitlines() if 'WR_BUILD ' in line],
                      'main_engine_attempts': [item for item in timings['engine_attempts']
                                               if item['scout'] is not True],
                      'native_summaries': [item for item in timings['native_summaries']
                                           if item['scout'] is not True],
                      'evidence_directory': directory.name}
            records.append(record)
            (args.output / 'summary.json').write_text(json.dumps({
                'grib_file': str(args.grib),
                'grib_sha256': grib_sha256,
                'profile': str(args.profile), 'timeout_seconds': args.timeout,
                'runs': records}, indent=2) + '\n')
            print(f"{engine}: {record['result_status']}; process wall {elapsed:.1f} s; "
                  f"evidence {directory}", flush=True)
    finally:
        if server is not None:
            stop(server)
    return 0 if all(record['result_status'] == 'complete' and
                    record['process_exit'] == 0 and not record['external_deadline_hit']
                    for record in records) else 1


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print('Interrupted; stopped this wrapper\'s application and virtual display.',
              file=sys.stderr)
        raise SystemExit(130)
