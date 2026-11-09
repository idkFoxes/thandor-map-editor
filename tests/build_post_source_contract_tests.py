#!/usr/bin/env python3
import argparse
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument('--source', required=True)
ns = ap.parse_args()
root = Path(ns.source)

cmake = (root / 'CMakeLists.txt').read_text()
core = (root / 'src/core/ai_knowledge.cpp').read_text()
ai_header = (root / 'include/thandor/ai_knowledge.hpp').read_text()
panel = (root / 'src/editor/ai_knowledge_panel.cpp').read_text()
app = (root / 'src/editor/editor_app.cpp').read_text()
app_header = (root / 'src/editor/editor_app.hpp').read_text()
effect_header = (root / 'include/thandor/effect.hpp').read_text()
effect = (root / 'src/core/effect.cpp').read_text()
model_header = (root / 'include/thandor/model.hpp').read_text()
binary_header = (root / 'include/thandor/binary.hpp').read_text()
model = (root / 'src/core/model.cpp').read_text()
simulation = (root / 'src/core/simulation.cpp').read_text()

# Every added translation unit must actually be reachable from the build graph.
for token in ['src/core/ai_knowledge.cpp', 'src/editor/ai_knowledge_panel.cpp',
              'tests/editor_post_editor_state_tests.cpp',
              'tests/core_stock_post_state_census.cpp',
              'tests/build_post_source_contract_tests.py']:
    if cmake.count(token) != 1:
        raise SystemExit(f'build graph does not contain exactly one intended reference to {token}')

# V417 is a semantic view over the unchanged serialized 128-dword image.
if 'kParameterCount = 128U' not in ai_header or 'std::array<std::uint32_t, kParameterCount>' not in ai_header:
    raise SystemExit('KI.DAT serialized 128-dword shape changed')
if ('constexpr std::array<AiKnowledgeParameterSemantic, 43>' not in core and
        'constexpr std::array<AiKnowledgeParameterSemantic, 45>' not in core and
        'constexpr std::array<AiKnowledgeParameterSemantic, 62>' not in core):
    raise SystemExit('V417 43-entry KI semantic frontier missing from current additive dictionary')
for semantic in [
    'AI_KNOWLEDGE_RESOURCE_136_DEFICIT_SCORE_NUMERATOR',
    'AI_KNOWLEDGE_PLACEMENT_CLEARANCE_PADDING_Q12',
    'AI_KNOWLEDGE_SPECIAL_CLASS12_WORKSPACE08_FIELD0C_COEFFICIENT',
    'AI_KNOWLEDGE_STRATEGIC_CLASS_143_PRESSURE2_COEFFICIENT',
]:
    if semantic not in core:
        raise SystemExit(f'missing V417 semantic {semantic}')
if 'Show 23 proven KI parameters' in panel:
    raise SystemExit('editor KI panel regressed behind the complete V417 frontier')
if 'Show 43 proven KI parameters' not in panel and 'Show 45 proven KI parameters' not in panel and 'Show 62 sourced KI parameters' not in panel:
    raise SystemExit('editor KI panel does not preserve the V417 frontier or a proven additive successor')
if 'not a universal LEV' not in panel or 'never blocked by this diagnostic overlay' not in panel:
    raise SystemExit('AI diagnostic/non-authoritative placement boundary missing')

# KI.DAT must be loaded as optional diagnostics and must not gate ordinary map work.
if 'catalog_.load("engine/ki.dat")' not in app or 'ai_knowledge_load_error_' not in app + app_header:
    raise SystemExit('editor does not load optional engine/ki.dat diagnostics')
if 'draw_ai_knowledge_panel(ai_knowledge_, ai_knowledge_load_error_, ai_knowledge_ui_)' not in app:
    raise SystemExit('Players & AI tab does not expose KI diagnostics')
for forbidden in ['placement_valid = ai_knowledge_', 'return ai_knowledge_->placement_clearance',
                  'throw std::runtime_error("AI placement']:
    if forbidden in app:
        raise SystemExit(f'KI diagnostics incorrectly gate manual placement: {forbidden}')

# Post-V414/V415 shot semantics replace the old misleading Q12 range-scale names.
for old in ['range_scale_d0_q12', 'range_scale_270_q12', 'lifetimeOrRangeScaleD0Q12', 'rangeScale270Q12']:
    if old in effect_header + effect + simulation:
        raise SystemExit(f'superseded SHT semantic survived: {old}')
for token in ['projectile_lifetime_ticks', 'trajectory_ramp_duration_ticks',
              'fixed_range_transition_age_threshold_ticks', 'shading_release_transition_ticks',
              'terrain_impact_effect_owner_slots', 'terrain_material_indices',
              'positioned_sound_gain_q15', 'positioned_sound_maximum_distance_q12']:
    if token not in effect_header + effect:
        raise SystemExit(f'missing corrected SHT field {token}')
if 'simulation_update_projectile_live_target(' not in app or 'live_target_placement_index' not in app:
    raise SystemExit('in-flight mode-2 entity-target refresh is not integrated')
if 'fixed_range_transition_age(' not in simulation or 'fixed_range_projectile_position(' not in simulation:
    raise SystemExit('readable mode-2 transition helpers missing')
if 'shot->shading_release_transition_ticks' not in app:
    raise SystemExit('projectile resource-shading release does not use SHT +0x264')

# Semantic-member rename closure must cover both object and pointer callsites in the GUI TU.
# G401 previously checked the declaration/parser but missed one stale shot->lifetime_ticks use.
if 'shot->lifetime_ticks' in app:
    raise SystemExit('stale ShotDefinitionRecord pointer callsite survived: shot->lifetime_ticks')
if 'flight.runtime_value_0c=std::max(1U,shot->projectile_lifetime_ticks);' not in app:
    raise SystemExit('projectile flight lifetime does not use the renamed SHT +0xD0 member')

# SPR +0xE0 remains parsed exactly; the compact tooltip may summarize evidence but must expose the decoded scale.
if 'lighting_scale_q12()' not in model_header or 'read_i32(data, 0xE0U)' not in model:
    raise SystemExit('SPR +0xE0 lighting scale parser missing')
if 'SPR light %.3f · scaled tris %zu/%zu' not in app:
    raise SystemExit('compact SPR lighting-scale diagnostic missing')
if 'q15_gain_to_float' not in binary_header or 'Sound: mask %d · gain %.3fx · max %.3f' not in app:
    raise SystemExit('V417 Q15 audio-gain diagnostic conversion missing')

# Readability regression: one shared cache loader replaces repeated local lambdas.
if 'EditorApp::cached_sprite(' not in app or 'cached_sprite(const std::string& path)' not in app_header:
    raise SystemExit('shared readable sprite cache accessor missing')
if 'const auto load_sprite=[&]' in app or 'const auto load_cached_sprite=[&]' in app:
    raise SystemExit('duplicated local sprite-cache loader survived')
if 'draw_world_distance_ring' not in app:
    raise SystemExit('shared world-distance ring helper missing')

# V415 validation is visible for custom SHT diagnostics without changing impact selection.
if 'validate_shot_terrain_material_references' not in app or 'Terrain refs: %u ok · %u sentinel' not in app:
    raise SystemExit('SHT terrain-material diagnostics are not exposed')

print('G401_POST_V417_SOURCE_CONTRACT PASS')
