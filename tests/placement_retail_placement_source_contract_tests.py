#!/usr/bin/env python3
import argparse
from pathlib import Path

p=argparse.ArgumentParser(); p.add_argument('--source',required=True); a=p.parse_args(); root=Path(a.source)
core=(root/'src/core/simulation_lab.cpp').read_text()
editor=(root/'src/editor/editor_app.cpp').read_text()
g410=(root/'tests/placement_resource_extractor_context_order_tests.cpp').read_text()

def req(c,m):
    if not c: raise SystemExit('FAIL: '+m)

req('kFootprintStepQ12 = 0x240' in core and 'base_x = original_x & ~0xFF' in core and
    'can_place_one(original_x, original_y)' in core and
    'can_place_one(base_x + offset_x, base_y + offset_y)' in core and
    'if (auto placed = can_place_one' in core,
    'retail first-success center/corner footprint validation is missing')
req('kTerrainRadiusQ12 = 0xC00' in core and '0x88006000U' in core and 'water_delta_q12 > 0' in core,
    'retail terrain placement rejection predicates are missing')
req('lab_append_resource08' in core and 'ws.resource08' in core and
    'const std::int64_t duplicate' in core and 'prior.army_asset_id != id' in core,
    'resource site workspace08 separation/replacement is missing')
req('run.ai_parameters[80U]' in core and '0x55' in core,
    'retail resource-site KI[0x50] evidence is not retained')
req('if(ImGui::Checkbox("Show other runs"' in editor and
    'simulation_static_replacement_dirty_=true' in editor,
    'other-run marker toggle does not invalidate dynamic geometry for the next update')
req('OpaqueDepthWrite' in g410 and ('alpha' in g410.lower()),
    'G410 authored opaque extractor-footprint regression was lost')
print('V566_SIMULATION_LAB_RETAIL_PLACEMENT_SOURCE_CONTRACT PASS')
