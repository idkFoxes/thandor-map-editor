#!/usr/bin/env python3
"""G373 source regression for simulation/static composition and texture residency."""
from __future__ import annotations

import argparse
import re
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    source = args.source.resolve()
    app = (source / "src/editor/editor_app.cpp").read_text(encoding="utf-8")
    header = (source / "src/editor/editor_app.hpp").read_text(encoding="utf-8")
    vert = (source / "shaders/model.vert").read_text(encoding="utf-8")
    frag = (source / "shaders/model.frag").read_text(encoding="utf-8")
    policy = (source / "include/thandor/simulation_render_policy.hpp").read_text(encoding="utf-8")

    require("animated_unit_placements_" in app + header,
            "generic authored unit-subnode animation list is missing")
    require("if(animated_unit) animated_unit_placements_.push_back(placement_index);" in app,
            "authored unit animation is not restricted to the recovered ARM callback fields")
    require("simulation_actor_placements_" in app + header,
            "live simulation replacement list is missing")
    require("preset->has_model_subnode_animation," in app and
            "authored_animated_building()" in app,
            "authored building animation classification is missing")
    require("(preset->has_model_subnode_animation||!preset->simulation_weapon_profiles.empty())" not in app,
            "armed buildings are again classified as authored animated buildings")
    require("preset->simulation_motion_profile.movable()||!preset->simulation_weapon_profiles.empty()" not in app,
            "all movable/armed units are again routed through the dynamic renderer")

    require("simulation_actor_states_.contains(placement_index)" in app,
            "static simulation replacement is not gated by live actor state")
    require("simulation_static_replacement_dirty_" in app + header,
            "simulation replacement membership does not request a static rebuild")
    require("if(created)simulation_static_replacement_dirty_=true;" in app,
            "new simulation actors do not mark the static scene dirty")
    require(app.count("reset_simulation(false);simulation_static_replacement_dirty_=false;") >= 3,
            "map load/new-map paths do not clear transient simulation state")

    # Shot/effect texture residency must be bounded by active actors and still-live projectiles.
    active_preload = re.search(
        r"if\(simulation_replacement&&simulation_actor_states_\.contains\(placement_index\)\)\{\s*"
        r"for\(const auto& weapon:placement_weapon_profiles\(\*preset,placement_index\)\)\s*"
        r"preload_shot_textures\(weapon\.shot_definition_id\);\s*\}", app, re.S)
    require(active_preload is not None,
            "technology-resolved simulation shot textures are not restricted to live replacement actors")
    require("for(const auto& projectile:simulation_projectiles_)\n        preload_shot_textures(projectile.shot_definition_id);" in app,
            "live projectile textures are not retained independently")
    transient_preload = re.search(
        r"if\(preload_runtime_resources\)\{.*?"
        r"for\(const auto& weapon:catalog_\.simulation_weapon_profiles_for_hierarchy\(hierarchy\)\)\s*"
        r"preload_shot_textures\(weapon\.shot_definition_id\);\s*\}", app, re.S)
    require(transient_preload is not None,
            "live transient actor weapon closure is not bounded by the runtime-resource gate")
    require("preload_simulation_lab_preset_textures(*preset,entity.faction_index,nullptr,true);" in app,
            "spawned transient actors do not opt into their runtime weapon/effect residency")
    require(app.count("preload_shot_textures(weapon.shot_definition_id);") == 2,
            "weapon texture preload escaped the placed-actor/live-transient residency gates")

    require("out_static_animation_category = in_direct_tint_bypass >> 1u" in vert and
            "kObjectVertexFogDynamicReplacementBit" in policy,
            "model vertex shader does not preserve replacement categories including fog")
    require("building_hide_mask" in frag and
            "(building_hide_mask & 1u)" in frag and "(in_static_animation_category & 1u)" in frag and
            "(building_hide_mask & 2u)" in frag and "(in_static_animation_category & 2u)" in frag,
            "building shader no longer suppresses authored and simulated categories independently")
    require("unit_hide_mask" in frag and
            "(unit_hide_mask & 1u)" in frag and "(in_static_animation_category & 4u)" in frag and
            "(unit_hide_mask & 2u)" in frag and "(in_static_animation_category & 8u)" in frag,
            "unit shader no longer suppresses simulated and authored categories independently")
    require("(in_static_animation_category & 16u)" in frag,
            "continuous radar static replacement category is missing")

    for symbol in (
        "kObjectVertexAuthoredAnimatedBuildingBit",
        "kObjectVertexSimulatedBuildingBit",
        "kObjectVertexSimulatedUnitBit",
        "kObjectVertexAuthoredAnimatedUnitBit",
        "kObjectVertexContinuousRadarBit",
        "authored_animated_unit",
        "has_live_simulation_state",
        "dynamic_replacement_enabled",
    ):
        require(symbol in policy, f"simulation render policy is missing {symbol}")

    require("if(!simulated_node&&authored_yaw_step!=0&&" in app and
            "!thandor::simulation_model_node_is_weapon_aim_axis(" in app and
            "model_runtime_child_yaw_step_turn16" in app,
            "authored recursive child animation can still overwrite a simulated weapon node")

    print("G373 live-actor-only simulation replacement and bounded texture residency PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
