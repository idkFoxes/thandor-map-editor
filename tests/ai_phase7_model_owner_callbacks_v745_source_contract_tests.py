#!/usr/bin/env python3
from source_contract_support import require_editor_version_at_least
from pathlib import Path
import argparse

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1])
args = parser.parse_args()
root = args.source.resolve()
core = (root / "src/core/simulation_lab.cpp").read_text()

assert "struct LabPhase7ModelOwnerCallback" in core
assert "lab_phase7_model_owner_callbacks(" in core
assert "simulation_lab_current_model_poses(entity,field,catalog)" in core
assert "const auto& owner=static_owners[entry.index]" in core
assert "owner.derived_selection_metrics_valid" in core
assert "model.simulation.occupancy_mark_radius_units" in core
assert "model.simulation.fog_reference_height_offset_q12" in core
assert "model_owner_projection=build_projected_fog_preview_occupancy(field,observers);" in core
assert "use_model_owner_callbacks?model_owner_projection[cell]" in core
assert "lab_rebuild_phase7_field_presence(scratch,run,field,catalog,static_owners);" in core
assert "lab_rebuild_phase7_field_presence(*next,run,field,catalog,static_owners,true," in core
require_editor_version_at_least(root, 758)
print("V745_PHASE7_MODEL_OWNER_CALLBACKS_SOURCE_CONTRACT PASS")
