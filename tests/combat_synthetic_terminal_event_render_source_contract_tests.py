#!/usr/bin/env python3
"""V703 source contract: synthetic terminal EFF is never placement/ground-mark filtered."""
from pathlib import Path
import argparse

parser=argparse.ArgumentParser()
parser.add_argument('--source',type=Path,required=True)
root=parser.parse_args().source
renderer=(root/'src/editor/editor_app.cpp').read_text()
worker=(root/'src/core/simulation_lab.cpp').read_text()
lab=(root/'include/thandor/simulation_lab.hpp').read_text()
start=renderer.index('for(const auto& event:one_shots){')
prepared=renderer.index('!event.terminal_attachment_world_points.empty()',start)
guard=renderer.index('if(event.source_placement_index>=placement_count)continue;',start)
assert prepared<guard, 'synthetic terminal effects must be processed before LEV-placement guard'
assert 'event.tick,point.position,\n                        orientation,true,false});' in renderer
assert 'event.tick,origin,orientation,true,false});' in renderer
assert 'event.terminal_attachment_world_points.push_back(point)' in worker
assert 'void SimulationLabExperiment::advance_terminal_attachment_effects(SimulationLabRun& run, const FieldGrid& field,' in worker
assert 'simulation_lab_current_model_poses(entity,field,*runtime_catalog_)' in worker
assert 'if (!direct_beam&&!environmental&&!model_contact) impact_position = target.actor.position;' not in worker
for field in ('projectile_child_component_hits','child_component_terminal_events','terminal_attachment_world_points_prepared'):
    assert field in lab and field in worker
print('V703 synthetic terminal event render source contract PASS')
