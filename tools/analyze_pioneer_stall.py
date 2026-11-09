#!/usr/bin/env python3
"""V590+ forensic replay triage. Reports observations, not retail-parity verdicts."""
import argparse
from collections import defaultdict
from pathlib import Path


def read_kv(line):
    pieces=line.split()
    if not pieces:
        return None, {}
    return pieces[0], dict(item.split('=',1) for item in pieces[1:] if '=' in item)


def read_log(path):
    runs, factions, pioneers, sites = {}, {}, {}, {}
    for line in path.open(encoding='utf-8',errors='replace'):
        kind,v=read_kv(line)
        if kind not in ('RUN','FACTION','ENTITY','RESOURCE_SITE'):
            continue
        try:
            run=int(v.get('index' if kind=='RUN' else 'run', '-1'))
            tick=int(v.get('tick','-1'))
        except ValueError:
            continue
        if kind=='RUN':
            if run not in runs or tick>=int(runs[run]['tick']): runs[run]=v
        elif kind=='FACTION' and 'f' in v:
            key=(run,int(v['f']))
            if key not in factions or tick>=int(factions[key]['tick']): factions[key]=dict(v,tick=str(tick))
        elif kind=='ENTITY' and v.get('class')=='18':
            key=(run,int(v['faction']))
            if key not in pioneers or tick>=int(pioneers[key]['tick']): pioneers[key]=v
        elif kind=='RESOURCE_SITE':
            key=(run,int(v['faction']))
            if key not in sites or tick>=int(sites[key]['tick']): sites[key]=v
    return runs,factions,pioneers,sites


def report(path):
    runs,factions,pioneers,sites=read_log(path)
    print(f'log={path} runs={len(runs)} (Simulation Lab, not retail)')
    counts=defaultdict(int)
    for run,overview in sorted(runs.items()):
        print(f'run={run} tick={overview["tick"]} buildings={overview.get("built","?")} '
              f'conversions={overview.get("conversions_completed","?")}')
        for faction in range(1,5):
            f=factions.get((run,faction),{})
            e=pioneers.get((run,faction),{})
            site=sites.get((run,faction),{})
            live=f.get('pioneers')
            if live=='0':
                status='converted-or-absent'  # historical snapshot cannot prove conversion by itself
            elif not e:
                status='no-entity-snapshot'
            elif 'pioneer_runtime_steps' not in e:
                status='pre-V590-unknown-callback'
            elif int(e.get('pioneer_hold_skips',0))>0:
                status='hold-skipped'
            elif int(e.get('pioneer_immobile_skips',0))>0:
                status='immobile-skipped'
            elif int(e.get('pioneer_runtime_steps',0))>0 and int(e.get('pioneer_ai_commands',0))==1:
                status='callback-active-single-command'
            else:
                status='other'
            if status=='callback-active-single-command': counts[faction]+=1
            print('  P{} {} live={} buildings={} callback_steps={} hold_skips={} immobile_skips={} '
                  'commands={} last_gate={} movement_flags={} route_rejects={} site_priority={} '
                  'site_score={} mode3_mode0=UNOBSERVED'.format(
                    faction,status,live,f.get('buildings','?'),e.get('pioneer_runtime_steps','?'),
                    e.get('pioneer_hold_skips','?'),e.get('pioneer_immobile_skips','?'),
                    e.get('pioneer_ai_commands','?'),e.get('pioneer_last_gate_action','?'),
                    e.get('pioneer_movement_flags','?'),e.get('pioneer_route_rejections','?'),
                    site.get('priority','?'),site.get('pioneer_weighted_score','?')))
    print('callback-active-single-command run counts:',dict(sorted(counts.items())))
    print('NOTE: A zero site score is not a proven root cause; workspace admission, '
          'mode3/0 placement, and ARM333 side effects are not captured by this log.')


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs',nargs='+',type=Path)
    opts=parser.parse_args()
    for file in opts.logs: report(file)
