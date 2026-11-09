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

    sim_h = (root / 'include/thandor/simulation.hpp').read_text(encoding='utf-8')
    app_h = (root / 'src/editor/editor_app.hpp').read_text(encoding='utf-8')
    app = (root / 'src/editor/editor_app.cpp').read_text(encoding='utf-8')

    require('impact_target_runtime_kind' in sim_h and 'impact_target_runtime_serial' in sim_h,
            'projectile state does not preserve a runtime-created model hit identity')
    require(app_h.count('int32_t maximum_integrity{};') >= 2 and app_h.count('int32_t integrity{};') >= 2,
            'class-13/class-21 transient entities do not own runtime integrity')
    require('entity.maximum_integrity=std::max(0,produced_preset->root_maximum_integrity);' in app,
            'class-13 produced runtime integrity is not initialized from the recovered ARM/MDL maximum')
    require('child.maximum_integrity=std::max(0,child_preset->root_maximum_integrity);' in app,
            'class-21 linked-child runtime integrity is not initialized from the recovered ARM/MDL maximum')
    require('projectile.impact_target_runtime_kind=model->transient->kind' in app,
            'non-direct shared owner-list model collision drops transient hit identity')
    require('projectile.impact_target_runtime_kind=best.transient->kind' in app,
            'mode-0 shared owner-list model collision drops transient hit identity')
    require('simulation_produced_entities_.find(\n                                   projectile.impact_target_runtime_serial)' in app,
            'impact transition does not resolve the actual class-13 owner hit')
    require('simulation_linked_child_entities_.find(\n                                   projectile.impact_target_runtime_serial)' in app,
            'impact transition does not resolve the actual class-21 owner hit')
    require('shot->target_impact_damage_q12[target_preset->shot_impact_class_index]' in app,
            'transient impact damage does not use the recovered SHT target-class table')
    require('entity.maximum_integrity>0&&entity.integrity<=0' in app,
            'terminal runtime-created units are not removed from live collision/target motion paths')
    require('build_damage_effect_schedule_for_attachment_keys' in app and
            'int32_t maximum_integrity,int32_t integrity' in app,
            'transient runtime damage does not reach the visible MDL threshold-effect path')
    require('if(entity.maximum_integrity<=0||entity.integrity>0)' in app,
            'terminal class-13 runtime owners still contribute faction fog visibility')
    require('child.visible&&(child.maximum_integrity<=0||child.integrity>0)' in app,
            'terminal class-21 runtime owners still contribute faction fog visibility')

    print('V556 transient runtime damage source contract PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
