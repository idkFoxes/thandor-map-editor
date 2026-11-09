#!/usr/bin/env python3
"""Small contract fixture; deliberately does not simulate Asgard."""
import importlib.util
from pathlib import Path
import tempfile

module_path = Path(__file__).with_name('analyze_pioneer_gate.py')
spec = importlib.util.spec_from_file_location('gate_analysis', module_path)
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
assert mod.classify(None) == 'NO_LIVE_PIONEER_SNAPSHOT'
assert mod.classify({'pioneer_selector_calls':'0'}) == 'PRE_V594_GATE_INPUTS_UNKNOWN'
assert mod.classify({'pioneer_gate_calls':'0'}) == 'MOVEMENT_GATE_NOT_ENTERED'
assert mod.classify({'pioneer_gate_calls':'5','pioneer_gate_score_sites':'1'}) == 'SCORE_BRANCH_REACHED'
assert mod.classify({'pioneer_gate_calls':'5','pioneer_gate_reset_kept_special':'4'}) == 'RESET_WITH_SPECIAL_FLAG_PRESERVED_OBSERVED'
assert mod.classify({'pioneer_gate_calls':'5','pioneer_gate_updated':'0'}) == 'NO_MOVEMENT_UPDATE_IN_GATE'
assert mod.classify({'pioneer_gate_calls':'5','pioneer_gate_updated':'5','pioneer_gate_carry':'0'}) == 'NO_MOVEMENT_CARRY_IN_GATE'
assert mod.classify({'pioneer_gate_calls':'5','pioneer_gate_updated':'5','pioneer_gate_carry':'2','pioneer_gate_bootstrap':'1'}) == 'BOOTSTRAP_OCCURRED_NO_SCORE_BRANCH'
with tempfile.TemporaryDirectory() as tmp:
    path = Path(tmp) / 'trace.log'
    path.write_text('RUN index=0 tick=512 built=0 conversions_completed=0\n'
                    'ENTITY run=0 tick=512 faction=1 class=18 pioneer_gate_calls=5 '
                    'pioneer_gate_reset=5 pioneer_gate_reset_kept_special=5\n')
    runs, units, sites = mod.load(path)
    assert runs[0]['tick']=='512' and (0,1) in units and sites=={}
print('V594_GATE_ANALYZER PASS')
