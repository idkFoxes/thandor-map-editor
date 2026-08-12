#!/usr/bin/env python3
import argparse
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument('--source', required=True)
ns = ap.parse_args()
root = Path(ns.source)
app = (root / 'src/editor/editor_app.cpp').read_text()
hdr = (root / 'src/editor/editor_app.hpp').read_text()
cat = (root / 'src/core/catalog.cpp').read_text()
model = (root / 'src/core/model.cpp').read_text()
doc = (root / 'src/core/map_document.cpp').read_text()
effect = (root / 'src/core/effect.cpp').read_text()
tech = (root / 'include/thandor/technology.hpp').read_text()

required = {
    '256-bit faction mask type': 'FactionTechnologyMask256',
    'faction research editor': 'draw_faction_research_mask_editor',
    'ARM last-unlocked rule': 'executable keeps the last unlocked ARM candidate',
    'ARM slot-zero baseline guard': 'candidate.variant_index == 0U',
    'MDL first-unlocked rule': 'first unlocked MDL +0x238..+0x24C wins',
    'six MDL replacement fields': 'faction_technology_variant_model_ids',
    'damage threshold parser': 'damage_effect_health_threshold_percent',
    'damage effect ID parser': 'damage_effect_id = read_u32(data, offset + 0x254U)',
    'damage base cooldown parser': 'damage_effect_min_cooldown_ticks = read_u32(data, offset + 0x258U)',
    'damage random cooldown parser': 'damage_effect_random_cooldown_ticks = read_u32(data, offset + 0x25CU)',
    'class-3 damage attachment lookup': 'lookup.key_class().value()==3U',
    'strict damage threshold': 'tag.preview_integrity_percent)>=source.health_threshold_percent',
    'damage root fallback': 'origin=runtime_transform.translation',
    'conditional cooldown RNG': 'if (timing.random_cooldown_ticks != 0U)',
    'project V3': 'THANDOR_MAP_EDITOR_PROJECT_V3',
    'project faction mask persistence': 'faction_technology_mask.',
    'project integrity persistence': 'placement_integrity_percent.',
}
combined = '\n'.join((app, hdr, cat, model, doc, effect, tech))
for name, token in required.items():
    if token not in combined:
        raise SystemExit(f'missing {name}: {token}')

for rejected in [
    'ArmyTechnologyLevelPreview', 'technology_level_previews', 'technology_level_index',
    'placement_technology_level', 'PendingTechnologyPlacement', 'popup_screen_position',
    'resolve_arm_linked_model_hierarchy', 'THANDOR_MAP_EDITOR_PROJECT_V2',
    'THANDOR_MAP_EDITOR_PROJECT_V1'
]:
    if rejected in combined:
        raise SystemExit(f'superseded/legacy technology path survived: {rejected}')

# The source must not consume a random number merely to compute a fixed cooldown. This checks the
# implementation shape in addition to the C++ behavioral test.
needle = 'if (timing.random_cooldown_ticks != 0U)\n            random_cooldown = resource_random_next_primary(state) % timing.random_cooldown_ticks;'
if needle not in effect:
    raise SystemExit('damage cooldown RNG is not conditional on MDL +0x25C')

print('G400_EDITOR_SOURCE_CONTRACT PASS')
