#!/usr/bin/env python3
"""Representation experiment, deliberately independent of production engines.

Compare a spatial semi-Lagrangian Bellman graph, visibility roadmap, and
continuous multistart waypoint optimisation on the SAME static analytic model.
This isolates topology/representation, not provider or sailing performance.
Units are arbitrary; speed is one; exact rectangle intersection is used.
"""
import argparse
import heapq
import json
import math
import random
import time
from pathlib import Path

import numpy as np
from scipy.optimize import minimize


def clear(a, b, rectangles):
    for x0, y0, x1, y1 in rectangles:
        lo, hi = 0., 1.
        for p, q, left, right in zip(a, b, (x0, y0), (x1, y1)):
            d = q - p
            if abs(d) < 1e-12:
                if not left <= p <= right:
                    lo, hi = 1., 0.
                    break
            else:
                l, h = sorted(((left - p) / d, (right - p) / d))
                lo, hi = max(lo, l), min(hi, h)
        if lo <= hi:
            return False
    return True


def graph(points, rectangles, local=False):
    n = len(points)
    distance = [math.inf] * n
    distance[0] = 0
    queue = [(0., 0)]
    work = 0
    while queue:
        cost, i = heapq.heappop(queue)
        if cost != distance[i]:
            continue
        if i == 1:
            return cost, work
        choices = range(n)
        if local:
            choices = [j for j in range(n) if
                       math.dist(points[i], points[j]) <= .3 or j == 1 and
                       math.dist(points[i], points[j]) <= .3]
        for j in choices:
            if j == i:
                continue
            d = math.dist(points[i], points[j])
            if cost + d >= distance[j]:
                continue
            work += 1
            if clear(points[i], points[j], rectangles):
                distance[j] = cost + d
                heapq.heappush(queue, (cost + d, j))
    return math.inf, work


def continuous(start, end, rectangles):
    best = math.inf
    work = 0
    def objective(x):
        nonlocal work
        work += 1
        p = [start, *np.asarray(x).reshape(-1, 2), end]
        cost = sum(math.dist(a, b) for a, b in zip(p, p[1:]))
        # Deliberately naive soft obstacle penalty: the experiment tests the
        # failure of a smooth/local objective to discover disconnected basins.
        return cost + sum(30 for a, b in zip(p, p[1:]) if not clear(a, b, rectangles))
    for offset in (-2., 0., 2.):
        seed = np.array([[start[0] + (end[0]-start[0])*f,
                          start[1] + (end[1]-start[1])*f + offset]
                         for f in (.25, .5, .75)])
        result = minimize(objective, seed.ravel(), method='Powell',
                          options={'maxiter': 35, 'maxfev': 2500})
        p = [start, *result.x.reshape(-1, 2), end]
        if all(clear(a, b, rectangles) for a, b in zip(p, p[1:])):
            best = min(best, sum(math.dist(a, b) for a, b in zip(p, p[1:])))
    return best, work


def run(count):
    rng = random.Random(61927)
    rows = []
    for seed in range(count):
        start, end = (0., 0.), (4., 0.)
        if seed % 4 == 0:
            rects = []
        elif seed % 4 == 1:
            rects = [(1.5, -rng.uniform(.4, 1.9), 2.2, rng.uniform(.4, 1.9))]
        elif seed % 4 == 2:
            # Narrow slalom; initially heading toward goal enters a cul-de-sac.
            rects = [(1., -2., 1.2, .8), (2.5, -.8, 2.7, 2.)]
        else:
            rects = [(1.5, -2., 1.7, -.04), (1.5, .04, 1.7, 2.)]
        corners = [start, end]
        for x0, y0, x1, y1 in rects:
            corners += [(x0-.001, y0-.001), (x0-.001, y1+.001),
                        (x1+.001, y0-.001), (x1+.001, y1+.001)]
        # For static isotropic polygonal geometry the corner visibility graph
        # gives the infimum as corner offset -> 0; do not call this a sailing oracle.
        oracle, _ = graph(corners, rects)
        mesh = [start, end] + [(x / 5., y / 5.) for x in range(-5, 26)
                              for y in range(-12, 13)
                              if clear((x/5., y/5.), (x/5., y/5.), rects)]
        for name, solver in [('bellman_mesh', lambda: graph(mesh, rects, True)),
                             ('visibility', lambda: graph(corners, rects)),
                             ('continuous_multistart', lambda: continuous(start, end, rects))]:
            before = time.perf_counter()
            cost, work = solver()
            rows.append(dict(seed=seed, family=seed%4, method=name,
                             cost=cost if math.isfinite(cost) else None,
                             oracle=oracle, ratio=cost/oracle if math.isfinite(cost) else None,
                             work=work, seconds=time.perf_counter()-before))
    return rows


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--count', type=int, default=32)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    rows = run(args.count)
    args.output.write_text(json.dumps(rows, indent=2, allow_nan=False) + '\n')
    for method in sorted({r['method'] for r in rows}):
        subset = [r for r in rows if r['method'] == method]
        valid = [r for r in subset if r['cost'] is not None]
        print(method, 'success', len(valid), '/', len(subset),
              'mean ratio', sum(r['ratio'] for r in valid)/max(1, len(valid)),
              'seconds', sum(r['seconds'] for r in subset))
