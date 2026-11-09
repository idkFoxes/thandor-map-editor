#!/usr/bin/env python3
"""Selected-run renderer must use the live MDL damage component, not the edit-time tag."""
import argparse
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--source',type=Path,required=True);src=p.parse_args().source
text=(src/'src/editor/editor_app.cpp').read_text()
start=text.index('const auto append_simulation_lab_synthetic_model=')
end=text.index('constexpr std::array<uint32_t,8> kRunGhostRgb',start)
block=text[start:end]
assert 'simulation_lab_damage_emitter_for_model(' in block
assert 'build_damage_effect_schedule_for_attachment_keys' in block
assert 'lookup.key_class().value()!=3U' in block
assert 'append_effect_visual(*definition' in block
assert 'entity.construction_complete' in block
# Selected-run mobile actors use this separate renderer. It must not gate all
# gun damage effects on the root actor's health summary.
transient=text[text.index('const auto append_runtime_transient_model='):text.index('if(simulation_mode_){',text.index('const auto append_runtime_transient_model='))]
assert 'simulation_lab_damage_emitter_for_model(' in transient
assert 'lab_runtime!=nullptr ||' in transient
assert 'if(lab_runtime){' in transient
start=text.index('if(animate_timed_effects_&&!damage_effect_controllers_.empty())')
end=text.index('const auto& placement=placements[source.placement_index];',start)
block=text[start:end]
assert 'simulation_lab_damage_emitter_for_model(' in block
assert 'owner->second->runtime_model_hierarchy' in block
assert 'lab_damage_run' in block
assert 'lab_live_damage_authorized=true' in block
assert 'owner->second->runtime_removed)continue;' in block, 'released LEV owner fell back to preview smoke'
assert 'if(!entity.synthetic)' in block, 'removed LEV owners must remain discoverable'
assert 'if(!lab_live_damage_authorized &&' in text[start:start+5500]
print('V711_LAB_DAMAGE_EMITTER_SOURCE PASS')
