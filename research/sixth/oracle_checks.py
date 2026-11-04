#!/usr/bin/env python3
"""Independent closed-form/visibility oracles, with explicit numerical tolerance.

Isotropic six-knot travel: geodesic distance / speed.
Constant upwind: independently maximise the known analytic polar VMG, + one tack.
Static rectangles/isotropic: polygon corner visibility graph, offset -> 0.
The latter two are planar approximations; fixtures are short and near equator.
"""
import argparse
import hashlib
import json
import math
import random
import shutil
import subprocess
from pathlib import Path
from prototypes import graph


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--binary',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--count',type=int,default=1000)
    args=p.parse_args();args.output.mkdir(parents=True,exist_ok=False)
    binary=(args.output/'atlas-benchmark').resolve();shutil.copy2(args.binary,binary)
    rng=random.Random(620261016)
    vmg=max((4+(a-40)/10 if a<=50 else 5+(a-50)/40)*math.cos(math.radians(a))
            for a in (40+i/10000 for i in range(500001)))
    results=[]
    for i in range(args.count):
        distance=rng.uniform(5,50);family=i%4
        j=dict(name=f'oracle_{i:04d}',start=[0,rng.uniform(-170,170)],
               east_nm=distance,wind_from=0,max_hours=96,
               atlas_improvement=0,cache=False)
        if family==0:
            j['isotropic']=True;reference=distance*600;kind='geodesic';tol=.002
        elif family==1:
            j.update(east_nm=0,north_nm=distance)
            reference=distance/vmg*3600+120;kind='upwind_envelope';tol=.007
        elif family==2:
            j.update(gate=True,gate_east_nm=distance/2,gate_opens_hours=distance/6*.9,
                     atlas_improvement=200)
            reference=(distance/6*.9+distance/2/6)*3600;kind='timed_gate';tol=.06
        else:
            left=distance*.4;right=distance*.6;height=rng.uniform(.5,3)
            rects=[(left,-height,right,height)]
            points=[(0.,0.),(distance,0.)]
            epsilon=1e-6
            for a,b,c,d in rects:
                points.extend([(a-epsilon,b-epsilon),(a-epsilon,d+epsilon),
                               (c+epsilon,b-epsilon),(c+epsilon,d+epsilon)])
            length,_=graph(points,rects);reference=length*600
            j.update(rectangles_nm=rects,isotropic=True,atlas_improvement=200)
            kind='visibility';tol=.035
        scenario=args.output/'scenario.json';scenario.write_text(json.dumps(j))
        run=subprocess.run([str(binary),'atlas',str(scenario.resolve()),'8'],capture_output=True,text=True,timeout=20)
        if run.returncode:raise RuntimeError(run.stderr)
        result=json.loads(run.stdout);ratio=result['arrival_seconds']/reference if result['valid'] else None
        # A 0.008 NM endpoint residual and integer-second integration are
        # allowed by the common model. These are explicit oracle tolerances.
        passed=result['valid'] and ratio<=1+tol and ratio>=1-.003
        results.append(dict(index=i,kind=kind,scenario=j,reference_seconds=reference,
                            ratio=ratio,passed=passed,result=result))
        if (i+1)%50==0:print(i+1,'/',args.count,'passed',sum(r['passed'] for r in results),flush=True)
    (args.output/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    summary=dict(count=len(results),passed=sum(r['passed'] for r in results),
                 binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest())
    for kind in sorted({r['kind'] for r in results}):
        subset=[r for r in results if r['kind']==kind]
        summary[kind]=dict(count=len(subset),passed=sum(r['passed'] for r in subset),
                           max_ratio=max((r['ratio'] for r in subset if r['ratio'] is not None),default=None))
    (args.output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary,indent=2))
    return 0 if summary['passed']==summary['count'] else 1


if __name__=='__main__':raise SystemExit(main())
