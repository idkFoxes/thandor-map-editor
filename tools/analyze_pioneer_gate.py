#!/usr/bin/env python3
"""Explain V594 Pioneer gate blockers without inferring unknown retail side effects."""
from argparse import ArgumentParser
from collections import Counter
from pathlib import Path


def parse_kv(line):
    parts = line.split()
    return (parts[0], dict(s.split('=', 1) for s in parts[1:] if '=' in s)) if parts else ('', {})


def get_int(d, key, default=0):
    return int(d.get(key, default))


def load(path):
    runs, units, sites = {}, {}, {}
    for line in path.open(encoding='utf8', errors='replace'):
        kind, d = parse_kv(line)
        if kind not in ('RUN', 'ENTITY', 'RESOURCE_SITE'):
            continue
        try:
            run = get_int(d, 'index' if kind == 'RUN' else 'run', -1)
            tick = int(d['tick'])
        except (ValueError, KeyError):
            continue
        if kind == 'RUN':
            target, key = runs, run
        else:
            try:
                faction = int(d['faction'])
            except (ValueError, KeyError):
                continue
            if kind == 'ENTITY' and d.get('class') != '18':
                continue
            target, key = (units if kind == 'ENTITY' else sites), (run, faction)
        if key not in target or tick >= get_int(target[key], 'tick'):
            target[key] = d
    return runs, units, sites


def classify(unit):
    if unit is None:
        return 'NO_LIVE_PIONEER_SNAPSHOT'
    if 'pioneer_gate_calls' not in unit:
        return 'PRE_V594_GATE_INPUTS_UNKNOWN'
    if get_int(unit, 'pioneer_gate_calls') == 0:
        return 'MOVEMENT_GATE_NOT_ENTERED'
    if get_int(unit, 'pioneer_gate_score_sites') > 0:
        return 'SCORE_BRANCH_REACHED'
    if get_int(unit, 'pioneer_gate_reset_kept_special') > 0:
        return 'RESET_WITH_SPECIAL_FLAG_PRESERVED_OBSERVED'
    if get_int(unit, 'pioneer_gate_updated') == 0:
        return 'NO_MOVEMENT_UPDATE_IN_GATE'
    if get_int(unit, 'pioneer_gate_carry') == 0:
        return 'NO_MOVEMENT_CARRY_IN_GATE'
    if get_int(unit, 'pioneer_gate_bootstrap'):
        return 'BOOTSTRAP_OCCURRED_NO_SCORE_BRANCH'
    if get_int(unit, 'pioneer_gate_no_command'):
        return 'NO_COMMAND_GATE_BRANCH'
    return 'GATE_NONSCORING_REASON_REQUIRES_HISTORY'


def report(path):
    runs, units, sites = load(path)
    print('FILE', path, 'runs=', len(runs), '(SimLab, not retail)')
    counts = Counter()
    for run in sorted(runs):
        print('RUN', run, 'tick', runs[run].get('tick'),
              'built', runs[run].get('built'),
              'conversions', runs[run].get('conversions_completed'))
        for faction in range(1, 5):
            unit = units.get((run, faction))
            status = classify(unit)
            counts[faction, status] += 1
            d = unit or {}
            site = sites.get((run, faction), {})
            print(f' P{faction} {status} callbacks={d.get("pioneer_runtime_steps", "?")}'
                  f' commands={d.get("pioneer_ai_commands", "?")}'
                  f' selector={d.get("pioneer_selector_calls", "?")}'
                  f' gate={d.get("pioneer_gate_calls", "?")}'
                  f' scheduled={d.get("pioneer_gate_scheduled", "?")}'
                  f' special={d.get("pioneer_gate_special", "?")}'
                  f' updated={d.get("pioneer_gate_updated", "?")}'
                  f' carry={d.get("pioneer_gate_carry", "?")}'
                  f' bootstrap={d.get("pioneer_gate_bootstrap", "?")}'
                  f' score_branch={d.get("pioneer_gate_score_sites", "?")}'
                  f' reset={d.get("pioneer_gate_reset", "?")}'
                  f' reset_kept_special={d.get("pioneer_gate_reset_kept_special", "?")}'
                  f' entry_flags={d.get("pioneer_gate_last_entry_flags", "?")}'
                  f' post_gate_flags={d.get("pioneer_gate_last_gate_flags", "?")}'
                  f' ws00={d.get("pioneer_gate_last_ws00", "?")}'
                  f' ws04={d.get("pioneer_gate_last_ws04", "?")}'
                  f' ws08={d.get("pioneer_gate_last_ws08", "?")}'
                  f' support_only_mode3={site.get("support_only_mode3_reached", "?")}'
                  f' support_only_mode0={site.get("support_only_mode0_reached", "?")}')
            if 'pioneer_gate_calls' in d:
                total = sum(get_int(d, key) for key in (
                    'pioneer_gate_no_command', 'pioneer_gate_bootstrap',
                    'pioneer_gate_score_sites', 'pioneer_gate_reset',
                    'pioneer_gate_await'))
                if total != get_int(d, 'pioneer_gate_calls'):
                    print(f'  INTEGRITY_WARNING action_total={total}'
                          f' calls={d["pioneer_gate_calls"]}')
    print('SUMMARY', dict(sorted(counts.items())))
    print('BOUNDARY: gate observation is source-model telemetry only. A reset'
          ' retaining 0x100 is not proof that the retail reset routine retains it.'
          ' Support-only reach never proves actual placement eligibility.')


if __name__ == '__main__':
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('logs', type=Path, nargs='+')
    args = parser.parse_args()
    for path in args.logs:
        report(path)
