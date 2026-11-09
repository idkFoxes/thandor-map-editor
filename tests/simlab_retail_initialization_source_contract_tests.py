#!/usr/bin/env python3
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
args = parser.parse_args()
root = Path(args.source)
app = (root / 'src/editor/editor_app.cpp').read_text(encoding='utf-8')
core = (root / 'src/core/simulation_lab.cpp').read_text(encoding='utf-8')
header = (root / 'include/thandor/simulation_lab.hpp').read_text(encoding='utf-8')

def req(condition, message):
    if not condition:
        raise SystemExit(message)

req('bootstrap_temporary' not in app + core + header and 'plan_bootstrap_unit_purchases' not in core,
    'non-retail synthetic starter bootstrap is still present')
req('V533 0x00531DAF..0x00531F05' in core and 'g_ArmyAssetRecordRegistry[0..0x2ff]' in core,
    'core does not pin the recovered retail level-initialization machine-code boundary')
req('(candidate.army_runtime_flags14_raw & 1U) == 0U' in core,
    'retail ARM +0x14 build-enabled filter is missing')
req('case 11U:' in core and 'case 14U:' in core and 'case 16U:' in core and
    'candidate.runtime_class_specific_c0_raw == 0' in core,
    'retail class11/class14 split/class16 registry scan is incomplete')
req('kGroupAClassCommandClasses' in core and
    '4U, 10U, 11U, 13U, 14U, 15U, 16U, 20U, 22U, 23U' in core,
    'exact V533 Group-A class-command set is not pinned')
req('entity.runtime_model_class_id == 18U' in core and 'if (!has_class18 || has_group_a) continue;' in core,
    'class-18 present / Group-A absent retail gate is missing')
req('class11->army_asset_id, class14_zero_c0->army_asset_id' in core and
    'class14_nonzero_c0->army_asset_id, class16->army_asset_id' in core and
    'class14_nonzero_c0 == nullptr || class16 == nullptr' in core,
    'four-entry retail primary-queue order/catalog invariant is not explicit')
req('product.runtime_class_specific_c0_raw=preset.runtime_class_specific_c0_raw;' in app,
    'editor does not pass root MDL +0xC0 into the worker catalog snapshot')
# Runtime ownership follows MODEL class/hierarchy, including class 18,
# independently of the editor's browser category.
owner_gate = (root / 'include/thandor/simulation_lab_catalog.hpp').read_text()
req('simulation_lab_faction_placement_has_runtime_owner(*preset,placement.faction_index)' in app and
    'preset.runtime_model_class_id<24U' in owner_gate and
    '(!preset.resolved_model_hierarchy.empty()||!preset.model_hierarchy.empty())' in owner_gate and
    'PlacementVisualKind' not in owner_gate,
    'editor seed handoff lost the shared native runtime-owner gate')
req('intentionally does not synthesize starter buildings' in app and
    'retail startup requires at least one faction runtime' in app and
    'if(seeds.empty())' in app and 'faction_economy,production_assets,technology_assets,faction_technology_masks,' in app,
    'editor seed boundary or empty-faction startup validation is missing')
req('retail_initial_primary_queue_faction_mask' in header and
    'retail_initial_primary_queue_factions' in header and
    'retail_initial_primary_queue_assets' in header,
    'retail initialization state/metrics are not visible in immutable Lab snapshots')
print('v564 simulation lab retail initialization source contract passed')
