#!/usr/bin/env python3
"""Summarize V585/V586/V587 Simulation Lab forensic logs without retail-parity claims."""
from __future__ import annotations
import argparse
import re
from collections import Counter, defaultdict
from pathlib import Path

PAIR = re.compile(r'(\w+)=([^\s]+)')

def parse(path: Path):
    latest_runs = {}
    latest_factions = defaultdict(dict)
    latest_pending = defaultdict(dict)
    latest_sites = defaultdict(dict)
    latest_pioneer = defaultdict(dict)
    triggers = Counter()
    seeds = set()
    lines = 0
    pending_reset_at = {}
    for raw in path.open(errors='replace'):
        lines += 1
        kind = raw.split(' ', 1)[0]
        if kind not in {'RUN','FACTION','TRIGGER','PENDING','RESOURCE_SITE','ENTITY'}:
            continue
        row = dict(PAIR.findall(raw))
        try:
            index = int(row.get('run', row.get('index', -1)))
        except ValueError:
            continue
        if kind == 'RUN':
            latest_runs[index] = row
            seeds.add(row.get('seed'))
        elif kind == 'FACTION':
            f = int(row.get('f','0'))
            latest_factions[index][f] = row
        elif kind == 'PENDING':
            latest_pending[index][(row.get('faction'), row.get('arm'))] = row
        elif kind == 'RESOURCE_SITE':
            f = int(row.get('faction','0'))
            latest_sites[index][(f, row.get('index'))] = row
        elif kind == 'ENTITY' and row.get('arm') == '50':
            latest_pioneer[index][int(row.get('faction','0'))] = row
        elif kind == 'TRIGGER':
            # TRIGGER entries are written only for force/changed snapshots, i.e.
            # together with a COMPLETE current pending-queue listing. Reset the
            # previous pending records once per such event, not at every RUN.
            if pending_reset_at.get(index) != row.get('tick'):
                latest_pending[index] = {}
                pending_reset_at[index] = row.get('tick')
            if row.get('processed') == '1':
                triggers[(index,row.get('faction'),row.get('id'))] += 1
    print(f'FILE {path.name}  lines={lines}  runs={len(latest_runs)}  distinct_run_seeds={len(seeds)}  processed_trigger_observations={sum(triggers.values())}')
    for index, run in sorted(latest_runs.items()):
        print(f" RUN {index:2} tick={run.get('tick','?'):>5} planned={run.get('planned','?'):>2} built={run.get('built','?'):>2} pending={run.get('pending','?'):>2} terminal={run.get('terminal_events','?')}")
        for f in range(1, 5):
            d = latest_factions.get(index, {}).get(f, {})
            requests = [p for p in latest_pending.get(index,{}).values() if p.get('faction') == str(f)]
            s = [site for (sf,_),site in latest_sites.get(index, {}).items() if sf == f]
            pion = latest_pioneer.get(index,{}).get(f,{})
            if not d:
                continue
            print(f"   P{f}: lifecycle={d.get('lifecycle','?')} live={d.get('live','?')} buildings={d.get('buildings','?')} pioneer={d.get('pioneers','?')} ws08={d.get('ws08','?')} ws09={d.get('ws09','?')} pending={len(requests)} attempts={[p.get('attempts') for p in requests]} commands={pion.get('pioneer_ai_commands','?')}")
            for site in s[:6]:
                print(f"      site arm={site.get('arm')} ({site.get('x')},{site.get('y')}) priority={site.get('priority')} reach_only={site.get('reach_only')} nearest_q12={site.get('nearest_constructor_q12')}")
            if d.get('buildings') == '0' and d.get('pioneers') == '1' and requests:
                print('      CHECK stalled pending primary queue; review RESOURCE_SITE reach_only and actual placement rejects')

if __name__ == '__main__':
    cli=argparse.ArgumentParser(description=__doc__)
    cli.add_argument('logs',nargs='+',type=Path)
    args=cli.parse_args()
    for file in args.logs:
        parse(file)
