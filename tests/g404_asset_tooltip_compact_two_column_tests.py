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
    start = text.index("const float tooltip_width=", text.index("if(preview_hovered)"))
    end = text.index("ImGui::EndTooltip();", start)
    tooltip = text[start:end]

    for token in [
        "std::clamp(ImGui::GetIO().DisplaySize.x*0.72F,720.0F,980.0F)",
        'ImGui::BeginTable("##asset_tooltip_columns",2',
        'ImGui::TableSetupColumn("Stats / weapons"',
        'ImGui::TableSetupColumn("Research / models"',
        "ImGui::TableSetColumnIndex(0)",
        "ImGui::TableSetColumnIndex(1)",
        "ImGui::GetContentRegionAvail().x",
        'ImGui::SeparatorText("PCK gameplay values")',
        'ImGui::SeparatorText("Weapons")',
        'ImGui::SeparatorText("Research / model variants")',
        'ImGui::SeparatorText("Visual/effect evidence")',
        "preset.fog_uncover_radius_q12",
        "preset.retail_energy_cost_q4",
        "shot->target_impact_damage_q12",
        "shot->projectile_lifetime_ticks",
        "shot->positioned_sound_gain_q15",
        "preset.technology_panel_entries",
        "preset.model_variant_groups",
        "preset.effect_pipeline_validation",
        "node.simulation.damage_effect_id",
    ]:
        require(token in tooltip, f"missing compact tooltip contract: {token}")

    for forbidden in [
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
    ]:
        require(forbidden not in tooltip, f"verbose/single-column tooltip regression returned: {forbidden}")

    require(tooltip.count("ImGui::BeginTable") == 1, "asset tooltip must use exactly one outer two-column table")
    require(tooltip.count("ImGui::EndTable") == 1, "asset tooltip table is not balanced")
    require(tooltip.count("ImGui::PushTextWrapPos") == 2 and tooltip.count("ImGui::PopTextWrapPos") == 2,
            "each tooltip column must own one bounded wrapping scope")
    print("G404_ASSET_TOOLTIP_COMPACT_TWO_COLUMN PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
