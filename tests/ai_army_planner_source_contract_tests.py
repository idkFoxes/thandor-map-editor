#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path


def require(value: bool, message: str) -> None:
    if not value:
        raise SystemExit(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    args = parser.parse_args()
    root = args.source.resolve()
    lab = (root / 'src/core/simulation_lab.cpp').read_text()
    header = (root / 'include/thandor/simulation_lab.hpp').read_text()
    producer = (root / 'include/thandor/ai_unit_producibility_v601.hpp').read_text()
    catalog = (root / 'src/core/catalog.cpp').read_text()
    editor = (root / 'src/editor/editor_app.cpp').read_text()

    for text in (
        'preset.ai_definition_class_value70 = read_u32(bytes, offset + 0x70U);',
        'preset.ai_definition_class_value74 = read_u32(bytes, offset + 0x74U);',
        'preset.ai_definition_class_value78 = read_u32(bytes, offset + 0x78U);',
    ):
        require(text in catalog, 'V568 raw ARM AI score offset parsing drifted: ' + text)

    require('army_asset_all_selected_model_technologies_unlocked(preset,mask)' in editor,
            'editor no longer snapshots the exact workspace11 selected-technology gate')
    require('product.ai_baseline_root_definition_value18=root->simulation.runtime_field_18_raw;' in editor,
            'editor lost the MDL +0x18 gate used by the +0x0C score normalizer')
    require('weapon.target_class_impact_damage_q12=shot->target_impact_damage_q12;' in editor,
            'editor no longer snapshots child SHT target-class damage for AI scoring')

    require('ai_v601_workspace11_candidate(' in lab and
            '(army_asset_id < 300U || army_asset_id >= 340U)' in producer,
            'workspace11 building-range helper gate or live use drifted')

    for text in (
        'request.army_asset_id>=340U&&',
        'request.army_asset_id<380U',
        'entity.army_asset_id == 300U',
        'kXeniteSpecialAsset = 330U',
        'kTritiumSpecialAsset = 332U',
    ):
        require(text in lab, 'decimal ARM identity contract drifted: ' + text)
    require('kXeniteSpecialAsset = 0x330U' not in lab and 'kTritiumSpecialAsset = 0x332U' not in lab,
            'symbolic decimal ARM names were reintroduced as hexadecimal numeric IDs')

    for text in (
        'constexpr AiArmyScoreWeightsV568 kAiArmyScoreVariantA{{0,512,512,512,512,640,512,0}',
        'constexpr AiArmyScoreWeightsV568 kAiArmyScoreVariantB{{0,128,128,128,128,128,128,128}',
        'constexpr AiArmyScoreWeightsV568 kAiArmyScoreVariantC{{0,384,384,384,384,448,384,512}',
        'if (enemy_present) variant_b_weight /= 2;',
        'plan_autonomous_army_purchases(run, field, selected_faction);',
    ):
        require(text in lab, 'V568 autonomous army planner contract drifted: ' + text)

    require('selected_faction = (run.tick >> 3U) & 7U' in lab and
            '((run.tick >> 6U) & 1U) != 0U' in lab,
            'staggered one-faction economy wheel is not represented')
    require('faction_ai_pressure_values' in header and 'faction_maximum_ai_pressure' in header,
            'persistent eight-channel faction AI pressure image is missing from SimulationLabRun')
    require('update_ai_pressure_state_v568(run);' in lab and
            lab.index('plan_autonomous_army_purchases(run, field, selected_faction);') <
            lab.index('update_ai_pressure_state_v568(run);'),
            'retail post-planning AI pressure refresh ordering is missing')
    require('((uint32_t(value)*3U+1U)>>2U)+1U' in lab and
            'entity.model_primary_occupancy_mask&(1U<<(2U*target_faction+1U))' in lab and
            '0x100U' in lab and 'entity.shot_impact_class_index' in lab,
            'AI pressure decay/channel contribution reconstruction is missing')
    require('weapon.target_class_impact_damage_q12[pressure_index]' in lab and
            'run.faction_ai_pressure_values[faction]' in lab and
            'run.faction_maximum_ai_pressure[faction]' in lab,
            'army scorer is not consuming the rebuilt faction pressure image')

    print('V568_ARMY_PLANNER_SOURCE_CONTRACT PASS')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
