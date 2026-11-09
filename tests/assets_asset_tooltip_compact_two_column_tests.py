#!/usr/bin/env python3
from __future__ import annotations
import argparse
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    root = args.source.resolve()
    text = (root / "src/editor/editor_app.cpp").read_text(encoding="utf-8")
    start = text.index("const bool technical_details=", text.index("if(preview_hovered)"))
    end = text.index("ImGui::EndTooltip();", start)
    tooltip = text[start:end]

    for token in [
        "ImGui::GetIO().KeyShift",
        "std::clamp(ImGui::GetIO().DisplaySize.x*0.60F,ui_px(620.0F),ui_px(860.0F))",
        'ImGui::BeginTable("##asset_tooltip_columns",2',
        'ImGui::TableSetupColumn("Gameplay"',
        'ImGui::TableSetupColumn("Research / model"',
        "ImGui::TableSetColumnIndex(0)",
        "ImGui::TableSetColumnIndex(1)",
        "ImGui::GetContentRegionAvail().x",
        'ImGui::SeparatorText("Retail build card")',
        'ImGui::SeparatorText("Weapons")',
        'ImGui::SeparatorText("Research / model variants")',
        'ImGui::SeparatorText("Visual / runtime")',
        '"Hold Shift for IDs, raw fields, and provenance"',
        "displayed_weapon_count>=4U",
        "technology_preview_count=std::min<std::size_t>(preset.technology_panel_entries.size(),3U)",
        "preset.fog_uncover_radius_q12",
        "preset.retail_energy_demand_q4",
        "shot->target_impact_damage_q12",
        "shot->projectile_lifetime_ticks",
        "shot->positioned_sound_gain_q15",
        "preset.technology_panel_entries",
        "preset.model_variant_groups",
        "preset.effect_pipeline_validation",
        "node.simulation.damage_effect_id",
        "preset.resource_storage_capacity_contribution_q4",
        "preset.water_damage_threshold_q12",
        "preset.vertical_deployment_step_q12_per_tick",
    ]:
        require(token in tooltip, f"missing compact/progressive tooltip contract: {token}")

    for forbidden in [
        "std::clamp(ImGui::GetIO().DisplaySize.x*0.72F,720.0F,980.0F)",
        "PushTextWrapPos(ImGui::GetCursorPosX()+650.0F)",
        "The exact scale is preserved for the retail 0x0800 path",
        "Retail formatting: one selected MDL per ARM node",
        "SHT timing: lifetime",
        "Positioned sound: terrain mask",
        "Exact runtime composition:",
        "table index %u · branch %u · panel row %u",
        "entry.description",
        "primary EFF: %s",
        "terrain impact EFF: %s",
        "target-class impact EFF: %s",
        "Resolved model nodes %zu",
        "for(const auto& variant:group.variants)",
        "for(uint32_t slot=0U;slot<31U;++slot)",
    ]:
        require(forbidden not in tooltip, f"verbose tooltip regression returned: {forbidden}")

    require(tooltip.count("ImGui::BeginTable") == 1,
            "asset tooltip must use exactly one outer two-column table")
    require(tooltip.count("ImGui::EndTable") == 1,
            "asset tooltip table is not balanced")
    require(tooltip.count("ImGui::PushTextWrapPos") == 2 and tooltip.count("ImGui::PopTextWrapPos") == 2,
            "each asset tooltip column must own one bounded wrapping scope")
    require(tooltip.count("technical_details") >= 6,
            "technical detail disclosure is not actually gating diagnostic content")

    helper_start = text.index("void show_item_tooltip(")
    helper_end = text.index("const char* scheduled_condition_kind_name", helper_start)
    helper = text[helper_start:helper_end]
    for token in [
        "display_width-48.0F",
        "std::clamp(display_width*0.30F,260.0F,max_width)",
        "ImGui::SetNextWindowSizeConstraints",
        "ImGui::PushTextWrapPos",
        "ImGui::TextUnformatted",
    ]:
        require(token in helper, f"ordinary tooltip helper is missing bounded wrapping: {token}")
    require("ImGui::SetTooltip" not in text,
            "editor_app.cpp still has one-off unbounded SetTooltip calls")

    print("G404_ASSET_TOOLTIP_COMPACT_PROGRESSIVE PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
