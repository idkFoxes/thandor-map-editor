#!/usr/bin/env python3
"""Read V589/V590 Simulation Lab GUI forensic logs; never infer retail parity."""
import argparse
from collections import defaultdict
from pathlib import Path

def unpack(line):
    kind, _, rest = line.strip().partition(' ')
    return kind, dict(part.split('=', 1) for part in rest.split() if '=' in part)

def inspect(path):
    runs, factions, pioneers, sites = {}, {}, {}, {}
    for line in path.open(encoding='utf-8', errors='replace'):
        kind, f = unpack(line)
        try:
            run = int(f.get('run', f.get('index', '-1')))
            tick = int(f.get('tick', '-1'))
        except ValueError:
            continue
        if kind == 'RUN':
            if run not in runs or tick >= int(runs[run]['tick']): runs[run] = f
        elif kind == 'FACTION':
            key = (run, int(f['f']))
            if key not in factions or tick >= int(factions[key]['tick']): factions[key] = dict(f, tick=tick)
        elif kind == 'ENTITY' and f.get('class') == '18':
            key = (run, int(f['faction']))
            if key not in pioneers or tick >= int(pioneers[key]['tick']): pioneers[key] = f
        elif kind == 'RESOURCE_SITE':
            key = (run, int(f['faction']))
            if key not in sites or tick >= int(sites[key]['tick']): sites[key] = f
    print(f'{path}: {len(runs)} runs')
    for run, summary in sorted(runs.items()):
        print(f' run={run} tick={summary["tick"]} seed={summary.get("seed", "?")} '
              f'built={summary.get("built", "?")} retries={summary.get("retries", "?")} '
              f'conversions={summary.get("conversions_completed", "?")}')
        for fac in range(1, 5):
            state, e, site = factions.get((run,fac),{}), pioneers.get((run,fac),{}), sites.get((run,fac),{})
            reason = 'converted/absent'
            if e and state.get('pioneers') == '0':
                reason='converted; showing last historical Pioneer snapshot'
            elif e:
                if 'pioneer_runtime_steps' not in e: reason='unknown: V589 lacks step/hold telemetry'
                elif int(e.get('pioneer_hold_skips','0')): reason='conversion-hold skips'
                elif int(e.get('pioneer_immobile_skips','0')): reason='immobile skips'
                elif int(e.get('pioneer_runtime_steps','0')): reason='movement callback executing'
                else: reason='zero callbacks (investigate)'
            print(f'   P{fac}: structures={state.get("buildings","?")} pioneer={state.get("pioneers","?")} '
                  f'score={site.get("pioneer_weighted_score","-")} '
                  f'route_rej={e.get("pioneer_route_rejections","-")} '
                  f'commands={e.get("pioneer_ai_commands","-")} '
                  f'movement_flags={e.get("pioneer_movement_flags","-")} '
                  f'steps={e.get("pioneer_runtime_steps","-")} '
                  f'hold_bit={e.get("pioneer_hold_bit","-")} '
                  f'last_gate={e.get("pioneer_last_gate_action","-")} '
                  f'diagnosis={reason}')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs',nargs='+',type=Path)
    args=parser.parse_args()
    for path in args.logs: inspect(path)
if __name__ == '__main__': main()
