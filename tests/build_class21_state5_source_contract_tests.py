#!/usr/bin/env python3
from pathlib import Path
import argparse

ap=argparse.ArgumentParser(); ap.add_argument('--source', default='.'); args=ap.parse_args()
root=Path(args.source)
app=(root/'src/editor/editor_app.cpp').read_text()
sim=(root/'src/core/simulation.cpp').read_text()
hpp=(root/'src/editor/editor_app.hpp').read_text()
checks={
    'event preserving state5 tick':'simulation_class21_tick_mission_pass_events(' in app and
        'mapped_terrain_sound_attempts' in app and 'model_point_ordinals' in app,
    'state5 source damage metrics':'hierarchy_damage_metrics(source_hierarchy)' in app,
    'candidate world scan':'choose_state5_candidate' in app and 'simulation_class21_candidate_score_full_integrity' in app,
    'candidate lock radius':'simulation_class21_candidate_within_projectile_lock' in app,
    'candidate conditional line of fire':'simulation_line_of_fire_clear(source.producer_placement_index' in app and 'simulation_class21_apply_candidate_line_of_fire_result' in app,
    'candidate source mdl18 policy':'root->simulation.runtime_field_18_raw!=0' in app and 'runtime_field_18_raw = read_i32' in (root/'src/core/model.cpp').read_text(),
    'attachment ordinal pose':'state5_emitter_pose' in app and 'const uint8_t ordinal=mission_events.model_point_ordinals[emission]' in app,
    'retail fallback z':'target.z-=thandor::q12_to_float(0x2000)' in app,
    'projectile runtime not effect fake':'simulation_create_volley(' in app and 'synthetic_weapon.shot_definition_id=root->simulation.shot_definition_id' in app,
    'mapped sound nearest cell':'retail_nearest_world_to_cell' in app,
    'mapped sound gate':'simulation_class21_mapped_terrain_sound_gate' in app,
    'mapped sound global playback':'play_runtime_global_sound(child_preset->runtime_class_effect_sound_asset_id_26c)' in app,
    'mapped sound cooldown reset':'simulation_relation_transition_ticks_[active_faction]=0U' in app,
    'neutral mdl 26c alias':'runtime_class_effect_sound_asset_id_26c' in app,
    'core seven ordinal schedule':'constexpr std::array<uint8_t,7> kOrdinals{{7U,6U,5U,4U,3U,2U,1U}}' in sim,
    'core decrement before event':'--child.mission_emission_counter;' in sim and '--child.mission_countdown;' in sim,
    'core sound gate bit10':'(active_faction_occupancy_byte&0x10U)!=0U' in sim and 'relation_transition_tick>0x10U' in sim,
    'relation tick state':'simulation_relation_transition_ticks_' in hpp,
}
failed=[name for name,ok in checks.items() if not ok]
if failed: raise SystemExit('V546_CLASS21_STATE5_SOURCE_CONTRACT FAIL: '+', '.join(failed))
print('V546_CLASS21_STATE5_SOURCE_CONTRACT PASS')
