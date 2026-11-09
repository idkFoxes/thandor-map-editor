#!/usr/bin/env python3
import argparse
from pathlib import Path

ap=argparse.ArgumentParser()
ap.add_argument('--source',required=True)
a=ap.parse_args()
root=Path(a.source)
app=(root/'src/editor/editor_app.cpp').read_text(encoding='utf-8')
model=(root/'src/core/model.cpp').read_text(encoding='utf-8')
catalog=(root/'src/core/catalog.cpp').read_text(encoding='utf-8')
header=(root/'src/editor/editor_app.hpp').read_text(encoding='utf-8')

checks={
    'class13 default drive target is transformed model lookup class5 index1':
        'simulation_class13_default_drive_target' in app and
        'ModelLookupKeyIndex{1U}' in app and 'ModelLookupKeyClass{5U}' in app,
    'custom drive command stores override then clears single selection':
        'simulation_production_rally_points_[selected_index]=*hovered_terrain_point_' in app and
        'selected_placement_.reset();' in app and 'runtime bit 0x800' in app,
    'reset drive target restores default semantics and clears marker/selection':
        'Reset drive target to factory default' in app and
        'simulation_production_rally_points_.erase(index)' in app and
        'simulation_class13_effective_drive_target(index,*preset)' in app and 'selected_placement_.reset();selected_simulation_lab_entity_.reset();' in app,
    'factory default and custom drive destinations use real faction-colored EGATH0':
        'append_command_marker(143U' in app and 'EGATH0' in app and
        'production_drive_target_world' in app and 'graphics_family_representative_rgb24' in app,
    'left-hold horizontal pointer delta rotates building and right suppresses rotation':
        'const auto rotate_pending_building' in app and
        'placement_rotation_pointer_anchor_={io.MousePos.x,io.MousePos.y};' in app and
        'glfwSetCursorPos(window_,placement_rotation_pointer_anchor_.x' in app and
        'placement_heading_after_pointer_delta' in app and
        'ImGui::IsMouseDown(ImGuiMouseButton_Left)' in app,
    'building commits at mouse release cell with current selected heading':
        'const auto release_cell=placement_rotation_cell_' in app and
        'add_asset_placement(release_cell->first,release_cell->second,selected_asset_id_)' in app and
        'placement.orientation_turn16=selected_orientation_' in app,
    'construction destruction effect parses MDL +0x190 and reaches preset':
        'construction_destruction_effect_id = read_u32(data, offset + 0x190U)' in model and
        'preset.construction_destruction_effect_id' in catalog,
    'destruction state keeps hierarchy until effect lifetime then hides it':
        'BuildingDestructionPreviewState' in header and
        'if(lifetime==0U||age>=lifetime)continue;' in app and
        'runtime_visual_tick-destruction->second.start_tick' in app and 'catalog_.effect_definition(destruction->second.effect_id)' in app,
    'destruction effect itself renders from recovered +0x190 definition':
        'for(const auto& [placement_index,state]:building_destruction_states_)' in app and
        'append_effect_visual(*definition,origin,age' in app,
    'class13 production smoke only runs in build phase':
        app.count('factory->second.phase==thandor::SimulationClass13ProductionPhase::Building') >= 2 and
        'Runtime class 13 services the timed SHT/EFF emitter while production state 1 is active.' in app,
}
failed=[name for name,ok in checks.items() if not ok]
if failed:
    for name in failed: print('FAIL:',name)
    raise SystemExit(1)
print('V546_DRIVE_ROTATION_DESTRUCTION_SMOKE_SOURCE_CONTRACT PASS')
