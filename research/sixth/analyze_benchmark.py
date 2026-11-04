#!/usr/bin/env python3
"""Analyse every record, including process errors and intended negative cases."""
import argparse
import collections
import json
import math
import statistics
from pathlib import Path

TITLES=dict(original='Quick',quick2='Quick2',quick='Standard',alternative='Alternative',main='Professional',atlas='Atlas')


def percentile(values,p):
    values=sorted(values)
    if not values:return None
    x=(len(values)-1)*p;i=int(x);a=x-i
    return values[i]*(1-a)+values[min(i+1,len(values)-1)]*a


def median(rows,key):return statistics.median(r[key] for r in rows) if rows else None


def analyse(folder):
    cases={p.stem:json.loads(p.read_text()) for p in (folder/'scenarios').glob('*.json')}
    rows=[json.loads(s) for s in (folder/'results.jsonl').read_text().splitlines()]
    engines=list(dict.fromkeys(r['engine'] for r in rows));repeats=sorted({r['repeat'] for r in rows})
    positive={k for k,c in cases.items() if not c.get('expected_negative')}
    lookup={(r['case'],r['engine'],r['repeat']):r for r in rows}
    missing=[(c,e,t) for c in cases for e in engines for t in repeats if (c,e,t) not in lookup]
    best={}
    for r in rows:
        if r['valid']:best[r['case']]=min(best.get(r['case'],math.inf),r['arrival_seconds'])
    summary=[]
    for e in engines:
        result=dict(engine=e,title=TITLES[e],runs={})
        for t in repeats:
            subset=[r for r in rows if r['engine']==e and r['repeat']==t and r['case'] in positive]
            valid=[r for r in subset if r['valid']]
            ratios=[r['arrival_seconds']/best[r['case']] for r in valid]
            result['runs'][str(t)]=dict(cases=len(subset),valid=len(valid),
                success_percent=100*len(valid)/len(subset),
                median_seconds=median(subset,'seconds'),p95_seconds=percentile([r['seconds'] for r in subset],.95),
                total_seconds=sum(r['seconds'] for r in subset),median_cpu_seconds=median(subset,'cpu_seconds') if not any('process_error' in r for r in subset) else None,
                median_quality_ratio=percentile(ratios,.5),p95_quality_ratio=percentile(ratios,.95),worst_quality_ratio=max(ratios,default=None),
                within_one_percent=sum(x<=1.01 for x in ratios),within_five_percent=sum(x<=1.05 for x in ratios),
                median_weather_queries=median(subset,'weather_queries') if not any('process_error' in r for r in subset) else None,
                median_land_queries=median(subset,'land_queries') if not any('process_error' in r for r in subset) else None,
                median_polar_queries=median(subset,'polar_queries') if not any('process_error' in r for r in subset) else None,
                median_peak_rss_kib=median(subset,'peak_rss_kib'),maximum_peak_rss_kib=max(r['peak_rss_kib'] for r in subset),
                median_generated=median(subset,'generated') if not any('process_error' in r for r in subset) else None,
                process_errors=sum('process_error' in r for r in subset),
                negative_acceptances=[r['case'] for r in rows if r['engine']==e and r['repeat']==t and r['case'] not in positive and r['valid']],
                failures=[r['case'] for r in subset if not r['valid']])
        summary.append(result)
    pairs=[]
    for e in engines:
        if e=='atlas':continue
        common=[(lookup[(c,'atlas',0)],lookup[(c,e,0)]) for c in positive
                if (c,'atlas',0) in lookup and (c,e,0) in lookup and lookup[(c,'atlas',0)]['valid'] and lookup[(c,e,0)]['valid']]
        ratios=[a['arrival_seconds']/b['arrival_seconds'] for a,b in common]
        pairs.append(dict(engine=e,common=len(common),median_atlas_eta_ratio=percentile(ratios,.5),
            worst_atlas_eta_ratio=max(ratios,default=None),median_baseline_wall_over_atlas=percentile([b['seconds']/a['seconds'] for a,b in common],.5),
            atlas_faster_arrival=sum(a['arrival_seconds']<b['arrival_seconds']-1 for a,b in common),
            atlas_slower_by_over_one_percent=sum(a['arrival_seconds']>b['arrival_seconds']*1.01 for a,b in common),
            baseline_only=[c for c in positive if (c,e,0) in lookup and lookup[(c,e,0)]['valid'] and not lookup.get((c,'atlas',0),{}).get('valid')],
            atlas_only=[c for c in positive if lookup.get((c,'atlas',0),{}).get('valid') and not lookup.get((c,e,0),{}).get('valid')]))
    determinism={}
    if len(repeats)>1:
        for e in engines:
            pairs2=[(lookup[(c,e,0)],lookup[(c,e,1)]) for c in cases if (c,e,0) in lookup and (c,e,1) in lookup]
            determinism[e]=dict(pairs=len(pairs2),validity_differences=[a['case'] for a,b in pairs2 if a['valid']!=b['valid']],
                eta_differences=[a['case'] for a,b in pairs2 if a['valid'] and b['valid'] and a['arrival_seconds']!=b['arrival_seconds']],
                track_differences=[a['case'] for a,b in pairs2 if a['valid'] and b['valid'] and a['track']!=b['track']],
                work_differences=[a['case'] for a,b in pairs2 if 'generated' in a and 'generated' in b and a['generated']!=b['generated']])
    # Offline portfolios compare independently validated component results.
    # They do not model the host Auto scheduler's different recovery budgets.
    portfolios={}
    for t in repeats:
        baseline=[e for e in engines if e!='atlas']
        def portfolio(members):
            return {c:min((lookup[(c,e,t)]['arrival_seconds'] for e in members
                           if lookup.get((c,e,t),{}).get('valid')),default=math.inf)
                    for c in positive}
        five=portfolio(baseline);six=portfolio(engines)
        added=sorted(c for c in positive if math.isinf(five[c]) and math.isfinite(six[c]))
        improved=sorted((dict(case=c,before_seconds=five[c],after_seconds=six[c],
                              saving_percent=100*(1-six[c]/five[c])) for c in positive
                         if math.isfinite(five[c]) and six[c]<five[c]-1),
                        key=lambda r:r['saving_percent'],reverse=True)
        quick_atlas=portfolio([e for e in ['original','atlas'] if e in engines])
        portfolios[str(t)]=dict(
            all_five_valid=sum(math.isfinite(v) for v in five.values()),
            all_six_valid=sum(math.isfinite(v) for v in six.values()),
            quick_and_atlas_valid=sum(math.isfinite(v) for v in quick_atlas.values()),
            atlas_additional_solutions=added,atlas_improves_best_five=improved,
            interpretation='Offline component portfolio; not a measured Auto timing or recovery-budget simulation')
    output=dict(cases=len(cases),intended_positive=len(positive),expected_negative=len(cases)-len(positive),
                records=len(rows),missing_records=missing,engines=summary,pairwise=pairs,determinism=determinism,portfolios=portfolios,
                unsolved_by_any=[c for c in positive if c not in best],
                atlas_quality_outliers=sorted([dict(case=c,ratio=lookup[(c,'atlas',0)]['arrival_seconds']/best[c]) for c in positive if lookup.get((c,'atlas',0),{}).get('valid')],key=lambda r:r['ratio'],reverse=True))
    (folder/'analysis.json').write_text(json.dumps(output,indent=2)+'\n')
    table=['| Engine | Valid cold / cases | Cold median s | Warm median s | ETA p95 / best observed | Peak RSS median MiB |',
           '|---|---:|---:|---:|---:|---:|']
    for engine in summary:
        c=engine['runs']['0'];w=engine['runs'].get('1',c)
        table.append(f"| {engine['title']} | {c['valid']}/{c['cases']} | {c['median_seconds']:.4f} | {w['median_seconds']:.4f} | {c['p95_quality_ratio']:.4f} | {c['median_peak_rss_kib']/1024:.1f} |")
    (folder/'table.md').write_text('\n'.join(table)+'\n')
    print('\n'.join(table));print('Missing',len(missing),'Unsolved',output['unsolved_by_any'])
    return output


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('folder',type=Path);args=p.parse_args();analyse(args.folder)
