#!/usr/bin/env python3
import argparse
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    root = args.source.resolve()

    app = (root / 'src/editor/editor_app.cpp').read_text(encoding='utf-8')
    app_h = (root / 'src/editor/editor_app.hpp').read_text(encoding='utf-8')
    renderer = (root / 'src/editor/terrain_renderer.cpp').read_text(encoding='utf-8')
    renderer_h = (root / 'src/editor/terrain_renderer.hpp').read_text(encoding='utf-8')
    shader = (root / 'shaders/model.frag').read_text(encoding='utf-8')
    simulation_h = (root / 'include/thandor/simulation.hpp').read_text(encoding='utf-8')

    require('animate_buildings_||fog_preview_mode_!=0||simulation_lab_active()' not in app,
            'Simulation Lab still globally forces authored building suppression')
    require('animate_units_||fog_preview_mode_!=0||simulation_lab_active()' not in app,
            'Simulation Lab still globally forces authored unit suppression')
    require('hide_simulated_buildings' in app and 'hide_simulated_units' in app,
            'editor does not publish separate live-simulation static-copy controls')
    require('hide_simulated_buildings' in renderer and 'hide_simulated_units' in renderer_h,
            'renderer signature did not preserve separate simulation suppression controls')
    require('building_hide_mask' in shader and 'unit_hide_mask' in shader,
            'model fragment shader still uses one broad authored/simulated suppression boolean')
    for fragment in (
        '(in_static_animation_category & 1u)',
        '(in_static_animation_category & 2u)',
        '(in_static_animation_category & 4u)',
        '(in_static_animation_category & 8u)',
    ):
        require(fragment in shader, f'missing split static category gate: {fragment}')

    require('impact_target_placement_index' in simulation_h,
            'projectile state does not retain the authored model actually hit')
    require('impact_damage_applied' in simulation_h,
            'projectile state has no one-shot damage transition guard')
    require('simulation_integrity_states_' in app_h,
            'ordinary simulation has no transient ModelRuntime integrity surface')
    require('shot->target_impact_damage_q12[target_preset->shot_impact_class_index]' in app,
            'target-class SHT damage is not applied at projectile impact')
    require('projectile.impact_damage_applied=true' in app,
            'impact damage is not committed as a one-shot transition')
    require('projectile.impact_target_placement_index=model->placement_index' in app,
            'shared model raycast hit identity is not propagated to damage')
    require('integrity_percent=static_cast<int32_t>(std::clamp<int64_t>(' in app,
            'damage-threshold effect preview is not driven by transient simulation integrity')
    require('building_destruction_states_[target_index]={tick,destruction_effect_id}' in app,
            'terminal building damage does not enter the recovered +0x190 destruction lifecycle')

    print('V555 visible runtime feedback source contract PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
