#!/usr/bin/env python3
import argparse
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('--source',required=True)
args=parser.parse_args()
root=Path(args.source)
app=(root/'src/editor/editor_app.cpp').read_text()
app_h=(root/'src/editor/editor_app.hpp').read_text()
lab=(root/'src/core/simulation_lab.cpp').read_text()
field=(root/'src/core/field.cpp').read_text()

failures=[]
def req(ok,msg):
    if not ok: failures.append(msg)

req('simulation_lab_worker_.start(simulation_lab_config_,ai_knowledge_->parameters(),seeds,' in app and
    'faction_economy,production_assets,technology_assets,faction_technology_masks,' in app and
    'faction_score_seeds,document_->field(),scenario_runtime)' in app,
    'Simulation Lab is not seeded from active map actors / production/research catalogs through the worker FLD snapshot boundary')
req('selected_states[entity.source_placement_index]=entity.actor' in app,
    'selected run is not synchronized into the full transient actor renderer')
req('if(!simulation_lab_show_all_runs_)return;' in app and 'project_to_canvas(label_position,vp,sx,sy,depth)' in app,
    'shared-world run marker projection missing')
req('Simulation Lab presents every isolated world in one real 3D coordinate system' in app,
    'Vulkan dynamic-scene multi-run integration missing')
req('simulation_lab_show_all_runs_' in app_h and 'draw_simulation_lab_overlay()' in app,
    'multi-run comparison controls/overlay missing')
req('retail_field_diagnostics_' in app_h and 'draw_retail_field_diagnostics_overlay()' in app,
    'retail field diagnostic overlay is not integrated')
for token in ['0x00200000U','0x04000000U','0x20000000U','0x40000000U','0x00001800U','0x00008000U']:
    req(token in field, f'missing machine-verified retail field marker predicate {token}')
req('retail_field_marker_audit(cell.flags_and_material,selector)' in app and
    'project_to_canvas(terrain_point,vp,primary_x,primary_y,primary_depth)' in app,
    'field marker audit/projection missing')
req('GZF consumer verified; downstream retail effect not modeled in this lab' in lab,
    'Simulation Lab no longer distinguishes verified consumers from modeled effects')

if failures:
    for f in failures: print('FAIL:',f)
    raise SystemExit(1)
print('V534_EDITOR_SOURCE_CONTRACT PASS')
