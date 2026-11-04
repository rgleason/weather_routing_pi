#!/usr/bin/env python3
"""Broaden ETA references with historical controls revalidated today.

Never use historical timing or an old success flag as current safety proof.
Only identical synthetic physical scenarios and fresh chronological replays
can contribute a faster reference. The measured timing matrix is unchanged.
"""
import argparse
import hashlib
import json
import math
import subprocess
from pathlib import Path
from analyze_benchmark import percentile


def physical(value):
    if isinstance(value,dict):
        return {k:physical(v) for k,v in value.items() if not k.startswith('atlas_')
                and k not in {'name','cache','profile_providers','seed','family','oracle','expected_negative'}}
    if isinstance(value,list):return [physical(v) for v in value]
    if isinstance(value,(int,float)) and not isinstance(value,bool):return float(value)
    return value


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--comparison',type=Path,required=True)
    p.add_argument('--audit',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--pools',type=Path,nargs='+',required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    cases={f.stem:json.loads(f.read_text()) for f in (a.comparison/'scenarios').glob('*.json')}
    measured=[json.loads(s) for s in (a.comparison/'results.jsonl').read_text().splitlines()]
    best={}
    for r in measured:
        if r['valid']:best[r['case']]=min(best.get(r['case'],math.inf),r['arrival_seconds'])
    original=best.copy();seen=set();attempts=[]
    for pool in a.pools:
        records=[]
        if (pool/'results.jsonl').exists():
            records=[json.loads(s) for s in (pool/'results.jsonl').read_text().splitlines()]
        elif (pool/'results.json').exists():
            value=json.loads((pool/'results.json').read_text())
            if isinstance(value,list):records=value
        for r in records:
            c=r.get('case')
            if c not in cases or not r.get('valid') or not r.get('track'):continue
            if r['arrival_seconds']>=best.get(c,math.inf)-1:continue
            current=cases[c]
            if any(k in current for k in ['grib','shoreline','polar']):continue
            paths=[pool/'scenarios'/(c+'.json'),*pool.glob(c+'*.json')]
            if not any(path.exists() and physical(json.loads(path.read_text()))==physical(current) for path in paths):continue
            key=json.dumps([c,r['track']],sort_keys=True)
            if key in seen:continue
            seen.add(key)
            index=len(attempts)
            scenario=(a.output/f'{index:04d}-scenario.json').resolve()
            record=(a.output/f'{index:04d}-record.json').resolve()
            scenario.write_text(json.dumps(current,indent=2)+'\n')
            record.write_text(json.dumps(r,indent=2)+'\n')
            try:
                run=subprocess.run([str(a.audit.resolve()),str(scenario),str(record)],
                    check=True,capture_output=True,text=True,timeout=60)
                audit=json.loads(run.stdout)
            except Exception as error:
                audit=dict(valid=False,reason=str(error))
            accepted=audit['valid'] and audit['arrival_seconds']==r['arrival_seconds']
            attempts.append(dict(case=c,pool=str(pool),engine=r['engine'],
                arrival_seconds=r['arrival_seconds'],accepted=accepted,audit=audit))
            if accepted:best[c]=r['arrival_seconds']
    summary=dict(attempts=len(attempts),accepted=sum(r['accepted'] for r in attempts),
        improved_cases={c:dict(matrix_best_seconds=original.get(c),best_known_seconds=cost)
                        for c,cost in best.items() if cost<original.get(c,math.inf)},
        per_engine={})
    for e in dict.fromkeys(r['engine'] for r in measured):
        subset=[r for r in measured if r['engine']==e and r['repeat']==0 and
                r['valid'] and not r['expected_negative']]
        ratios=[r['arrival_seconds']/best[r['case']] for r in subset]
        summary['per_engine'][e]=dict(p95_ratio=percentile(ratios,.95),
            worst_ratio=max(ratios,default=None),within_one_percent=sum(v<=1.01 for v in ratios))
    (a.output/'attempts.json').write_text(json.dumps(attempts,indent=2)+'\n')
    (a.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    (a.output/'provenance.json').write_text(json.dumps(dict(command=__import__('sys').argv,
        audit_binary_sha256=hashlib.sha256(a.audit.read_bytes()).hexdigest(),
        audit_source_sha256=hashlib.sha256(Path(__file__).with_name('audit_tracks.cpp').read_bytes()).hexdigest(),
        source_comparison=str(a.comparison),note='No historical timing contributes to the timing matrix'),indent=2)+'\n')
    print(json.dumps(summary,indent=2))


if __name__=='__main__':main()
