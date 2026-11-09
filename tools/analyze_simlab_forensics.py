#!/usr/bin/env python3
"""Summarize V584+ Simulation Lab GUI forensics; never confuse fog snapshots with history.

No dependencies beyond the standard library. This is an analysis tool, not a retail oracle.
Usage: python3 tools/analyze_simlab_forensics.py ~/ThandorLogs/V585/simlab_*.log
"""
from __future__ import annotations
import argparse
from collections import defaultdict
from pathlib import Path


def fields(line: str) -> tuple[str, dict[str, str]]:
    words = line.split()
    if not words:
        return "", {}
    return words[0], dict(word.split("=", 1) for word in words[1:] if "=" in word)


def number(row: dict[str, str], key: str, fallback: int = 0) -> int:
    try:
        return int(row.get(key, str(fallback)))
    except ValueError:
        return fallback


def analyze(path: Path) -> str:
    start = {}
    by_run = {}
    factions = defaultdict(dict)
    entities = defaultdict(dict)
    intervals = defaultdict(int)
    snapshots = defaultdict(list)
    for line in path.read_text(errors="replace").splitlines():
        kind, row = fields(line)
        if kind == "START":
            start = row
        elif kind == "RUN":
            idx = number(row, "index", -1)
            if idx < 0:
                continue
            snapshots[idx].append(row)
            if idx not in by_run or number(row, "tick") >= number(by_run[idx], "tick"):
                by_run[idx] = row
        elif kind == "FACTION":
            idx, faction = number(row, "run", -1), number(row, "f", -1)
            if idx >= 0 and faction >= 1:
                factions[idx][faction] = row
        elif kind == "ENTITY":
            idx = number(row, "run", -1)
            if idx < 0:
                continue
            # Keep latest record for each stable source/faction/ARM; user should consult raw
            # log for transitions because conversion can reuse a source index with a new ARM.
            key = (row.get("source", ""), row.get("faction", ""), row.get("arm", ""))
            if key not in entities[idx] or number(row, "tick") > number(entities[idx][key], "tick"):
                entities[idx][key] = row
    lines = [f"FILE {path}",
             f"MAP {start.get('map', '?')} declared_runs={start.get('runs', '?')} "
             f"active_factions={start.get('active_factions', '?')} "
             f"whole_map_explored={start.get('whole_map_explored', '?')} "
             f"vary_seed={start.get('vary_seed_per_run', 'not_logged')}"]
    if not by_run:
        return "\n".join(lines + ["WARNING no RUN records found"])
    signatures = set()
    for idx in sorted(by_run):
        r = by_run[idx]
        signatures.add(tuple(r.get(k, "") for k in
                             ("seed", "swept_value", "planned", "built", "conversions_completed")))
        lines.append(f"RUN {idx} tick={r.get('tick', '?')} AI_mask={r.get('ai_mask', '?')} "
                     f"planned={r.get('planned', '?')} built={r.get('built', '?')} "
                     f"retry={r.get('retries', '?')} conversions={r.get('conversions_completed', '?')}")
        for faction in sorted(factions[idx]):
            if number(start, "active_factions", 7) < faction:
                continue
            f = factions[idx][faction]
            lines.append(f"  F{faction} live={f.get('live', '?')} pioneer={f.get('pioneers', '?')} "
                         f"buildings={f.get('buildings', '?')} ws08={f.get('ws08', '?')} "
                         f"ws09={f.get('ws09', '?')} presence={f.get('presence_owners', '?')} "
                         f"planning_tick={f.get('last_planning_tick', '?')} "
                         f"AI={f.get('ai_assigned', 'unknown')}")
            if number(f, "pioneers") and not number(f, "ws08") and not number(f, "ws09"):
                lines.append(f"  ALERT F{faction} live Pioneer but no resource/base site candidates")
            if number(f, "pioneers") == 0 and number(f, "buildings") > 0:
                lines.append(f"  NOTE F{faction} missing Pioneer with live structures: verify conversions before declaring death")
        if number(r, "ai_mask") == 0:
            lines.append("  ALERT AI-mask is zero (LEV assignable-player metadata; planner may still run)")
        problems = []
        for e in entities[idx].values():
            if number(e, "terminal") or number(e, "radius") <= 0:
                continue
            if e.get("fog_origin_inside") == "1" and e.get("fog_origin_barrier") == "0" and \
               e.get("owner_disabled") == "0" and number(e, "self_occupancy") == 0:
                problems.append((e.get("faction"), e.get("arm"), e.get("source")))
        for faction,arm,source in problems[:12]:
            lines.append(f"  ALERT F{faction} ARM{arm} source={source} unobstructed active origin, "
                         "but zero self-visibility: inspect relation mask / projection generation")
    if len(by_run) > 1 and len(signatures) == 1:
        lines.append("WARNING Runs have identical logged seeds, sweep values and outcomes; "
                     "check sweep/seed variation before treating them as independent samples")
    lines.append("NOTE `fog_current` and `fog_explored` in V584 are latest union-snapshot checks; "
                 "V585 adds `fog_persistent_explored` for authoritative history at the entity cell.")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logfile", nargs="+", type=Path)
    args = parser.parse_args()
    for index, path in enumerate(args.logfile):
        if index:
            print()
        print(analyze(path))


if __name__ == "__main__":
    main()
