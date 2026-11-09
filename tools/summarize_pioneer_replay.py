#!/usr/bin/env python3
"""Summarize V588+ Simulation Lab GUI replay logs without treating them as retail traces."""
from __future__ import annotations
import argparse
import pathlib


def parse_fields(line: str) -> tuple[str, dict[str, str]]:
    words = line.strip().split()
    return (words[0], dict(word.split('=', 1) for word in words[1:] if '=' in word)) if words else ('', {})


def summarize(path: pathlib.Path) -> str:
    runs: dict[int, dict[str, str]] = {}
    factions: dict[tuple[int, int], dict[str, str]] = {}
    sites: dict[tuple[int, int], dict[str, str]] = {}
    pioneers: dict[tuple[int, int], dict[str, str]] = {}
    ended = False
    start: dict[str, str] = {}
    for raw in path.open(encoding='utf-8', errors='replace'):
        kind, fields = parse_fields(raw)
        try:
            run = int(fields.get('run', fields.get('index', '0')))
            faction = int(fields.get('faction', fields.get('f', '0')))
        except ValueError:
            continue
        if kind == 'START':
            start = fields
        elif kind == 'END':
            ended = True
        elif kind == 'RUN':
            runs[run] = fields
        elif kind == 'FACTION' and faction in (1, 2, 3, 4):
            factions[(run, faction)] = fields
        elif kind == 'RESOURCE_SITE' and faction in (1, 2, 3, 4) and fields.get('index') == '0':
            sites[(run, faction)] = fields
        elif kind == 'ENTITY' and fields.get('class') == '18' and faction in (1, 2, 3, 4):
            pioneers[(run, faction)] = fields
    output = [f'{path.name}  runs={start.get("runs", "?")} '
              f'vary_seed={start.get("vary_seed_per_run", "?")} '
              f'end_marker={ended}']
    for run in sorted(runs):
        r = runs[run]
        output.append(f'  run={run} tick={r.get("tick")} built={r.get("built")} '
                      f'retries={r.get("retries")} terminal={r.get("terminal_events")}')
        for f in range(1, 5):
            d = factions.get((run, f), {})
            p = pioneers.get((run, f), {})
            s = sites.get((run, f), {})
            output.append(f'    P{f} structures={d.get("buildings", "?")} '
                          f'pioneers={d.get("pioneers", "?")} '
                          f'site_score={s.get("pioneer_weighted_score", "-")} '
                          f'mode0_reach={s.get("mode0_radius_with_extent", "-")} '
                          f'move_commands={p.get("pioneer_ai_commands", "-")} '
                          f'route_rejections={p.get("pioneer_route_rejections", "-")} '
                          f'movement_flags={p.get("pioneer_movement_flags", "-")}')
    return '\n'.join(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('logs', type=pathlib.Path, nargs='+')
    args = parser.parse_args()
    for path in args.logs:
        print(summarize(path))
