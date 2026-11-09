#!/usr/bin/env python3
from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=(root/'src/core/simulation_lab.cpp').read_text()
airport=(root/'src/core/simulation_lab_airport.cpp').read_text()
effects=(root/'src/core/simulation_lab_environment.cpp').read_text()
editor=(root/'src/editor/editor_app.cpp').read_text()
checks={
 'Workspace05 native first field column': 'for (std::uint32_t x=0U;' in source,
 'aircraft step-batch sound gate': 'events.mapped_terrain_sound_attempts' in airport and 'countdown_before' in airport,
 'independent effect retains full lifetime': 'run.tick-instance.spawn_tick>=effect_lifetime_ticks(e->second.definition)' in effects,
 'visual-only effect has a live owner': 'run.environmental_effects.push_back({id,run.tick,pose,0});' in effects and 'if(!effect->second.emissions.empty())' not in effects,
 'live independent effect draw': 'for(const auto& effect:run.environmental_effects)' in editor and 'selected_lab_effect_model_tint' in editor,
 'journal/live effect dedupe': 'if(has_live_owner)continue;' in editor,
 'independent effect texture residency': 'simulation_lab_resident_effect_ids_.insert(effect.effect_id)' in editor,
 'overlay separation': 'emitting_command_overlay=true;' in editor and 'EditorCommandOverlay' in editor,
 'combat ruins remain disabled': 'simulation_lab_create_terminal_ruin' not in source,
 'required algorithm include': '#include <algorithm>' in (root/'include/thandor/simulation_command_markers.hpp').read_text(),
}
for k,ok in checks.items():
    print(('PASS ' if ok else 'FAIL ')+k)
assert all(checks.values())
