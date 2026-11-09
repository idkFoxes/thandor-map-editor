#!/usr/bin/env python3
"""Audit THDTGR1 retail recordings using only independently valid V1 evidence.

Faction 1 is not hard-coded as human. TICK.humanFactionMask is authoritative:
bit n marks faction n as human. AIPH rows for human factions are reported but
excluded from autonomous-AI cadence. V1 ACTR faction/ARM/command words are never
used; only root pose, yaw, health, model flags and research id are inspected.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
import mmap
from pathlib import Path
import statistics
import struct
from typing import Any

HEADER_SIZE = 192
SIZES = {b"TICK": 48, b"FACT": 296, b"ENDT": 16, b"AIPH": 16,
         b"UCMD": 32, b"AIFA": 16, b"ACTR": 164, b"RMOV": 8, b"AIAA": 20}
TICK_FIELDS = ("simulation_tick presentation_tick periodic_clock_tick game_speed_q8 "
               "relation_ui_flags network_tick primary_seed secondary_seed rng_selector "
               "human_faction_mask player_count pending_overflow_count").split()


def signed32(value: int) -> int:
    return value if value < 0x80000000 else value - 0x100000000


def percentile(values: list[int], fraction: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    index = fraction * (len(ordered) - 1)
    lower = int(index)
    upper = min(lower + 1, len(ordered) - 1)
    weight = index - lower
    return ordered[lower] * (1.0 - weight) + ordered[upper] * weight


def summarize_intervals(ticks: list[int]) -> dict[str, Any]:
    intervals = [later - earlier for earlier, later in zip(ticks, ticks[1:])]
    distribution = Counter(intervals)
    return {
        "events": len(ticks),
        "first_tick": ticks[0] if ticks else None,
        "last_tick": ticks[-1] if ticks else None,
        "interval_count": len(intervals),
        "interval_min": min(intervals) if intervals else None,
        "interval_median": statistics.median(intervals) if intervals else None,
        "interval_p95": percentile(intervals, 0.95),
        "interval_max": max(intervals) if intervals else None,
        "interval_distribution": {str(k): v for k, v in sorted(distribution.items())},
    }


def analyze(path: Path) -> dict[str, Any]:
    result: dict[str, Any] = {
        "format": "THDTGR1-V751-retail-AI-oracle",
        "file": path.name,
        "size_bytes": path.stat().st_size,
        "errors": [],
        "oracle_boundary": {
            "valid": ["TICK", "AIPH with validated metadata", "FACT", "UCMD",
                      "ACTR root pose/yaw/health/model flags/research id"],
            "excluded": ["V1 ACTR faction", "V1 ACTR ARM/asset id",
                         "V1 ACTR army commands/targets/movement",
                         "V1 AIAA/RMOV derived faction attribution"],
        },
    }
    counts: Counter[str] = Counter()
    payload_sizes: dict[str, Counter[int]] = defaultdict(Counter)
    human_masks: Counter[int] = Counter()
    game_speeds: Counter[int] = Counter()
    valid_ai_ticks: dict[int, list[int]] = defaultdict(list)
    valid_ai_phase_counts: dict[int, Counter[int]] = defaultdict(Counter)
    player_aiph: Counter[int] = Counter()
    invalid_aiph: Counter[str] = Counter()
    slot_guard_misses: Counter[int] = Counter()
    phase_errors: Counter[int] = Counter()
    user_commands: dict[int, Counter[int]] = defaultdict(Counter)
    command_sources: Counter[int] = Counter()
    first_fact: dict[int, list[int]] = {}
    last_fact: dict[int, list[int]] = {}
    previous_fact: dict[int, list[int]] = {}
    technology_transitions: list[dict[str, Any]] = []
    lifecycle_transitions: list[dict[str, Any]] = []
    root_states: dict[int, tuple[tuple[int, ...], int, int, int]] = {}
    roots_pose_changed: set[int] = set()
    roots_health_decreased: set[int] = set()
    roots_zero_health: set[int] = set()
    roots_researching: Counter[int] = Counter()
    tick_samples: list[dict[str, int]] = []
    frame_record_counts: Counter[str] | None = None
    frame_actor_ids: set[int] = set()
    current_tick: tuple[int, ...] | None = None
    previous_ordinal: int | None = None
    frames = 0
    accepted_records = 0
    peak_actor_rows = 0

    with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        if len(data) < HEADER_SIZE or data[:8] != b"THDTGR1\0":
            result["errors"].append("invalid or truncated THDTGR1 header")
            return result
        version, header_size = struct.unpack_from("<II", data, 8)
        result["version"] = version
        result["header_size"] = header_size
        result["header_flags"] = struct.unpack_from("<I", data, 16)[0]
        result["initial_simulation_tick"] = struct.unpack_from("<I", data, 20)[0]
        result["initial_primary_seed"] = struct.unpack_from("<I", data, 24)[0]
        result["initial_secondary_seed"] = struct.unpack_from("<I", data, 28)[0]
        result["map_name"] = data[32:160].decode("utf-16-le", errors="replace").split("\0", 1)[0]
        if version != 1 or header_size != HEADER_SIZE:
            result["errors"].append("V751 audit currently admits only THDTGR1 version 1 / 192-byte header")
            return result

        offset = header_size
        while offset < len(data):
            if offset + 8 > len(data):
                result["errors"].append(f"truncated record header at {offset}")
                break
            tag = data[offset:offset + 4]
            length = struct.unpack_from("<I", data, offset + 4)[0]
            payload = offset + 8
            end = payload + length
            if length > 16 * 1024 * 1024 or end > len(data):
                result["errors"].append(f"invalid payload bounds at {offset}: {length}")
                break
            if tag not in SIZES or SIZES[tag] != length:
                result["errors"].append(f"unknown tag or invalid payload size at {offset}: {tag!r}/{length}")
                break
            name = tag.decode("ascii")
            counts[name] += 1
            payload_sizes[name][length] += 1
            accepted_records += 1

            if tag == b"TICK":
                if frame_record_counts is not None:
                    result["errors"].append(f"missing ENDT before TICK at {offset}")
                    break
                current_tick = struct.unpack_from("<12I", data, payload)
                ordinal = current_tick[0]
                if previous_ordinal is not None and ordinal != previous_ordinal + 1:
                    result["errors"].append(
                        f"nonconsecutive TICK ordinal {ordinal}, expected {previous_ordinal + 1}")
                    break
                previous_ordinal = ordinal
                frame_record_counts = Counter()
                frame_actor_ids = set()
                human_masks[current_tick[9]] += 1
                game_speeds[current_tick[3]] += 1
                if frames < 4 or ordinal % 1000 == 0:
                    tick_samples.append(dict(zip(TICK_FIELDS, current_tick)))
            if frame_record_counts is None or current_tick is None:
                result["errors"].append(f"record outside TICK/ENDT frame at {offset}")
                break
            frame_record_counts[name] += 1
            simulation_tick = current_tick[0]
            human_mask = current_tick[9]

            if tag == b"AIPH":
                captured_tick, faction, phase, reserved = struct.unpack_from("<4I", data, payload)
                reason = None
                if captured_tick != simulation_tick:
                    reason = "captured tick mismatch"
                elif not 1 <= faction <= 7:
                    reason = "out-of-range faction"
                elif reserved != 0:
                    reason = "reserved word nonzero"
                elif phase != ((simulation_tick >> 6) & 1):
                    reason = "schedule phase mismatch"
                if reason is not None:
                    invalid_aiph[reason] += 1
                elif human_mask & (1 << faction):
                    player_aiph[faction] += 1
                else:
                    valid_ai_ticks[faction].append(simulation_tick)
                    valid_ai_phase_counts[faction][phase] += 1
                    if faction != ((simulation_tick >> 3) & 7):
                        slot_guard_misses[faction] += 1
                    if phase != ((simulation_tick >> 6) & 1):
                        phase_errors[faction] += 1
            elif tag == b"UCMD":
                words = struct.unpack_from("<8I", data, payload)
                source, captured_tick, _player_runtime, faction, command = words[:5]
                command_sources[source] += 1
                # UCMD's explicit faction is recorder input metadata, not the broken ACTR lookup.
                if captured_tick == simulation_tick and faction < 8:
                    user_commands[faction][command] += 1
            elif tag == b"FACT":
                words = list(struct.unpack_from("<74I", data, payload))
                faction = words[0]
                if faction >= 8:
                    result["errors"].append(f"FACT faction out of range at {offset}: {faction}")
                    break
                first_fact.setdefault(faction, words)
                if faction in previous_fact:
                    before = previous_fact[faction]
                    if before[1] != words[1]:
                        lifecycle_transitions.append({"tick": simulation_tick, "faction": faction,
                                                      "before": before[1], "after": words[1]})
                    if before[50:58] != words[50:58]:
                        added, removed = [], []
                        for word_index, (old, new) in enumerate(zip(before[50:58], words[50:58])):
                            for bit in range(32):
                                mask = 1 << bit
                                tech = word_index * 32 + bit
                                if (new & mask) and not (old & mask):
                                    added.append(tech)
                                if (old & mask) and not (new & mask):
                                    removed.append(tech)
                        technology_transitions.append({"tick": simulation_tick, "faction": faction,
                                                       "added": added, "removed": removed})
                previous_fact[faction] = words
                last_fact[faction] = words
            elif tag == b"ACTR":
                words = struct.unpack_from("<41I", data, payload)
                actor = words[0]
                if actor in frame_actor_ids:
                    result["errors"].append(f"duplicate ACTR identity {actor} at tick {simulation_tick}")
                    break
                frame_actor_ids.add(actor)
                pose = tuple(words[4:10])
                health = signed32(words[19])
                model_flags = words[38]
                research_id = words[40]
                if actor in root_states:
                    old_pose, old_health, _old_flags, _old_research = root_states[actor]
                    if pose != old_pose:
                        roots_pose_changed.add(actor)
                    if health < old_health:
                        roots_health_decreased.add(actor)
                if health == 0:
                    roots_zero_health.add(actor)
                if model_flags & 0xC0:
                    roots_researching[research_id] += 1
                root_states[actor] = (pose, health, model_flags, research_id)
            elif tag == b"ENDT":
                end_words = struct.unpack_from("<4I", data, payload)
                expected = (simulation_tick, sum(frame_record_counts.values()),
                            frame_record_counts["ACTR"], frame_record_counts["FACT"])
                if end_words != expected:
                    result["errors"].append(
                        f"ENDT mismatch at {offset}: {end_words} != {expected}")
                    break
                peak_actor_rows = max(peak_actor_rows, frame_record_counts["ACTR"])
                frames += 1
                frame_record_counts = None
                frame_actor_ids = set()
                current_tick = None
            offset = end

        if frame_record_counts is not None and not result["errors"]:
            result["errors"].append("file ended before ENDT")
        result["end_offset"] = offset

    def fact_summary(words: list[int]) -> dict[str, Any]:
        return {
            "lifecycle": words[1],
            "xenite_q4": words[2], "xenite_cap_q4": words[3],
            "tritium_q4": words[6], "tritium_cap_q4": words[7],
            "baseline_supply_q4": words[10], "generation_capacity_q4": words[11],
            "supplied_demand_q4": words[12], "unpowered_demand_q4": words[13],
            "terrain_scale_q8": words[20],
            "technology_mask": words[50:58],
            "relation_ticks": words[58:66],
            "ai_pressure": [signed32(v) for v in words[66:74]],
        }

    ai_cadence = {}
    all_factions = sorted(set(valid_ai_ticks) | set(player_aiph))
    for faction in all_factions:
        summary = summarize_intervals(valid_ai_ticks[faction])
        summary.update({
            "phase_counts": {str(k): v for k, v in sorted(valid_ai_phase_counts[faction].items())},
            "slot_guard_misses": slot_guard_misses[faction],
            "phase_errors": phase_errors[faction],
            "excluded_human_events": player_aiph[faction],
        })
        ai_cadence[str(faction)] = summary

    result.update({
        "complete_frames": frames,
        "accepted_records": accepted_records,
        "record_counts": dict(counts),
        "payload_sizes": {name: dict(values) for name, values in payload_sizes.items()},
        "human_faction_masks": {str(k): v for k, v in sorted(human_masks.items())},
        "game_speed_q8_values": {str(k): v for k, v in sorted(game_speeds.items())},
        "ai_cadence": ai_cadence,
        "invalid_aiph": dict(invalid_aiph),
        "legacy_ai_hook_corruption_detected": invalid_aiph["out-of-range faction"] != 0,
        "ordinary_unhooked_ai_cadence_oracle": False if invalid_aiph["out-of-range faction"] else True,
        "aiph_scope": (
            "AIPH records the direct planner arguments observed in this instrumented run. "
            "When out-of-range faction arguments occur, the legacy recorder hook changed later "
            "faction dispatches; admitted rows describe that modified history, not unhooked retail AI cadence."
        ),
        "user_commands_by_faction": {
            str(f): {str(command): count for command, count in sorted(commands.items())}
            for f, commands in sorted(user_commands.items())},
        "user_command_sources": {str(k): v for k, v in sorted(command_sources.items())},
        "first_faction_state": {str(f): fact_summary(words) for f, words in sorted(first_fact.items())},
        "last_faction_state": {str(f): fact_summary(words) for f, words in sorted(last_fact.items())},
        "lifecycle_transitions": lifecycle_transitions,
        "technology_transitions": technology_transitions,
        "root_model_evidence": {
            "distinct_roots": len(root_states),
            "peak_rows_per_frame": peak_actor_rows,
            "roots_with_pose_changes": len(roots_pose_changed),
            "roots_with_health_decreases": len(roots_health_decreased),
            "roots_reaching_zero_health": len(roots_zero_health),
            "researching_root_samples_by_technology": {
                str(k): v for k, v in sorted(roots_researching.items())},
        },
        "tick_samples": tick_samples,
    })
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recording", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    result = analyze(args.recording)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({
        "map_name": result.get("map_name"),
        "complete_frames": result.get("complete_frames"),
        "human_faction_masks": result.get("human_faction_masks"),
        "ai_factions": sorted(result.get("ai_cadence", {}).keys()),
        "invalid_aiph": result.get("invalid_aiph"),
        "lifecycle_transitions": len(result.get("lifecycle_transitions", [])),
        "technology_transitions": len(result.get("technology_transitions", [])),
        "errors": result.get("errors"),
    }))
    return 1 if result.get("errors") else 0


if __name__ == "__main__":
    raise SystemExit(main())
