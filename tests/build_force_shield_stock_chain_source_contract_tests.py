#!/usr/bin/env python3
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
args = parser.parse_args()
root = Path(args.source)

app = (root / 'src/editor/editor_app.cpp').read_text(encoding='utf-8')
worker = (root / 'src/core/simulation_lab.cpp').read_text(encoding='utf-8')
policy = (root / 'include/thandor/simulation_render_policy.hpp').read_text(encoding='utf-8')
effect = (root / 'src/core/effect.cpp').read_text(encoding='utf-8')
catalog = (root / 'src/core/catalog.cpp').read_text(encoding='utf-8')

def req(condition, message):
    if not condition:
        raise SystemExit(message)

# Check that EFF definitions decode and validate their sprite references.
req('constexpr std::size_t kDefinitionSize = 0xC0;' in effect and
    'constexpr std::size_t kPathOffset = 0x7C;' in effect and
    'record.definition_id = read_u32(data, offset + 0x08);' in effect and
    'record.sprite_path = read_resource_path(data, offset + kPathOffset);' in effect and
    'if (!path.empty() && !path.ends_with(".spr")) path += ".spr";' in effect,
    'generic EFF id/path/.spr decode contract disappeared')
req('load_effects(EffectAsset::parse(catalog.load(base.ends_with(".eff") ? base : base + ".eff")))' in catalog and
    'catalog.effect_definitions_.try_emplace(definition.definition_id, definition);' in catalog and
    'validate_sprite(effect.sprite_path, "EFF " + std::to_string(effect_id));' in catalog,
    'generic EFF registry/nested-SPR validation contract disappeared')

# The generic controller owns stationary class-4/class-16 timed effects. Classes 2/3 are the
# intentional exception because their pulses are historical world-space movement marks.
req('if(node.runtime_class_id==2U||node.runtime_class_id==3U)continue;' in app,
    'generic timed-effect controller exclusion no longer matches the movement-history exception')
req('pending.land_effect_id=node.simulation.timed_effect_id;' in app and
    'pending.timing={std::max(1U,node.simulation.timed_effect_interval_ticks)' in app,
    'editor no longer consumes the stock MDL timed-effect id/cadence generically')
req('timed_effect_preview_enabled(' in app,
    'editor timed-effect controller is no longer routed through the recovered preview gate')
req('Special Research Lab' in policy and 'owner_has_authored_model_animation' in policy,
    'static class-4 shield policy lost its authored-animation independence contract')

# Do not duplicate stationary class-4/class-16 periodic effects in the Simulation Lab worker.
# Its MovementGroundEffect transport is deliberately confined to classes 2/3 root-motion pulses.
movement = worker[worker.index('const bool root_changed = entity.last_translated || entity.last_turned;'):
                  worker.index('if (entity.runtime_model_class_id == 17U)')]
req('entity.runtime_model_class_id == 2U || entity.runtime_model_class_id == 3U' in movement,
    'worker movement-effect transport was widened beyond the recovered 2/3 history path')
req('entity.runtime_model_class_id == 4U' not in movement and
    'entity.runtime_model_class_id == 16U' not in movement,
    'worker duplicates stationary timed-effect controllers for class 4/16')

# Production code must remain asset-driven rather than hard-coding the stock research-lab IDs.
for path in (root / 'src/editor/editor_app.cpp', root / 'src/core/simulation_lab.cpp'):
    text = path.read_text(encoding='utf-8')
    req('EFF 153' not in text and 'effect_id==153' not in text and 'effect_id == 153' not in text,
        f'hard-coded stock shield EFF leaked into production code: {path}')

print('v567 force shield stock chain source contract passed')
