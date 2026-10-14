#!/usr/bin/env python3
"""Extract xWeatherRouting timing records; no OpenCPN or third-party imports."""
# SPDX-License-Identifier: GPL-3.0-or-later
import argparse
import json
import re
from pathlib import Path

FIELDS = re.compile(r'(\w+)=("[^"]*"|\S+)')


def fields(text):
    return {key: value.strip('"') for key, value in FIELDS.findall(text)}


def extract(text, single_route=False):
    attempts, summaries, pending, budget = [], [], [], []
    for number, line in enumerate(text.splitlines(), 1):
        if 'WR_ENGINE_ATTEMPT ' in line:
            value = fields(line.split('WR_ENGINE_ATTEMPT ', 1)[1])
            value.update(line=number, computation_seconds=int(value['elapsed_ms']) / 1000,
                         scout=None, route=None)
            attempts.append(value)
            pending.append(value)
        elif 'WR_MODERN_NATIVE_SUMMARY ' in line:
            value = fields(line.split('WR_MODERN_NATIVE_SUMMARY ', 1)[1])
            value.update(line=number, computation_seconds=int(value['elapsed_ms']) / 1000)
            value['scout'] = value.get('scout') == '1'
            summaries.append(value)
            if single_route:
                for attempt in pending:
                    attempt.update(scout=value['scout'], route=value.get('route'))
                pending.clear()
        elif 'WR_PROFESSIONAL_QUICK_BUDGET ' in line:
            value = fields(line.split('WR_PROFESSIONAL_QUICK_BUDGET ', 1)[1])
            value['line'] = number
            budget.append(value)
    return {'engine_attempts': attempts, 'native_summaries': summaries,
            'professional_quick_budgets': budget,
            'single_route_association': single_route,
            'note': 'elapsed_ms is computation time; passage_s is voyage duration. '
                    'Engine records include waits inside an invocation, but not '
                    'earlier preparation. Unknown scout/route associations are '
                    'left unset. --single-route is valid only for serial, '
                    'single-route runs, not concurrent GUI/departure tests.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('--json', type=Path, help='Write the full extracted timing records')
    parser.add_argument('--single-route', action='store_true',
                        help='Associate attempts with following summaries in serial single-route logs')
    args = parser.parse_args()
    result = extract(args.log.read_text(errors='replace'), args.single_route)
    if args.json:
        args.json.write_text(json.dumps(result, indent=2) + '\n')
    print('Record\tEngine / solver\tSeconds\tStatus\tValidated\tScout\tLog line')
    records = [('attempt', item) for item in result['engine_attempts']]
    records += [('native_summary', item) for item in result['native_summaries']]
    for kind, item in sorted(records, key=lambda pair: pair[1]['line']):
        label = item.get('engine', item.get('solver', 'unknown'))
        print(f"{kind}\t{label}\t{item['computation_seconds']:.3f}\t"
              f"{item['status']}\t{item.get('validated', 'see result JSON')}\t"
              f"{item['scout']}\t{item['line']}")


if __name__ == '__main__':
    main()
