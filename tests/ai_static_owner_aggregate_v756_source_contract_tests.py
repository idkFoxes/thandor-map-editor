#!/usr/bin/env python3
from source_contract_support import require_editor_version_at_least
from pathlib import Path
import argparse

parser=argparse.ArgumentParser()
parser.add_argument('--source',type=Path,default=Path(__file__).resolve().parents[1])
args=parser.parse_args();root=args.source.resolve()
environment=(root/'src/core/simulation_lab_environment.cpp').read_text()
phase7=(root/'src/core/simulation_lab.cpp').read_text()
site=(root/'src/core/simulation_lab_site_placement.cpp').read_text()
runtime=(root/'src/core/simulation_lab_runtime.cpp').read_text()
types=(root/'include/thandor/simulation_lab_environment_types.hpp').read_text()
metrics=(root/'include/thandor/simulation_lab_derived_selection_metrics.hpp').read_text()
order=(root/'include/thandor/simulation_lab_owner_order.hpp').read_text()

assert 'simulation_lab_is_model_runtime_root' in metrics
assert 'simulation_lab_accumulate_derived_selection_metrics' in metrics
assert 'runtime_root_count' in metrics
assert 'simulation_lab_derived_selection_metrics(' in runtime
assert 'source_placement_index{SIZE_MAX}' in types
assert 'derived_selection_metrics_valid' in types
assert 'owner_roots.push_back' in environment
assert 'descends_from' in environment
assert 'metrics,nodes[runtime_root],relative_height' in environment
assert 'owner.source_placement_index=placement_index' in environment
assert 'owner.derived_selection_metrics_valid=metrics.runtime_root_count!=0U' in environment
assert 'simulation_lab_owner_newer_first' in order
assert 'std::sort(ordered.begin(),ordered.end()' in phase7
assert 'owner.derived_selection_metrics_valid' in phase7
assert 'owner.visibility_radius_q12' in phase7
assert 'owner.visibility_height_offset_q12' in phase7
assert 'simulation_lab_owner_newer_first' in site
require_editor_version_at_least(root, 758)
print('V756_STATIC_OWNER_AGGREGATE_SOURCE_CONTRACT PASS')
