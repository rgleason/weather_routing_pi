#!/usr/bin/env python3
"""Generate reproducible adversarial problems and compare unchanged engines.

Runs serially. Each engine/case uses a fresh process; repeats inside that
process distinguish cold providers from retained caches. No GUI/profile edits.
"""
import argparse
import hashlib
import gzip
import json
import math
import os
import platform
import random
import shutil
import statistics
import subprocess
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ENGINES = ['original', 'quick2', 'quick', 'alternative', 'main', 'atlas']


def generate(count=128, shore=None):
    cases = []
    def case(name, **values):
        c = dict(name=name, start=[30, -140], east_nm=20, north_nm=0,
                 wind_from=0, max_hours=48, cache=True)
        c.update(values); cases.append(c)
    case('trivial', east_nm=.2)
    case('reach')
    case('upwind', east_nm=0, north_nm=20)
    case('dead_downwind', east_nm=0, north_nm=-20)
    case('island_north_south', rectangles_nm=[[8,-4,12,4]])
    case('island_chain', east_nm=45, rectangles_nm=[[7,-8,10,3],[18,-3,21,8],[31,-7,33,2]])
    case('slalom', rectangles_nm=[[6,-12,7,4],[13,-4,14,12]])
    case('u_trap', rectangles_nm=[[1,4,11,5],[10,-5,11,5],[1,-5,11,-4]])
    case('narrow_400m', rectangles_nm=[[8,-20,12,-.108],[8,.108,12,20]])
    case('narrow_40m', rectangles_nm=[[8,-20,12,-.0108],[8,.0108,12,20]])
    case('offset_narrow', rectangles_nm=[[8,-20,12,1.9],[8,2.1,12,20]])
    case('wait_gate', gate=True, gate_opens_hours=4)
    case('wait_gate_no_wait', gate=True, gate_opens_hours=4, waiting=False)
    case('tidal_east', currents=True, current_speed=6, current_to=90, tidal=True)
    case('tidal_adverse', currents=True, current_speed=7, current_to=90, tidal=True, tidal_phase=-1.5)
    # With this polar every allowed sailing velocity has south component
    # less than 5 kn. Latitude therefore strictly increases; the due-east
    # great-circle endpoint has slightly lower latitude. Proven infeasible.
    case('strong_cross_current', currents=True, current_speed=5, current_to=0, expected_negative=True)
    case('favourable_current_jet', east_nm=40, currents=True, jet=True, current_to=90)
    case('changing_weather', east_nm=60, variable=True)
    case('wind_reversal', east_nm=60, wind_shift=True)
    case('moving_storm', east_nm=40, storm=True, max_wind=30)
    case('motor_calm', east_nm=10, wind_speed=0, motor=True)
    case('shallow_rejected', depth=2, min_depth=5, expected_negative=True)
    case('missing_depth', missing_depth=True, min_depth=5, expected_negative=True)
    case('missing_wind', missing_wind=True, expected_negative=True)
    case('missing_current', currents=True, missing_current=True, expected_negative=True)
    case('short_forecast', coverage_hours=.5, expected_negative=True)
    case('wave_limit', wave_height=5, max_wave=2, expected_negative=True)
    case('strict_validator', validation_block=True, expected_negative=True, atlas_edges=1000)
    case('dateline_east', start=[-20,179.9], east_nm=30)
    case('dateline_west', start=[-20,-179.9], east_nm=-30)
    case('high_latitude', start=[72,10], east_nm=60)
    case('long_ocean', start=[25,-140], east_nm=1800, max_hours=500, step_seconds=21600)
    case('long_ocean_variable', start=[25,-140], east_nm=1200, variable=True, max_hours=400, step_seconds=21600)
    if shore:
        case('provincetown_departure', start=[42.08368,-70.16199], end=[42.1,-70.5], shoreline=str(shore), margin_nm=.2)
        case('provincetown_arrival', start=[42.1,-70.5], end=[42.08368,-70.16199], shoreline=str(shore), margin_nm=.2)
        # Hadley Harbor buoy 3 position from USCG Light List Vol. 1 (2026).
        case('hadley_departure', start=[41.5148336,-70.6979206], end=[41.54,-70.71], shoreline=str(shore), margin_nm=.05)
        case('hadley_arrival', start=[41.54,-70.71], end=[41.5148336,-70.6979206], shoreline=str(shore), margin_nm=.05)
        case('holyhead_dublin', start=[53.334,-4.618], end=[53.338,-6.065], shoreline=str(shore), currents=True, current_speed=3, current_to=90, tidal=True, max_hours=72)
        case('holyhead_conwy', start=[53.334,-4.618], end=[53.29,-3.84], shoreline=str(shore), wind_from=200, max_hours=72)
    rng = random.Random(20261016)
    for seed in range(count):
        family = seed % 8
        data = dict(east_nm=rng.uniform(6,55), wind_from=rng.uniform(0,360),
                    max_hours=72, seed=seed, family=family)
        if family == 0:
            data.update(isotropic=True, oracle='geodesic_6kn', cache=False)
        elif family == 1:
            data.update(east_nm=0, north_nm=rng.uniform(5,40), wind_from=0, oracle='upwind_envelope')
        elif family == 2:
            x=data['east_nm']*.45
            data.update(rectangles_nm=[[x,-rng.uniform(1,5),x+1,rng.uniform(1,5)]])
        elif family == 3:
            data.update(currents=True, current_speed=rng.uniform(2,8), current_to=rng.uniform(0,360), tidal=True, tidal_phase=rng.uniform(-3,3))
        elif family == 4:
            data.update(variable=True)
        elif family == 5:
            data.update(rectangles_nm=[[2,-20,3,-.15],[2,.15,3,20]], wind_from=0)
        elif family == 6:
            data.update(gate=True, gate_east_nm=3, gate_opens_hours=rng.uniform(1,5), wind_from=0)
        else:
            data.update(currents=True, jet=True, current_to=90, wind_from=0)
        case(f'generated_{seed:04d}', **data)
    return cases


def sha(path):
    digest=hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda:source.read(1024*1024),b''):digest.update(chunk)
    return digest.hexdigest()


def summary(rows, cases):
    positive={c['name'] for c in cases if not c.get('expected_negative')}
    best={}
    for r in rows:
        if r['valid']:
            best[r['case']]=min(best.get(r['case'],math.inf),r['arrival_seconds'])
    output=[]
    for e in dict.fromkeys(r['engine'] for r in rows):
        subset=[r for r in rows if r['engine']==e and r['repeat']==0 and r['case'] in positive]
        valid=[r for r in subset if r['valid']]
        ratios=[r['arrival_seconds']/best[r['case']] for r in valid]
        output.append(dict(engine=e,cases=len(subset),valid=len(valid),
                           process_errors=sum('process_error' in r for r in subset),
                           negative_cases=sum(r['case'] not in positive for r in rows if r['engine']==e and r['repeat']==0),
                           unsafe_negative_acceptances=sum(r['valid'] and r['case'] not in positive for r in rows if r['engine']==e and r['repeat']==0),
                           median_seconds=statistics.median(r['seconds'] for r in subset),
                           total_seconds=sum(r['seconds'] for r in subset),
                           median_ratio_to_best=statistics.median(ratios) if ratios else None,
                           worst_ratio_to_best=max(ratios,default=None),
                           median_rss_kib=statistics.median(r['peak_rss_kib'] for r in subset)))
    return output


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,default=ROOT/'build-atlas/tests/atlas_benchmark')
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--generated',type=int,default=128)
    p.add_argument('--engines',nargs='+',default=ENGINES,choices=ENGINES)
    p.add_argument('--repeats',type=int,default=2)
    p.add_argument('--timeout',type=float,default=10)
    p.add_argument('--cpu',type=int,help='Pin solver processes to an allowed logical CPU using Linux taskset')
    p.add_argument('--shoreline',type=Path)
    p.add_argument('--case-filter',default='')
    p.add_argument('--extra-cases',type=Path,help='JSON array of additional real-data scenarios')
    p.add_argument('--atlas-waypoints',type=int)
    p.add_argument('--atlas-edges',type=int)
    p.add_argument('--atlas-improvement',type=int)
    args=p.parse_args()
    if args.cpu is not None and (not shutil.which('taskset') or args.cpu not in os.sched_getaffinity(0)):
        p.error('--cpu requires taskset and an allowed logical CPU')
    source_binary=args.binary.resolve()
    args.output.mkdir(parents=True,exist_ok=False)
    # A running research tree may be rebuilt while this suite runs. Execute
    # an immutable binary snapshot, not whatever a later build puts at argv[0].
    snapshot=(args.output/'atlas-benchmark').resolve()
    shutil.copy2(args.binary,snapshot)
    args.binary=snapshot
    if args.shoreline:
        args.shoreline=args.shoreline.resolve()
        if args.shoreline.suffix=='.gz':
            unpacked=(args.output/'shoreline.dat').resolve()
            with gzip.open(args.shoreline,'rb') as src,unpacked.open('wb') as dst:
                shutil.copyfileobj(src,dst)
            args.shoreline=unpacked
    (args.output/'scenarios').mkdir()
    cases=generate(args.generated,args.shoreline)
    if args.extra_cases:
        cases.extend(json.loads(args.extra_cases.read_text()))
    cases=[c for c in cases if args.case_filter in c['name']]
    for c in cases:
        for key in ['atlas_waypoints','atlas_edges','atlas_improvement']:
            if getattr(args,key) is not None:c[key]=getattr(args,key)
        (args.output/'scenarios'/(c['name']+'.json')).write_text(json.dumps(c,indent=2)+'\n')
    sources={str(path.relative_to(ROOT)):sha(path) for folder in ['vendor/weather-routing-engine','vendor/original-routing-engine','vendor/quick2-routing-engine','vendor/atlas-routing-engine'] for path in (ROOT/folder).rglob('*') if path.is_file() and path.suffix in {'.cpp','.h','.txt'}}
    provenance=dict(machine=platform.platform(),cpu=platform.processor(),python=platform.python_version(),
                    binary=str(args.binary.resolve()),binary_sha256=sha(args.binary),sources=sources,
                    command=os.sys.argv,started=time.time(),serial=True,
                    cpu_info=Path('/proc/cpuinfo').read_text().split('\n')[:30])
    if args.shoreline:provenance['shoreline_sha256']=sha(args.shoreline)
    provenance['runner_sha256']=sha(Path(__file__))
    provenance['selected_cpu']=args.cpu
    provenance['allowed_cpus']=sorted(os.sched_getaffinity(0))
    cache=source_binary.parent.parent/'CMakeCache.txt'
    if cache.exists():
        provenance['build_settings']=[s for s in cache.read_text().splitlines() if s.startswith(('CMAKE_BUILD_TYPE:','CMAKE_CXX_COMPILER:','CMAKE_CXX_FLAGS_RELEASE:'))]
    provenance['input_sha256']={str(path):sha(path) for path in
        {Path(c[key]) for c in cases for key in ['grib','polar','shoreline'] if key in c}}
    (args.output/'provenance.json').write_text(json.dumps(provenance,indent=2)+'\n')
    rows=[]
    with (args.output/'results.jsonl').open('w') as output:
        for i,c in enumerate(cases):
            for engine in args.engines:
                cmd=[str(args.binary.resolve()),engine,str((args.output/'scenarios'/(c['name']+'.json')).resolve()),str(args.timeout),str(args.repeats)]
                if args.cpu is not None:cmd=['taskset','-c',str(args.cpu),*cmd]
                started=time.perf_counter()
                try:
                    run=subprocess.run(cmd,text=True,capture_output=True,timeout=args.timeout*args.repeats+90)
                    if run.returncode:raise RuntimeError(f'{run.returncode}: {run.stderr}')
                    batch=[json.loads(line) for line in run.stdout.splitlines()]
                    if len(batch)!=args.repeats:raise RuntimeError('Missing benchmark records')
                    for row in batch:
                        row['process_wall_seconds']=time.perf_counter()-started
                        row['expected_negative']=c.get('expected_negative',False)
                        rows.append(row);output.write(json.dumps(row,separators=(',',':'))+'\n')
                    output.flush()
                    first=batch[0]
                    print(f"{i+1}/{len(cases)} {c['name']} {engine} valid={first['valid']} {first['seconds']:.3f}s eta={first['arrival_seconds']}",flush=True)
                except Exception as e:
                    (args.output/f"{c['name']}-{engine}.error").write_text(str(e))
                    # Infrastructure failures count in the denominator. Never
                    # silently improve a success rate by dropping failed runs.
                    for repeat in range(args.repeats):
                        row=dict(case=c['name'],engine=engine,repeat=repeat,
                                 valid=False,arrival_seconds=0,
                                 seconds=time.perf_counter()-started,
                                 peak_rss_kib=0,process_error=str(e),
                                 expected_negative=c.get('expected_negative',False))
                        rows.append(row);output.write(json.dumps(row)+'\n')
                    output.flush()
                    print('ERROR',c['name'],engine,e,flush=True)
    (args.output/'summary.json').write_text(json.dumps(summary(rows,cases),indent=2)+'\n')
    print(json.dumps(summary(rows,cases),indent=2))


if __name__=='__main__':main()
