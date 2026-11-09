#!/usr/bin/env python3
import importlib.util
from pathlib import Path
import tempfile
source=Path(__file__).with_name('analyze_pioneer_gate_telemetry.py')
spec=importlib.util.spec_from_file_location('v595_analyze',source)
a=importlib.util.module_from_spec(spec);spec.loader.exec_module(a)
assert a.classify(None)=='NO_LIVE_PIONEER_AT_FINAL_SNAPSHOT'
assert a.classify({'pioneer_gate_calls':'12'})=='WORKSPACE_HISTORY_UNKNOWN_PRE_V595'
assert a.classify({'pioneer_gate_calls':'12','pioneer_gate_ws_equal':'11','pioneer_gate_ws_unequal':'0'})=='INTEGRITY_FAILURE_WORKSPACE_ACCOUNTING'
assert a.classify({'pioneer_gate_calls':'12','pioneer_gate_ws_equal':'12','pioneer_gate_ws_unequal':'0'})=='ALL_OBSERVED_GATES_EQUAL_WORKSPACES'
assert a.classify({'pioneer_gate_calls':'12','pioneer_gate_ws_equal':'11','pioneer_gate_ws_unequal':'1','pioneer_gate_special_unequal_sites':'1','pioneer_gate_ws_unequal_carry':'0'})=='UNEQUAL_WORKSPACES_SPECIAL_BLOCKS_UPDATE_NO_CARRY_OBSERVED'
with tempfile.TemporaryDirectory() as td:
    path=Path(td)/'run.log'
    path.write_text('RUN index=0 tick=20 conversions_completed=0\nENTITY run=0 tick=20 faction=1 class=18 pioneer_gate_calls=3 pioneer_gate_ws_equal=3 pioneer_gate_ws_unequal=0\n')
    runs,units=a.load(path)
    assert runs[0]['tick']=='20' and a.classify(units[(0,1)])=='ALL_OBSERVED_GATES_EQUAL_WORKSPACES'
print('V595_PIONEER_ANALYZER PASS')
