#!/usr/bin/env python3
"""V751 source contract for runtime faction repaint and destroyed-owner shadows."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys


def require(condition: bool, message: str, errors: list[str]) -> None:
    if not condition:
        errors.append(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    source = args.source.resolve()
    cpp = (source / "src/editor/editor_app.cpp").read_text(encoding="utf-8")
    hpp = (source / "src/editor/editor_app.hpp").read_text(encoding="utf-8")
    errors: list[str] = []

    require("simulation_runtime_factions_" in hpp,
            "runtime placement faction map is missing", errors)
    require("runtime_faction_for_placement" in hpp and
            "faction_merge_redirect[authored_faction]" in cpp,
            "runtime placement faction does not follow the selected run merge redirect", errors)
    require("selected_runtime_factions[entity.source_placement_index]" in cpp and
            "entity.faction_index" in cpp,
            "live authored entity faction is not retained from the Simulation Lab snapshot", errors)
    require("mix_render_signature(entity.faction_index)" in cpp and
            "for(const auto redirect:run.faction_merge_redirect)mix_render_signature(redirect);" in cpp,
            "faction merge does not invalidate the texture/render signature", errors)
    require("preload_simulation_lab_preset_textures(*preset,\n                runtime_faction_for_placement" in cpp,
            "authored placement texture residency still uses only the LEV faction", errors)
    require("const uint32_t runtime_faction=neutral?0U:effective_runtime_faction;" in cpp and
            "graphics_family_for_faction(effective_runtime_faction)" in cpp,
            "dynamic authored rendering still uses the original placement faction", errors)

    require("bool requires_live_owner{};" in hpp,
            "dynamic shadow templates do not identify runtime-owner dependence", errors)
    require("dynamic_shadow.requires_live_owner=simulation_replacement;" in cpp,
            "runtime replacement shadows are not tagged as owner-dependent", errors)
    require("shadow.requires_live_owner&&\n           !simulation_actor_states_.contains(shadow.placement_index)" in cpp,
            "destroyed runtime owners do not suppress their cached shadow receiver", errors)
    require("simulation_runtime_owner_revision_" in hpp and
            "show_movement_ground_marks_,simulation_runtime_owner_revision_" in cpp,
            "dynamic model/shadow cache is not invalidated when runtime ownership changes", errors)

    # The editor/static-map path must retain authored faction semantics. V751 only changes
    # presentation while a runtime owner exists; it must not rewrite LEV save identity.
    require("graphics_family_for_faction(placement.faction_index)" in cpp,
            "all authored faction rendering was replaced instead of keeping static editor semantics", errors)

    if errors:
        for error in errors:
            print(f"FAIL: {error}")
        return 1
    print("PASS: V751 runtime faction repaint and destroyed-owner shadow source contract")
    return 0


if __name__ == "__main__":
    sys.exit(main())
