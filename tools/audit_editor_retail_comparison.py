#!/usr/bin/env python3
"""Compare a V751 Simulation Lab forensic log with a validated retail THDTGR1 audit.

The comparison deliberately separates three evidence classes:

* independently valid retail facts (TICK, FACT, validated AIPH, root pose/health/research),
* editor/Simulation Lab diagnostics, and
* non-comparable rows caused by different scenario inputs or the known intrusive V1 AIPH hook.

It never uses recorder-V1 ACTR faction/ARM labels as retail truth.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import json
from pathlib import Path
import re
from typing import Any


def parse_scalar(text: str) -> Any:
    text = text.strip().strip('"')
    try:
        return int(text, 0)
    except ValueError:
        try:
            return float(text)
        except ValueError:
            return text


def parse_kv_line(line: str) -> dict[str, Any]:
    result: dict[str, Any] = {}
    # The forensic log uses whitespace-separated key=value tokens. Values that contain
    # spaces are quoted only in the editor log; the fields used here never contain spaces.
    for token in line.rstrip().split()[1:]:
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        result[key] = parse_scalar(value)
    return result


def parse_simlab(path: Path) -> dict[str, Any]:
    start: dict[str, Any] = {}
    runs: dict[int, dict[int, dict[str, Any]]] = defaultdict(dict)
    faction_rows: dict[int, dict[int, dict[int, dict[str, Any]]]] = defaultdict(lambda: defaultdict(dict))
    economy_rows: dict[int, dict[int, dict[int, dict[str, Any]]]] = defaultdict(lambda: defaultdict(dict))
    entity_rows = 0
    removed_render_rows = 0
    submitted_render_rows = 0
    shadow_samples: list[dict[str, Any]] = []
    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if line.startswith("START "):
                start = parse_kv_line(line)
            elif line.startswith("RUN "):
                row = parse_kv_line(line)
                runs[int(row.get("index", 0))][int(row.get("tick", 0))] = row
            elif line.startswith("FACTION "):
                row = parse_kv_line(line)
                faction_rows[int(row.get("run", 0))][int(row.get("tick", 0))][int(row.get("f", 0))] = row
            elif line.startswith("ECONOMY "):
                row = parse_kv_line(line)
                economy_rows[int(row.get("run", 0))][int(row.get("tick", 0))][int(row.get("faction", 0))] = row
            elif line.startswith("ENTITY "):
                entity_rows += 1
            elif line.startswith("CPU_RENDER "):
                if " result=removed " in f" {line}":
                    removed_render_rows += 1
                if " result=submitted " in f" {line}":
                    submitted_render_rows += 1
            elif line.startswith("PERF "):
                row = parse_kv_line(line)
                if "shadow_templates" in row or "shadow_vertices" in row:
                    shadow_samples.append({k: row.get(k) for k in (
                        "sample", "shadow_templates", "shadow_vertices", "projected_shadows",
                        "static_shadow_build_ms", "gui_shadow_ms")})
    return {
        "file": path.name,
        "start": start,
        "runs": runs,
        "faction_rows": faction_rows,
        "economy_rows": economy_rows,
        "entity_rows": entity_rows,
        "removed_render_rows": removed_render_rows,
        "submitted_render_rows": submitted_render_rows,
        "shadow_samples": shadow_samples,
    }


def parse_editor_log(path: Path | None) -> dict[str, Any] | None:
    if path is None:
        return None
    first: dict[str, Any] | None = None
    last: dict[str, Any] | None = None
    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            if not line.startswith("EDITOR_PERF "):
                continue
            row = parse_kv_line(line)
            if first is None:
                first = row
            last = row
    if first is None:
        return {"file": path.name, "error": "no EDITOR_PERF rows"}
    keys = ("map", "tick", "placements", "generated_shadow_cache_entries",
            "shadow_templates", "shadow_vertices", "projected_shadows",
            "object_rebuild_calls", "dynamic_revision")
    return {
        "file": path.name,
        "first": {key: first.get(key) for key in keys},
        "last": {key: last.get(key) for key in keys},
    }


def retail_sample(samples: dict[str, Any], tick: int) -> dict[str, Any] | None:
    for sample in samples.get("sampled_frames", []):
        if sample.get("tick_words", [None])[0] == tick:
            return sample
    return None


def simlab_tick_rows(simlab: dict[str, Any], run: int, requested_tick: int) -> tuple[int | None, dict[int, dict[str, Any]]]:
    rows = simlab["economy_rows"].get(run, {})
    if not rows:
        return None, {}
    nearest = min(rows, key=lambda tick: abs(tick - requested_tick))
    return nearest, rows[nearest]


def sim_tech(row: dict[str, Any]) -> list[int]:
    return [int(row.get(f"tech{i}", 0)) for i in range(8)]


def compare_state(sim_rows: dict[int, dict[str, Any]], retail: dict[str, Any] | None,
                  active_factions: set[int]) -> dict[str, Any]:
    if retail is None:
        return {"error": "retail sample unavailable"}
    fields = {
        "xenite": "xenite_q4",
        "xenite_cap": "xenite_cap_q4",
        "tritium": "tritium_q4",
        "tritium_cap": "tritium_cap_q4",
    }
    factions: dict[str, Any] = {}
    exact_fields = 0
    compared_fields = 0
    exact_tech = 0
    compared_tech = 0
    for faction_s, retail_row in retail.get("factions", {}).items():
        faction = int(faction_s)
        if faction not in active_factions:
            continue
        sim_row = sim_rows.get(faction)
        if sim_row is None:
            factions[faction_s] = {"simlab_missing": True}
            continue
        row_result: dict[str, Any] = {}
        for sim_key, retail_key in fields.items():
            sim_value = int(sim_row.get(sim_key, 0))
            retail_value = int(retail_row.get(retail_key, 0))
            compared_fields += 1
            if sim_value == retail_value:
                exact_fields += 1
            row_result[retail_key] = {
                "simlab": sim_value,
                "retail": retail_value,
                "delta": sim_value - retail_value,
                "equal": sim_value == retail_value,
            }
        sim_mask = sim_tech(sim_row)
        retail_mask = [int(value) for value in retail_row.get("technology_mask", [0] * 8)]
        compared_tech += 1
        if sim_mask == retail_mask:
            exact_tech += 1
        row_result["technology_mask"] = {
            "simlab": sim_mask,
            "retail": retail_mask,
            "equal": sim_mask == retail_mask,
        }
        factions[faction_s] = row_result
    return {
        "factions": factions,
        "resource_field_matches": exact_fields,
        "resource_fields_compared": compared_fields,
        "technology_mask_matches": exact_tech,
        "technology_masks_compared": compared_tech,
    }


def markdown(report: dict[str, Any]) -> str:
    s = report["simlab"]
    r = report["retail"]
    lines = [
        "# Thandor V751 editor/retail comparison",
        "",
        "## Evidence boundary",
        "",
        "- Retail V1 evidence admitted: TICK, FACT, validated AIPH metadata, UCMD, root pose/yaw/health/model flags/research.",
        "- Recorder-V1 ACTR faction, ARM identity, movement and target fields are excluded.",
        "- The supplied recorder hook altered later AI planner arguments; AIPH describes that instrumented history and is not an ordinary unhooked retail cadence oracle.",
        "",
        "## Input profile",
        "",
        "| Item | Simulation Lab log | Retail recording | Comparable? |",
        "|---|---:|---:|---|",
        f"| Map | `{s['start'].get('map')}` | `{r.get('map_name')}` | {report['map_assessment']} |",
        f"| Active factions | {s['start'].get('active_factions')} | {report['retail_active_factions']} | {'yes' if report['active_faction_count_equal'] else 'no'} |",
        f"| All active factions autonomous | {s['start'].get('simulate_all_active_factions_as_ai')} | false (`humanFactionMask={report['human_faction_mask']}`) | no |",
        f"| Game speed Q8 | {s['start'].get('game_speed_q8')} | {report['retail_game_speed_q8']} | {'yes' if report['game_speed_equal'] else 'no'} |",
        "",
        "The old forensic run is therefore not a matched timing replay. It remains useful for source/runtime invariants, rendering ownership, destruction and combat diagnostics.",
        "",
        "## Initial state comparison",
        "",
        f"Retail sample tick: {report['initial_comparison']['retail_tick']}; Simulation Lab tick: {report['initial_comparison']['simlab_tick']}.",
        f"Resource/cap fields equal: **{report['initial_comparison']['comparison']['resource_field_matches']}/{report['initial_comparison']['comparison']['resource_fields_compared']}**.",
        f"Technology masks equal: **{report['initial_comparison']['comparison']['technology_mask_matches']}/{report['initial_comparison']['comparison']['technology_masks_compared']}**.",
        "",
        "## Later checkpoint",
        "",
        f"Retail tick 1000 was compared with the nearest available Simulation Lab sample at tick {report['checkpoint_comparison']['simlab_tick']}.",
        "This row is diagnostic only because the human mask, game speed and recorder-hook execution history differ.",
        f"Resource/cap fields equal: **{report['checkpoint_comparison']['comparison']['resource_field_matches']}/{report['checkpoint_comparison']['comparison']['resource_fields_compared']}**; technology masks equal: **{report['checkpoint_comparison']['comparison']['technology_mask_matches']}/{report['checkpoint_comparison']['comparison']['technology_masks_compared']}**.",
        "",
        "## Recorder findings",
        "",
        f"- Complete frames: **{r.get('complete_frames')}**.",
        f"- Human faction mask: `{report['human_faction_mask']}` (faction 1 / Ares is human).",
        f"- Validated AI factions seen in AIPH: {', '.join(report['validated_ai_factions']) or 'none'}.",
        f"- Invalid AIPH rows: `{r.get('invalid_aiph', {})}`.",
        f"- Legacy hook corruption detected: **{r.get('legacy_ai_hook_corruption_detected')}**.",
        f"- Ordinary unhooked AI cadence oracle: **{r.get('ordinary_unhooked_ai_cadence_oracle')}**.",
        "",
        "## Required matched rerun",
        "",
        f"- Map/scenario corresponding to `{r.get('map_name')}`.",
        "- Disable **Simulate every active faction as AI**.",
        f"- Human faction mask: `{report['human_faction_mask']}`.",
        f"- Game speed Q8: `{report['retail_game_speed_q8']}`.",
        "- Compare factions 2–7 only for autonomous planner timing.",
        "- Continue to use FACT, commands, research, root pose and health as the primary retail oracle.",
    ]
    if report.get("editor_log"):
        lines += ["", "## Editor presentation log", "", f"```json\n{json.dumps(report['editor_log'], indent=2)}\n```"]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--simlab-log", type=Path, required=True)
    parser.add_argument("--retail-audit", type=Path, required=True)
    parser.add_argument("--retail-samples", type=Path, required=True)
    parser.add_argument("--editor-log", type=Path)
    parser.add_argument("--expected-simlab-map")
    parser.add_argument("--json", type=Path, required=True)
    parser.add_argument("--markdown", type=Path, required=True)
    args = parser.parse_args()

    simlab = parse_simlab(args.simlab_log)
    retail = json.loads(args.retail_audit.read_text(encoding="utf-8"))
    samples = json.loads(args.retail_samples.read_text(encoding="utf-8"))
    first_sample = retail_sample(samples, 2)
    checkpoint = retail_sample(samples, 1000)
    sim_initial_tick, sim_initial_rows = simlab_tick_rows(simlab, 0, 0)
    sim_checkpoint_tick, sim_checkpoint_rows = simlab_tick_rows(simlab, 0, 1000)

    human_mask_values = retail.get("human_faction_masks", {})
    human_mask = int(next(iter(human_mask_values))) if human_mask_values else 0
    speed_values = retail.get("game_speed_q8_values", {})
    game_speed = int(next(iter(speed_values))) if speed_values else 0
    active_factions = {int(faction) for faction, row in retail.get("first_faction_state", {}).items()
                       if int(faction) != 0 and int(row.get("lifecycle", 0)) == 1}
    retail_active = len(active_factions)
    expected = args.expected_simlab_map or ""
    actual_map = str(simlab["start"].get("map", ""))
    if expected and actual_map == expected:
        map_assessment = "candidate map alias matches"
    elif actual_map.lower() in str(retail.get("map_name", "")).lower():
        map_assessment = "name matches"
    else:
        map_assessment = "not established"

    validated_ai = [faction for faction, data in retail.get("ai_cadence", {}).items()
                    if data.get("events", 0) > 0 and not data.get("excluded_human_events", 0)]
    report = {
        "format": "Thandor-V751-editor-retail-comparison",
        "simlab": simlab,
        "retail": retail,
        "editor_log": parse_editor_log(args.editor_log),
        "map_assessment": map_assessment,
        "human_faction_mask": human_mask,
        "retail_game_speed_q8": game_speed,
        "retail_active_factions": retail_active,
        "active_faction_count_equal": int(simlab["start"].get("active_factions", 0)) == retail_active,
        "game_speed_equal": int(simlab["start"].get("game_speed_q8", 0)) == game_speed,
        "validated_ai_factions": validated_ai,
        "initial_comparison": {
            "retail_tick": 2,
            "simlab_tick": sim_initial_tick,
            "comparison": compare_state(sim_initial_rows, first_sample, active_factions),
        },
        "checkpoint_comparison": {
            "retail_tick": 1000,
            "simlab_tick": sim_checkpoint_tick,
            "comparison": compare_state(sim_checkpoint_rows, checkpoint, active_factions),
            "comparable_for_timing": False,
            "reasons": [
                "Simulation Lab log used all active factions as AI",
                "Simulation Lab log used a different Q8 game speed",
                "retail AIPH hook changed later faction dispatches",
            ],
        },
    }
    args.json.parent.mkdir(parents=True, exist_ok=True)
    args.markdown.parent.mkdir(parents=True, exist_ok=True)
    args.json.write_text(json.dumps(report, indent=2), encoding="utf-8")
    args.markdown.write_text(markdown(report), encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
