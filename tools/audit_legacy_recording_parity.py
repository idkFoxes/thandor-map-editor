#!/usr/bin/env python3
"""Admit complete V1 frames, then export only independently recovered legacy semantics.

Bad bounded frames are discarded until the next TICK. A broken length/header stops
the scan: no guessed resynchronization. Army ownership/orders/targets, derived AI
attribution, child animation and audio are deliberately never treated as evidence.
"""
import argparse
from collections import Counter
import hashlib
import json
import mmap
from pathlib import Path
import struct
from audit_native_recording import SIZES

SAMPLE_TICKS = {2, 8, 16, 66, 74, 82, 90, 98, 106, 114, 122, 130, 256, 1000}
ACTOR_FIELDS = {"model_id": 0, "attachment_count": 1, "x_q12": 4, "y_q12": 5,
                "z_q12": 6, "angle0": 7, "angle1": 8, "yaw_turn16": 9,
                "health": 19, "model_state_flags": 38, "energy_load_q4": 39,
                "research_technology_id": 40}
SIGNED_FIELDS = {"x_q12", "y_q12", "z_q12", "health"}


def signed(value):
    return value if value < 0x80000000 else value - 0x100000000


def admitted_frames(data, stats):
    stats.update(accepted_frames=0, excluded_frames=0, excluded_records=0,
                 accepted_records=0, errors=[], stop_offset=None)
    if len(data) < 192 or data[:8] != b"THDTGR1\0" or struct.unpack_from("<II", data, 8) != (1, 192):
        stats["errors"].append("unsupported or truncated V1 header")
        return
    at, frame, previous_tick = 192, None, None

    def exclude(reason):
        stats["excluded_frames"] += 1
        stats["excluded_records"] += frame["records"]
        if len(stats["errors"]) < 50:
            stats["errors"].append({"offset": frame["offset"], "reason": reason})

    while at < len(data):
        if at + 8 > len(data):
            stats["stop_offset"] = at
            break
        tag, length = struct.unpack_from("<4sI", data, at)
        payload, end = at + 8, at + 8 + length
        if length > 16 * 1024 * 1024 or end > len(data):
            stats["stop_offset"] = at
            break
        if tag == b"TICK":
            if frame is not None:
                exclude("missing ENDT before next TICK")
            frame = {"offset": at, "records": 0, "counts": Counter(),
                     "actors": {}, "factions": {}, "bad": [], "tick": None}
        if frame is None:
            stats["excluded_records"] += 1
            at = end
            continue
        frame["records"] += 1
        frame["counts"][tag] += 1
        if tag not in SIZES or SIZES[tag] != length:
            frame["bad"].append("unknown tag or wrong payload size")
        elif tag == b"TICK":
            frame["tick"] = struct.unpack_from("<12I", data, payload)
            if previous_tick is not None and frame["tick"][0] <= previous_tick:
                frame["bad"].append("non-increasing tick")
            if frame["tick"][11]:
                frame["bad"].append("recorder pending overflow: incomplete event evidence")
        elif tag == b"ACTR":
            words = struct.unpack_from("<41I", data, payload)
            if words[0] in frame["actors"]:
                frame["bad"].append("duplicate ACTR identity")
            frame["actors"][words[0]] = words
        elif tag == b"FACT":
            words = struct.unpack_from("<74I", data, payload)
            if words[0] >= 8 or words[0] in frame["factions"]:
                frame["bad"].append("invalid or duplicate FACT identity")
            frame["factions"][words[0]] = words
        if tag == b"ENDT":
            tick = frame["tick"]
            expected = (tick[0] if tick else None, frame["records"],
                        frame["counts"][b"ACTR"], frame["counts"][b"FACT"])
            if length != 16 or struct.unpack_from("<4I", data, payload) != expected:
                frame["bad"].append("ENDT counters differ")
            if frame["bad"]:
                exclude("; ".join(sorted(set(frame["bad"]))))
            else:
                previous_tick = tick[0]
                stats["accepted_frames"] += 1
                stats["accepted_records"] += frame["records"]
                yield frame
            frame = None
        at = end
    if frame is not None:
        exclude("incomplete frame at end or broken record boundary")
    if stats["stop_offset"] is not None:
        stats["errors"].append({"offset": stats["stop_offset"], "reason": "truncated or excessive record; scan stopped without guessing"})


def analyze(path, initial_tsv=None):
    stats = {"format": "V647-admitted-legacy-parity-evidence", "file": path.name,
             "size_bytes": path.stat().st_size,
             "excluded_semantics": ["ACTR army faction and asset ID", "ACTR army orders, targets and movement state",
                "AIAA/RMOV derived army faction attribution", "child animation, track UVs and audio (not captured)"],
             "sampled_frames": [], "research_mask_transitions": []}
    previous_masks, initial = {}, None
    with path.open("rb") as stream, mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
        stats["sha256"] = hashlib.sha256(data).hexdigest()
        for frame in admitted_frames(data, stats):
            tick = frame["tick"][0]
            if initial is None and frame["actors"]:
                initial = frame
                stats["first_actor_tick"] = tick
                stats["first_actor_count"] = len(frame["actors"])
            for faction, words in frame["factions"].items():
                mask = words[50:58]
                if faction in previous_masks and mask != previous_masks[faction]:
                    stats["research_mask_transitions"].append({"tick": tick, "faction": faction,
                        "before": previous_masks[faction], "after": mask})
                previous_masks[faction] = mask
            if tick in SAMPLE_TICKS:
                facts = {str(f): {"xenite_q4": w[2], "xenite_cap_q4": w[3],
                    "tritium_q4": w[6], "tritium_cap_q4": w[7], "supply_q4": w[10],
                    "capacity_q4": w[11], "supplied_demand_q4": w[12], "unpowered_demand_q4": w[13],
                    "terrain_scale_q8": w[20], "technology_mask": w[50:58]}
                    for f, w in frame["factions"].items()}
                actors = [{key: signed(w[i]) if key in SIGNED_FIELDS else w[i]
                    for key, i in ACTOR_FIELDS.items()} for w in frame["actors"].values()]
                stats["sampled_frames"].append({"tick_words": frame["tick"], "factions": facts, "actors": actors})
    if initial_tsv is not None and initial is not None:
        # Raw indexed words are the probe interchange, never implicit army semantics.
        initial_tsv.write_text("".join("\t".join(map(str, w))+"\n" for w in initial["actors"].values()))
    return stats


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recording", type=Path);parser.add_argument("output", type=Path)
    parser.add_argument("--initial-tsv", type=Path)
    args = parser.parse_args();result = analyze(args.recording, args.initial_tsv)
    args.output.write_text(json.dumps(result, indent=2)+"\n")
    print(json.dumps({k: result[k] for k in ("accepted_frames", "excluded_frames", "accepted_records", "excluded_records", "stop_offset", "errors")}))
    return result["stop_offset"] is not None


if __name__ == "__main__":
    raise SystemExit(main())
