#!/usr/bin/env python3
"""V593 selector-time SimLab forensic summary. Never asserts retail placement parity."""
from argparse import ArgumentParser
from pathlib import Path
from collections import Counter


def kv(line):
    parts = line.split()
    if not parts:
        return '', {}
    return parts[0], dict(p.split('=', 1) for p in parts[1:] if '=' in p)


def parse(path):
    runs, entities, faction_sites = {}, {}, {}
    for line in path.open(encoding='utf-8', errors='replace'):
        kind, values = kv(line)
        if kind not in ('RUN', 'ENTITY', 'RESOURCE_SITE'):
            continue
        try:
            run = int(values.get('index' if kind == 'RUN' else 'run', '-1'))
            tick = int(values['tick'])
        except (KeyError, ValueError):
            continue
        if kind == 'RUN':
            if run not in runs or tick >= int(runs[run]['tick']):
                runs[run] = values
        elif kind == 'ENTITY' and values.get('class') == '18':
            try:
                faction = int(values['faction'])
            except (KeyError, ValueError):
                continue
            key = run, faction
            if key not in entities or tick >= int(entities[key]['tick']):
                entities[key] = values
        elif kind == 'RESOURCE_SITE':
            try:
                faction = int(values['faction'])
            except (KeyError, ValueError):
                continue
            key = run, faction
            if key not in faction_sites or tick >= int(faction_sites[key]['tick']):
                faction_sites[key] = values
    return runs, entities, faction_sites


def classify(entity):
    if not entity:
        return 'NO_PIONEER_SNAPSHOT'
    if 'pioneer_selector_calls' not in entity:
        return 'PRE_V593_SELECTOR_UNKNOWN'
    if int(entity['pioneer_selector_calls']) == 0:
        return 'SCORE_SELECTOR_NOT_ENTERED'
    if int(entity['pioneer_selector_candidates']) == 0:
        return 'EMPTY_WORKSPACE_AT_LAST_SELECTOR'
    if int(entity['pioneer_selector_positive']) == 0:
        return 'NO_POSITIVE_SCORE_AT_LAST_SELECTOR'
    if int(entity['pioneer_route_rejections']) > 0:
        return 'POSITIVE_SCORE_ROUTE_REJECTED_AT_SOME_POINT'
    return 'POSITIVE_SCORE_ADMISSION_UNMODELED'


def report(path):
    runs, entities, sites = parse(path)
    print(f'FILE {path} runs={len(runs)} (SimLab telemetry, not a retail trace)')
    categories = Counter()
    for run in sorted(runs):
        print('RUN {} tick={} built={} conversions={}'.format(
            run, runs[run]['tick'], runs[run].get('built', '?'),
            runs[run].get('conversions_completed', '?')))
        for faction in range(1, 5):
            e = entities.get((run, faction), {})
            s = sites.get((run, faction), {})
            status = classify(e)
            categories[faction, status] += 1
            print(' P{} {} callbacks={} commands={} gate={} flags={} '
                  'selector_calls={} selector_tick={} sites={} scored={} positive={} '
                  'best_score={} site_priority={} economy_score={} '
                  'support_mode3={} support_mode0={} support_inputs={}'.format(
                      faction, status, e.get('pioneer_runtime_steps', '?'),
                      e.get('pioneer_ai_commands', '?'), e.get('pioneer_last_gate_action', '?'),
                      e.get('pioneer_movement_flags', '?'), e.get('pioneer_selector_calls', '?'),
                      e.get('pioneer_selector_last_tick', '?'),
                      e.get('pioneer_selector_candidates', '?'),
                      e.get('pioneer_selector_evaluated', '?'),
                      e.get('pioneer_selector_positive', '?'),
                      e.get('pioneer_selector_best_score', '?'),
                      s.get('priority', '?'), s.get('pioneer_weighted_score', '?'),
                      s.get('support_only_mode3_reached', '?'),
                      s.get('support_only_mode0_reached', '?'),
                      s.get('support_only_inputs_available', '?')))
    print('COUNTS', dict(sorted(categories.items())))
    print('IMPORTANT: support_* fields are owner-reach ONLY. Runtime collision, '
          'terrain contact callbacks, field blocker, source owner-list ordering, '
          'mode-1 and ARM333 reservations remain unverified. Zero-positive-score '
          'means this modeled selector found no positive score, NOT why retail stalls.')


if __name__ == '__main__':
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('logs', type=Path, nargs='+')
    args = parser.parse_args()
    for log in args.logs:
        report(log)
