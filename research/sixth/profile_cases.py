#!/usr/bin/env python3
"""Separate instrumented profiling run; never mix with qualification timings."""
import argparse
import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--cases',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--cpu',type=int,default=4)
    p.add_argument('--names',nargs='+',default=['reach','hadley_departure',
        'provincetown_departure','wind_reversal','generated_0071',
        'moving_storm','long_ocean_variable','real_tpxo_offshore_00'])
    a=p.parse_args()
    if a.cpu not in os.sched_getaffinity(0):p.error('CPU not allowed')
    a.output.mkdir(parents=True,exist_ok=False)
    binary=(a.output/'atlas-benchmark').resolve();shutil.copy2(a.binary,binary)
    rows=[]
    for name in a.names:
        scenario=json.loads((a.cases/(name+'.json')).read_text())
        scenario['profile_providers']=True
        path=(a.output/(name+'.json')).resolve()
        path.write_text(json.dumps(scenario,indent=2)+'\n')
        run=subprocess.run(['taskset','-c',str(a.cpu),str(binary),'atlas',str(path),'10'],
                           check=True,capture_output=True,text=True,timeout=100)
        rows.append(json.loads(run.stdout))
    (a.output/'results.json').write_text(json.dumps(rows,indent=2)+'\n')
    (a.output/'provenance.json').write_text(json.dumps(dict(
        binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
        command=os.sys.argv,cpu=a.cpu,provider_timers_add_overhead=True,
        note='Field time is included in preprocessing time; provider timers overlap phases'),indent=2)+'\n')
    table=['| Case | Total ms | First valid ms | Preprocess ms | Field ms | Land provider ms | Weather provider ms | Polar provider ms |',
           '|---|---:|---:|---:|---:|---:|---:|---:|']
    for r in rows:
        d=r['profile']
        values=[r['seconds']*1000]+[d[k] for k in ['first_valid_ms','preprocessing_ms',
            'value_field_ms','land_provider_ms','weather_provider_ms','polar_provider_ms']]
        table.append('| '+r['case']+' | '+' | '.join(f'{v:.2f}' for v in values)+' |')
    (a.output/'table.md').write_text('\n'.join(table)+'\n')
    print('\n'.join(table))


if __name__=='__main__':main()
