#!/usr/bin/env python3
"""Plot measured coverage/time and quality; no continuous-optimum claim."""
import argparse
import json
from pathlib import Path
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from analyze_benchmark import TITLES


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('folder',type=Path)
    p.add_argument('--best-known',type=Path,help='Freshly revalidated historical reference summary')
    a=p.parse_args()
    analysis=json.loads((a.folder/'analysis.json').read_text())
    rows=[json.loads(s) for s in (a.folder/'results.jsonl').read_text().splitlines()]
    best={}
    for r in rows:
        if r['valid']:best[r['case']]=min(best.get(r['case'],float('inf')),r['arrival_seconds'])
    if a.best_known:
        for c,v in json.loads(a.best_known.read_text())['improved_cases'].items():
            best[c]=min(best[c],v['best_known_seconds'])
    n=analysis['intended_positive']
    fig,axes=plt.subplots(1,2,figsize=(11.6,4.2),layout='constrained')
    palette=plt.get_cmap('tab10')
    for i,e in enumerate(analysis['engines']):
        s=e['runs']['0'];color=palette(i)
        axes[0].scatter(s['median_seconds'],s['success_percent'],s=80,color=color)
        offset={'atlas':(0,10),'main':(-8,7),'quick':(-5,7)}.get(e['engine'],(5,7))
        align={'atlas':'center','main':'right','quick':'right'}.get(e['engine'],'left')
        axes[0].annotate(e['title'],(s['median_seconds'],s['success_percent']),
                         xytext=offset,textcoords='offset points',fontsize=9,color=color,ha=align)
        ratios=sorted(100*(r['arrival_seconds']/best[r['case']]-1) for r in rows
                      if r['engine']==e['engine'] and r['repeat']==0 and
                      r['valid'] and not r['expected_negative'])
        x=[0.]+ratios+[max(25.,max(ratios,default=0))]
        y=[0.]+[100*(k+1)/n for k in range(len(ratios))]+[100*len(ratios)/n]
        axes[1].step(x,y,where='post',label=TITLES[e['engine']],color=color)
    axes[0].set_xscale('log')
    axes[0].set_xlabel('Median cold solver time (s; log scale)')
    axes[0].set_ylabel('Positive cases with a validated route (%)')
    axes[0].set_ylim(65,104)
    axes[1].set_xlabel('Arrival-cost excess over best validated reference (%)'
                       if a.best_known else 'Arrival-cost excess over matrix best (%)')
    axes[1].set_ylabel('All positive cases attaining this cost (%)')
    axes[1].set_xlim(0,25);axes[1].set_ylim(0,103)
    axes[1].legend(fontsize=8,loc='lower right')
    for ax in axes:ax.grid(alpha=.22)
    fig.suptitle(f'Measured six-engine comparison: {n} positive scenarios, one pinned CPU')
    fig.savefig(a.folder/'comparison.svg')
    fig.savefig(a.folder/'comparison.png',dpi=170)


if __name__=='__main__':main()
