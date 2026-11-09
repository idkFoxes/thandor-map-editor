#!/usr/bin/env python3
import sys
import tempfile
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from performance_compare import aggregate_editor, aggregate_simlab, compare, summarize

with tempfile.TemporaryDirectory() as tmp:
    a = Path(tmp)/'editor.zip'
    line1='EDITOR_PERF sample=1 map="Asgard 1" mode=lab frame_calls=10 frame_avg_ms=5 frame_max_ms=9 object_layers_relocated=4 session_frames=10\n'
    line2='EDITOR_PERF sample=2 map="Asgard 1" mode=lab frame_calls=20 frame_avg_ms=10 frame_max_ms=11 object_layers_relocated=8 session_frames=30\n'
    with zipfile.ZipFile(a,'w') as z:
        z.writestr('logs/trace.log',line1+line2)
    result=next(iter(summarize(a).values()))
    assert result['map']=='Asgard 1' and result['stages']['frame']['calls']==30
    assert abs(result['stages']['frame']['weighted_avg_ms']-8.333333)<0.0001
    assert result['counters']['object_layers_relocated']==8
    newer=aggregate_editor([{'sample':1,'map':'Asgard 1','mode':'lab','frame_calls':10,'frame_avg_ms':12}])
    delta=compare(result,newer)
    assert delta['comparable'] and delta['changes']['frame']['change_percent']>40
    bad=aggregate_editor([{'sample':1,'map':'Elsewhere','mode':'lab','frame_calls':1,'frame_avg_ms':1}])
    assert not compare(result,bad)['comparable']
    sim=aggregate_simlab([{'sample':1,'step_wall_us':25,'worker_backpressure_events':1},
                          {'sample':2,'step_wall_us':35,'worker_backpressure_events':3}])
    assert sim['metrics']['step_wall_us']['mean']==30 and sim['counters']['worker_backpressure_events']==3
print('PERFORMANCE_COMPARE PASS editor=1 simlab=1 zip=1 equivalence_checks=1')
