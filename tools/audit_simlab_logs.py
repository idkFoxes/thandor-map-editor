#!/usr/bin/env python3
"""Audit sampled SimLab diagnostics without counting repeated samples as events.

Usage: python tools/audit_simlab_logs.py LOG_DIRECTORY OUTPUT.json
Old log schemas omit audio/animation fields; absence is reported, never a pass.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import re

FIELDS = re.compile(r"(\w+)=([^\s]+)")


def number(row, key, default=0):
    try:
        return int(row.get(key, default))
    except ValueError:
        return default


def audit(path):
    counts = Counter()
    runs, entities, economy = {}, {}, {}
    first_advanced = {}
    maxima = {}
    header, start, end = "", {}, None
    audio_first, audio_last, audio_max = None, None, {}
    errors, nonfinite = [], []
    for line_number, line in enumerate(path.open(errors="replace"), 1):
        kind = line.split(" ", 1)[0].strip()
        if not kind:
            continue
        counts[kind] += 1
        row = dict(FIELDS.findall(line))
        if line_number == 1:
            header = line.strip()
        if kind == "START":
            start = row
        elif kind == "END":
            end = row
        elif kind == "ERROR":
            errors.append({"line": line_number, "text": line.strip()})
        elif kind == "RUN":
            ri = number(row, "index")
            if number(row, "tick") >= number(runs.get(ri, {}), "tick"):
                runs[ri] = row
        elif kind == "ENTITY":
            key = (number(row, "run"), number(row, "entity"))
            previous = entities.get(key, {})
            if number(row, "tick") >= number(previous, "tick"):
                entities[key] = row
            if 302 <= number(row, "arm") <= 306 and row.get("removed") != "1":
                arm_key = f"run{key[0]}:f{number(row, 'faction')}:arm{number(row, 'arm')}"
                first_advanced.setdefault(arm_key, number(row, "tick"))
        elif kind == "ECONOMY":
            key = (number(row, "run"), number(row, "faction"))
            if number(row, "tick") >= number(economy.get(key, {}), "tick"):
                economy[key] = row
        elif kind == "AUDIO":
            if audio_first is None:
                audio_first = row
            audio_last = row
            for key, value in row.items():
                try:
                    audio_max[key] = max(float(value), audio_max.get(key, float("-inf")))
                except ValueError:
                    pass
        if kind in {"PERF", "RUN", "AUDIO"}:
            for key in ("gui_dynamic_build_ms", "gui_sync_ms", "step_wall_us",
                        "snapshot_peak_us", "worker_peak_generation_us", "log_writer_peak_ms",
                        "peak_pending_ticks", "log_failures", "invalid_health_scales"):
                if key in row:
                    try:
                        value = float(row[key])
                        if value > maxima.get(key, -1):
                            maxima[key] = value
                    except ValueError:
                        pass
        if any(v.lower() in {"nan", "+nan", "-nan", "inf", "+inf", "-inf"} for v in row.values()):
            nonfinite.append(line_number)
    summaries = []
    for ri, final in sorted(runs.items()):
        owner_rows = [v for (r, _), v in entities.items() if r == ri]
        sample_tick = max((number(v, "tick") for v in owner_rows), default=None)
        owner_rows = [v for v in owner_rows if number(v, "tick") == sample_tick]
        alive = [v for v in owner_rows if v.get("removed") != "1" and v.get("terminal") != "1"]
        factions = {str(f): v for (r, f), v in economy.items() if r == ri}
        warnings = []
        if final.get("finished") != "1":
            warnings.append("Not observed finished; may be restart/partial recording, not a crash verdict.")
        if number(final, "oldest_primary_age_ticks") >= 10000:
            warnings.append("Primary request age >=10000 ticks; diagnose prerequisites/placement/producer, not generic expiry.")
        if number(final, "factory_exit_retries") >= 1000:
            warnings.append("At least 1000 cumulative factory-exit retries; no per-unit stall duration implied.")
        moving_walkers = [v for v in alive if v.get("class") == "3" and
                          (v.get("translated") == "1" or v.get("turned") == "1")]
        summaries.append({
            "index": ri, "final_run": final, "latest_entity_sample_tick": sample_tick,
            "latest_sample_live_arm_counts": dict(sorted(Counter(v.get("arm") for v in alive).items(), key=lambda x: int(x[0]))),
            "latest_sample_live_class_counts": dict(Counter(v.get("class") for v in alive)),
            "latest_sample_moving_walkers": len(moving_walkers),
            "latest_sample_moving_walkers_without_contacts": sum(v.get("contacts_initialized") == "0" for v in moving_walkers),
            "final_economy_by_faction": factions, "warnings": warnings,
        })
    return {
        "file": path.name, "size_bytes": path.stat().st_size,
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(), "header": header,
        "start": start, "end": end, "row_counts": dict(counts), "max_sampled_gauges": maxima,
        "first_observed_advanced_factories": first_advanced, "errors": errors,
        "nonfinite_row_numbers": nonfinite, "audio_rows_present": counts["AUDIO"] > 0,
        "audio_first": audio_first, "audio_last": audio_last, "audio_max_sampled": audio_max,
        "runs": summaries,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log_directory", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    files = sorted(args.log_directory.glob("*.log"))
    if not files:
        parser.error("no .log files found")
    result = {"schema": "v644-simlab-audit-1", "limits": [
        "Editor diagnostic versions are recorded per file; none is a native-game trace.",
        "Counters are cumulative per run; files/seeds may duplicate experiments. Do not sum across files.",
        "Latest ENTITY/ECONOMY snapshots can precede the final RUN tick; only explicit observations are listed.",
        "Audio submission/completion counters are not proof of audible speaker output; no pixels means no rendered-animation certification.",
    ], "files": [audit(path) for path in files]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    for item in result["files"]:
        finals = ", ".join(f"r{r['index']}:{r['final_run']['tick']} finished={r['final_run'].get('finished')}" for r in item["runs"])
        print(f"{item['file']} {item['start'].get('map')} {finals}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
