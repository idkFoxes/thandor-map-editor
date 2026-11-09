#!/usr/bin/env python3
"""Stage29: compare V594 historical snapshots to V595 per-call gate telemetry.

All inputs are SimLab forensic logs, never a retail trace. A historical last
workspace count is NOT treated as a lifetime count or an inequality proof.
"""
from argparse import ArgumentParser
from collections import defaultdict
from pathlib import Path


def parse_kv(line):
    parts = line.split()
    if not parts:
        return '', {}
    return parts[0], dict(s.split('=', 1) for s in parts[1:] if '=' in s)


def load(path):
    runs, units = {}, {}
    for line in Path(path).open(encoding='utf-8', errors='replace'):
        kind, row = parse_kv(line)
        if kind not in ('RUN', 'ENTITY'):
            continue
        if kind == 'ENTITY' and row.get('class') != '18':
            continue
        try:
            run = int(row['index'] if kind == 'RUN' else row['run'])
            tick = int(row['tick'])
            key = run if kind == 'RUN' else (run, int(row['faction']))
        except (KeyError, ValueError):
            continue
        into = runs if kind == 'RUN' else units
        if key not in into or tick >= int(into[key]['tick']):
            into[key] = row
    return runs, units


def classify(row):
    if row is None:
        return 'NO_LIVE_PIONEER_AT_FINAL_SNAPSHOT'
    if 'pioneer_gate_calls' not in row:
        return 'GATE_TELEMETRY_ABSENT'
    if 'pioneer_gate_ws_equal' not in row:
        return 'WORKSPACE_HISTORY_UNKNOWN_PRE_V595'
    calls = int(row['pioneer_gate_calls'])
    eq = int(row['pioneer_gate_ws_equal'])
    neq = int(row['pioneer_gate_ws_unequal'])
    if eq + neq != calls:
        return 'INTEGRITY_FAILURE_WORKSPACE_ACCOUNTING'
    if neq == 0:
        return 'ALL_OBSERVED_GATES_EQUAL_WORKSPACES'
    special = int(row['pioneer_gate_special_unequal_sites'])
    carry = int(row['pioneer_gate_ws_unequal_carry'])
    if special > 0 and carry == 0:
        return 'UNEQUAL_WORKSPACES_SPECIAL_BLOCKS_UPDATE_NO_CARRY_OBSERVED'
    if carry > 0 and int(row.get('pioneer_gate_score_sites', 0)) == 0:
        return 'UNEQUAL_WITH_CARRY_NO_SCORE_REQUIRES_FLAG_HISTORY'
    return 'MIXED_WORKSPACE_HISTORY'


def report(path):
    runs, units = load(path)
    print('SOURCE',path, 'RUNS',len(runs), 'SIMLAB_NOT_RETAIL')
    summary = defaultdict(int)
    for run in sorted(runs):
        print('RUN',run, 'TICK',runs[run]['tick'],
              'CONVERSIONS',runs[run].get('conversions_completed', '?'),
              'BUILT',runs[run].get('built', '?'))
        for faction in range(1, 5):
            row = units.get((run,faction))
            state = classify(row)
            summary[(faction,state)] += 1
            row = row or {}
            print(f' P{faction} {state}',
                  'gate',row.get('pioneer_gate_calls','?'),
                  'eq',row.get('pioneer_gate_ws_equal','UNKNOWN'),
                  'neq',row.get('pioneer_gate_ws_unequal','UNKNOWN'),
                  'special_neq_sites',row.get('pioneer_gate_special_unequal_sites','UNKNOWN'),
                  'unequal_carry',row.get('pioneer_gate_ws_unequal_carry','UNKNOWN'),
                  'selector',row.get('pioneer_selector_calls','?'),
                  'commands',row.get('pioneer_ai_commands','?'))
    print('SUMMARY',dict(sorted(summary.items())))
    print('RESET_CONTRACT: V537 binary-export C flags &= 0xFFFFFFC6, retains 0x100. '
          'Command-mode conditional also needs independent runtime +0x100 state field.')
    print('LIMIT: V594 last workspace counts do not establish per-call equality. '
          'No retail movement/placement trace available.')

if __name__=='__main__':
    parser=ArgumentParser(description=__doc__)
    parser.add_argument('logs',nargs='+',type=Path)
    args=parser.parse_args()
    for path in args.logs:
        report(path)
