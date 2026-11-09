#!/usr/bin/env python3
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--source', required=True)
args = parser.parse_args()
root = Path(args.source)
app = (root / 'src/editor/editor_app.cpp').read_text(encoding='utf-8')
core = (root / 'src/core/simulation_lab.cpp').read_text(encoding='utf-8')
model = (root / 'src/core/model.cpp').read_text(encoding='utf-8')

def req(condition, message):
    if not condition:
        raise SystemExit(message)

for token in (
    'seed.runtime_class_specific_c0_raw=preset->runtime_class_specific_c0_raw;',
    'seed.runtime_class_specific_c4_raw=preset->runtime_class_specific_c4_raw;',
    'seed.runtime_class_specific_19c_raw=preset->runtime_class_specific_19c_raw;',
    'seed.runtime_class_specific_1a8_raw=preset->runtime_class_specific_1a8_raw;',
    'seed.class18_conversion_effect_lifetime_ticks=thandor::effect_model_completion_tick(*definition).value_or(0U);',
    'template_seed.runtime_class_specific_c0_raw=preset.runtime_class_specific_c0_raw;',
    'template_seed.runtime_class_specific_c4_raw=preset.runtime_class_specific_c4_raw;',
):
    req(token in app, f'missing editor conversion seed contract: {token}')

req('SimulationLabOneShotEventKind::PioneerConversionEffect' in app,
    'editor does not present the Pioneer MDL +0xC4 conversion effect event')
req('simulation_lab_suppressed_placements_.contains(placement_index)' in app and
    'entity.source_placement_index<placement_count' in app,
    'converted PlayerStart authored geometry is not suppressed by replacement lineage')
req('suppressed_placements.insert(entity.source_placement_index);' in app and
    'if(suppressed_placements!=simulation_lab_suppressed_placements_)' in app and
    'simulation_lab_suppressed_placements_=std::move(suppressed_placements);' in app and
    'simulation_static_replacement_dirty_=true;' in app,
    'conversion lineage changes do not invalidate the separate placement-suppression domain')
req('simulation_lab_resident_render_assets(run)' in app,
    'texture residency is not keyed by the finite synthetic ARM/faction asset set')

for token in (
    'visual.simulation.class_specific_19c_raw = read_i32(data, offset + 0x19CU);',
    'visual.simulation.class_specific_1a8_raw = read_i32(data, offset + 0x1A8U);',
):
    req(token in model, f'MDL raw conversion field is not parsed: {token}')

dispatch = core[core.index('void SimulationLabExperiment::dispatch_group_a_class_command'):
                core.index('void SimulationLabExperiment::advance_pioneer_conversions')]
req('(candidate.runtime_flags & 0x18U) == 0U' in dispatch and
    'candidate.runtime_class_specific_19c_raw == 0' in dispatch and
    'pioneer_conversion_overlap(owner, candidate)' in dispatch,
    'Group-A dispatch no longer uses the recovered candidate eligibility/extent contract')
req('pioneer.runtime_flags |= 0x08U;' in dispatch and
    'PioneerConversionEffect' in dispatch and
    'pioneer.runtime_class_specific_c4_raw' in dispatch,
    'Group-A dispatch no longer starts the recovered Pioneer hold/effect transition')
req('dispatch_group_a_class_command(run, placed_entity_index);' in core,
    'construction placement no longer dispatches the newly committed runtime class command')
step_run = core[core.index('void SimulationLabExperiment::step_run'):
                core.index('void SimulationLabExperiment::step(', core.index('void SimulationLabExperiment::step_run'))]
req('dispatch_group_a_class_command' not in step_run,
    'Group-A class command is incorrectly replayed from the simulation heartbeat')

advance = core[core.index('void SimulationLabExperiment::advance_pioneer_conversions'):
               core.index('void SimulationLabExperiment::advance_production_jobs')]
req('pioneer.runtime_class_specific_c0_raw' in advance and
    'old_integrity' in advance and 'root.maximum_integrity' in advance and 'old_maximum' in advance and
    'erase_simulation_lab_entity_and_repair_indices' in advance,
    'completion action 3 no longer creates/reintegrates the MDL +0xC0 replacement safely')

print('v567 pioneer conversion source contract passed')
