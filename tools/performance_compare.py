#!/usr/bin/env python3
"""Summarize editor/SimLab logs and compare repeatable performance runs.

Usage:
  python tools/performance_compare.py editor_*.log.zip Simlab.zip --json report.json
  python tools/performance_compare.py baseline.log.zip current.log.zip --compare

Never interpret independently sampled stage timings as disjoint wall-time.
"""
import argparse
import json
import re
import shlex
import statistics
import sys
import zipfile
from collections import defaultdict
from pathlib import Path


def records(path):
    """Yield (name, log line) from a plain log or each log within a ZIP."""
    if zipfile.is_zipfile(path):
        with zipfile.ZipFile(path) as archive:
            for entry in archive.infolist():
                if entry.filename.lower().endswith('.log') and not entry.is_dir():
                    # Do not extract untrusted archive paths to disk.
                    with archive.open(entry) as stream:
                        for line in stream:
                            yield entry.filename, line.decode('utf-8', 'replace').strip()
    else:
        with open(path, encoding='utf-8', errors='replace') as stream:
            for line in stream:
                yield str(path), line.strip()


def parse_line(line):
    # shlex preserves map="map with spaces" while avoiding manual regexp tricks.
    try:
        fields = shlex.split(line)
    except ValueError:
        return {}
    out = {}
    for field in fields[1:]:
        if '=' not in field:
            continue
        key, value = field.split('=', 1)
        try:
            out[key] = float(value) if any(c in value for c in '.eE') else int(value)
        except ValueError:
            out[key] = value
    return out


def aggregate_editor(rows):
    keys = set().union(*(set(row) for row in rows))
    # Counters such as object_update_calls are not timed stages.
    stages = sorted(set(k[:-6] for k in keys
                        if k.endswith('_calls') and k[:-6]+'_avg_ms' in keys))
    durations = {}
    for stage in stages:
        samples = [(row.get(stage+'_calls', 0), row.get(stage+'_avg_ms', 0)) for row in rows]
        calls = sum(n for n, _ in samples if isinstance(n, (int, float)))
        if calls:
            durations[stage] = {'calls': int(calls),
                'weighted_avg_ms': round(sum(n*ms for n, ms in samples) / calls, 6),
                'peak_sample_ms': round(max(float(r.get(stage+'_max_ms', 0)) for r in rows), 6)}
    latest = max(rows, key=lambda r: r.get('sample', 0))
    fields = ('session_frames', 'session_over_16ms', 'session_over_33ms', 'session_over_100ms',
              'object_bytes_uploaded', 'object_idle_waits', 'object_frame_fence_waits', 'object_layers_resampled',
              'object_layers_relocated', 'scene_model_cache_hits', 'scene_model_cache_misses',
              'scene_plain_geometry_fast_hits', 'sprite_canvas_cache_hits',
              'sprite_canvas_cache_misses', 'sprite_canvas_cache_evictions',
              'sprite_canvas_cache_bytes', 'object_source_reuse_hits',
              'object_source_reuse_misses', 'object_source_reuse_bytes',
              'object_layer_stable', 'object_layer_moved', 'object_layer_added',
              'object_layer_removed', 'lab_snapshot_dirty_coalesced',
              'log_failures')
    counters = {k: latest[k] for k in fields if k in latest}
    return {'kind': 'editor', 'samples': len(rows), 'map': latest.get('map'),
            'mode': latest.get('mode'), 'counters': counters, 'stages': durations}


def aggregate_simlab(rows):
    fields = ('step_wall_us', 'snapshot_copy_us', 'pending_ticks', 'peak_pending_ticks',
              'gui_dynamic_build_ms', 'gui_pre_shadow_ms', 'gui_model_setup_ms',
              'gui_shadow_ms', 'gui_effects_ms', 'gui_sort_ms')
    out = {}
    for field in fields:
        numbers = [float(row[field]) for row in rows if isinstance(row.get(field), (int, float))]
        if numbers:
            out[field] = {'mean': round(statistics.mean(numbers), 6), 'peak': max(numbers),
                          'samples': len(numbers)}
    final = max(rows, key=lambda r: r.get('sample', 0))
    counters = {k: final[k] for k in ('worker_accepted_ticks', 'worker_deferred_ticks',
                                     'worker_backpressure_events', 'peak_pending_ticks') if k in final}
    return {'kind': 'simlab', 'samples': len(rows), 'counters': counters, 'metrics': out}


def summarize(path):
    groups = defaultdict(lambda: {'editor': [], 'simlab': []})
    for name, line in records(path):
        if line.startswith('EDITOR_PERF '):
            groups[name]['editor'].append(parse_line(line))
        elif line.startswith('PERF '):
            groups[name]['simlab'].append(parse_line(line))
    results = {}
    for name, group in groups.items():
        if group['editor']:
            results[name] = aggregate_editor(group['editor'])
        elif group['simlab']:
            results[name] = aggregate_simlab(group['simlab'])
    return results


def compare(previous, current):
    """Percent difference only for matching report kinds; never claim hardware equality."""
    if previous['kind'] != current['kind']:
        return {'comparable': False, 'reason': 'log kinds differ'}
    if previous['kind'] == 'editor':
        if (previous.get('map'), previous.get('mode')) != (current.get('map'), current.get('mode')):
            return {'comparable': False, 'reason': 'map or mode differs'}
        a, b, value = previous['stages'], current['stages'], 'weighted_avg_ms'
    else:
        a, b, value = previous['metrics'], current['metrics'], 'mean'
    changes = {}
    for key in sorted(a.keys() & b.keys()):
        before, after = a[key][value], b[key][value]
        if before > 0:
            changes[key] = {'before': before, 'after': after,
                            'change_percent': round(100.0*(after/before-1), 2)}
    return {'comparable': True, 'changes': changes,
            'caution': 'Compare only equivalent maps, work, runs, seeds and hardware; stages overlap.'}


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('logs', type=Path, nargs='+', help='ZIP or plain performance log')
    ap.add_argument('--json', type=Path, help='write machine-readable JSON report')
    ap.add_argument('--compare', action='store_true', help='compare the first record in each of two inputs')
    args = ap.parse_args()
    data = {str(path): summarize(path) for path in args.logs}
    if args.compare:
        if len(args.logs) != 2:
            ap.error('--compare needs two log inputs')
        a, b = [next(iter(data[str(path)].values()), None) for path in args.logs]
        if not a or not b:
            ap.error('one input has no performance samples')
        data['comparison'] = compare(a, b)
    result = json.dumps(data, indent=2, sort_keys=True)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(result+'\n', encoding='utf-8')
    print(result)


if __name__ == '__main__':
    main()
