#!/usr/bin/env python3
"""Inspect one editor and one SimLab log; classify large stalls without losing run/mode context.

Examples:
  python tools/performance_hotspots.py editor.log simlab.log --json current.json
  python tools/performance_hotspots.py editor_old.log.zip simlab_old.zip --json baseline.json

Counters are cumulative, deferred ticks are repeated retry attempts, and
per-window maxima do not establish a frame-time percentile distribution.
"""
import argparse
import json
import statistics
from collections import defaultdict
from pathlib import Path
from performance_compare import parse_line, records


def parse_records(path, type_tag):
    return [parse_line(line) for _, line in records(path) if line.startswith(type_tag + ' ')]


def summarize_editor(rows):
    if not rows:
        return {}
    groups = defaultdict(list)
    for row in rows:
        groups[row.get('mode', 'unknown')].append(row)
    counters = ('object_update_calls', 'object_layers_uploaded', 'object_layers_resampled',
                'object_layers_relocated', 'object_bytes_uploaded', 'object_idle_waits', 'object_frame_fence_waits',
                'object_array_recreates', 'scene_model_cache_hits',
                'scene_model_cache_misses', 'scene_plain_geometry_fast_hits',
                'sprite_canvas_cache_hits', 'sprite_canvas_cache_misses',
                'object_source_reuse_hits', 'object_source_reuse_misses',
                'object_layer_stable', 'object_layer_moved', 'object_layer_added',
                'object_layer_removed', 'lab_snapshot_dirty_coalesced')
    stages = ('frame', 'frame_interval', 'ui', 'ui_main_menu', 'ui_ribbon',
              'ui_left_panel', 'ui_right_panel', 'ui_lab_overlay',
              'ui_canvas_interaction', 'lab_snapshot_sync', 'update',
              'object_rebuild', 'object_assembly', 'object_upload',
              'dynamic_build', 'models_setup', 'begin_frame_wait')
    result = {}
    for mode, samples in groups.items():
        count = sum(row.get('frame_calls', 0) for row in samples)
        first, last = samples[0], samples[-1]
        duration_s = sum(row.get('window_ms', 0) for row in samples) / 1000.0
        # Include the mode's first sample in counter deltas by using the immediately
        # preceding session sample as the initial cumulative counter reading.
        start_index = rows.index(first)
        previous = rows[start_index - 1] if start_index else {}
        deltas = {field: max(0, last.get(field, 0) - previous.get(field, 0))
                  for field in counters if field in last}
        data = {
            'samples': len(samples), 'duration_s_approx': round(duration_s, 2),
            'frames': count,
            'long_frames_total_at_last_sample': {
                limit: last.get('session_over_' + limit)
                for limit in ('16ms', '33ms', '100ms')},
            'counters_delta': deltas,
            'rates_per_second': {field: round(value / duration_s, 3)
                                 for field, value in deltas.items() if duration_s > 0},
            'stages': {},
        }
        for stage in stages:
            calls = sum(row.get(stage + '_calls', 0) for row in samples)
            if calls:
                data['stages'][stage] = {
                    'calls': calls,
                    'weighted_mean_ms': round(sum(row.get(stage + '_calls', 0) *
                                                  row.get(stage + '_avg_ms', 0)
                                                  for row in samples) / calls, 4),
                    'maximum_ms': round(max(row.get(stage + '_max_ms', 0)
                                            for row in samples), 4),
                }
        result[mode] = data
    result['_worst_windows'] = [{
        'sample': row.get('sample'), 'uptime_s': round(row.get('uptime_ms', 0)/1000, 1),
        'mode': row.get('mode'), 'frame_max_ms': row.get('frame_max_ms'),
        'update_max_ms': row.get('update_max_ms'),
        'object_rebuild_max_ms': row.get('object_rebuild_max_ms'),
        'object_upload_max_ms': row.get('object_upload_max_ms'),
        'ui_max_ms': row.get('ui_max_ms'),
        'ui_left_panel_max_ms': row.get('ui_left_panel_max_ms'),
        'ui_right_panel_max_ms': row.get('ui_right_panel_max_ms'),
        'ui_lab_overlay_max_ms': row.get('ui_lab_overlay_max_ms'),
        'lab_snapshot_sync_max_ms': row.get('lab_snapshot_sync_max_ms'),
        'object_assembly_max_ms': row.get('object_assembly_max_ms'),
        'begin_frame_wait_max_ms': row.get('begin_frame_wait_max_ms'),
        'submit_present_max_ms': row.get('submit_present_max_ms')
    } for row in sorted(rows, key=lambda row: row.get('frame_max_ms', 0), reverse=True)[:10]]
    return result


def summarize_simlab(rows):
    if not rows:
        return {}
    final = rows[-1]
    fields = ('step_wall_us', 'snapshot_copy_us', 'gui_dynamic_build_ms',
              'gui_pre_shadow_ms', 'gui_model_setup_ms', 'pending_ticks')
    return {
        'samples': len(rows),
        'final_worker_counters': {field: final.get(field) for field in (
            'worker_accepted_ticks', 'worker_deferred_ticks',
            'worker_backpressure_events', 'peak_pending_ticks', 'pending_ticks')},
        'metrics': {field: {
            'mean': round(statistics.mean(row[field] for row in rows if field in row), 3),
            'maximum': max(row[field] for row in rows if field in row),
        } for field in fields if any(field in row for row in rows)},
        'cautions': [
            'Deferred ticks counts rejected retries, which may be counted repeatedly; not lost simulation ticks.',
            'Step wall timings are sampled generation values, not a full-run CPU profile.',
            'Compare workloads with identical maps, seeds, run counts and tick/graphics settings.'
        ]
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('editor', type=Path)
    parser.add_argument('simlab', type=Path)
    parser.add_argument('--json', type=Path)
    args = parser.parse_args()
    result = {
        'editor': summarize_editor(parse_records(args.editor, 'EDITOR_PERF')),
        'simlab': summarize_simlab(parse_records(args.simlab, 'PERF')),
        'notes': ['Different modes should be compared separately.',
                  'Object-upload counters include host-side copies, not GPU completion times.',
                  'No GPU timestamp measurements are present in these logs.']
    }
    output = json.dumps(result, indent=2, sort_keys=True)
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(output + '\n', encoding='utf-8')
    print(output)


if __name__ == '__main__':
    main()
