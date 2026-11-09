#!/usr/bin/env python3
"""Validate observed THDTGR1 framing without guessing recorder-specific fields.

THDTGR1 version1 has a192-byte header followed by tag/u32-length/payload
records. ENDT counters are independently checked against parsed records.
Unverified ACTR/FACT fields are exported as raw indexed words, not game labels.
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import mmap
from pathlib import Path
import struct

SIZES = {b"TICK": 48, b"FACT": 296, b"ENDT": 16, b"AIPH": 16,
         b"UCMD": 32, b"AIFA": 16, b"ACTR": 164, b"RMOV": 8, b"AIAA": 20}
U32 = struct.Struct("<I")
PAIR = struct.Struct("<II")


def audit(path):
    result = {"format": "THDTGR1-framing-audit-2", "file": path.name,
              "size_bytes": path.stat().st_size, "errors": [], "limitations": [
        "V1 recorder reads ModelRuntimeSlot as ArmyRuntimeSlot; raw words retain that capture error.",
        "Use the V646 schema audit for validated field semantics; do not treat V1 ownership/commands as army state.",
        "Complete framing is not proof of native/editor simulation parity, pixels or audible sound.",
    ]}
    errors = result["errors"]
    counts = Counter()
    sizes = defaultdict(Counter)
    first, last = {}, {}
    first_fact, last_fact, last_actor = {}, {}, {}
    first_full_actor, frame_actor = {}, {}
    actor_column1, actor_column2 = Counter(), Counter()
    tick_samples, actor_ids = [], set()
    frames = 0
    max_actor_rows = 0
    frame_counts = None
    previous_ordinal = None
    with path.open("rb") as stream:
        result["sha256"] = hashlib.file_digest(stream, "sha256").hexdigest()
        if result["size_bytes"] < 192:
            errors.append("file smaller than header")
            return result
        with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
            if data[:8] != b"THDTGR1\0":
                errors.append("invalid magic")
                return result
            version, header_size = PAIR.unpack_from(data, 8)
            result["version"] = version
            result["header_size"] = header_size
            result["header_words_16_28"] = list(struct.unpack_from("<4I", data, 16))
            if version not in (1, 2) or header_size != 192:
                errors.append("unsupported version/header size")
                return result
            result["map_name"] = data[32:160].decode("utf-16-le").split("\0", 1)[0]
            offset = header_size
            while offset < len(data):
                if offset + 8 > len(data):
                    errors.append(f"truncated record header at {offset}")
                    break
                tag = data[offset:offset + 4]
                length = U32.unpack_from(data, offset + 4)[0]
                payload = offset + 8
                if length > 16 * 1024 * 1024:
                    errors.append(f"record exceeds supported payload limit at {offset}: {length}")
                    break
                if length > len(data) - payload:
                    errors.append(f"truncated payload at {offset}: {length}")
                    break
                if tag in SIZES and length != SIZES[tag]:
                    errors.append(f"unexpected {tag!r} length {length} at {offset}")
                    break
                name = tag.decode("ascii", errors="replace")
                counts[name] += 1
                sizes[name][length] += 1
                first.setdefault(name, (payload, length))
                last[name] = (payload, length)
                if tag == b"TICK":
                    if frame_counts is not None:
                        errors.append(f"missing ENDT before {offset}")
                        break
                    tick = struct.unpack_from("<12I", data, payload)
                    if previous_ordinal is not None and tick[0] != previous_ordinal + 1:
                        errors.append(f"nonconsecutive observed TICK ordinal {tick[0]}")
                        break
                    previous_ordinal = tick[0]
                    frame_counts = Counter()
                    actor_ids = set()
                    frame_actor = {}
                    if frames < 4 or tick[0] % 1000 == 0:
                        tick_samples.append(list(tick))
                if frame_counts is None:
                    errors.append(f"record outside TICK/ENDT at {offset}")
                    break
                frame_counts[name] += 1
                if tag == b"FACT":
                    index = U32.unpack_from(data, payload)[0]
                    first_fact.setdefault(index, payload)
                    last_fact[index] = payload
                elif tag == b"ACTR":
                    key, column1, column2 = struct.unpack_from("<3I", data, payload)
                    actor_ids.add(key)
                    last_actor[key] = payload
                    frame_actor[key] = payload
                    actor_column1[column1] += 1
                    actor_column2[column2] += 1
                elif tag == b"ENDT":
                    end = struct.unpack_from("<4I", data, payload)
                    expected = (previous_ordinal, sum(frame_counts.values()),
                                frame_counts["ACTR"], frame_counts["FACT"])
                    if end != expected:
                        errors.append(f"ENDT counter mismatch at {offset}: {end} vs {expected}")
                        break
                    if len(actor_ids) != frame_counts["ACTR"]:
                        errors.append(f"duplicate ACTR word0 within ordinal {end[0]}")
                        break
                    frames += 1
                    if not first_full_actor and frame_actor:
                        first_full_actor = frame_actor.copy()
                        result["first_full_actor_tick_raw_words"] = list(tick)
                    max_actor_rows = max(max_actor_rows, frame_counts["ACTR"])
                    frame_counts = None
                offset = payload + length
            if frame_counts is not None and not errors:
                errors.append("file ends before ENDT")
            result.update({"end_offset": offset, "complete_frames": frames,
                "record_counts": dict(counts),
                "payload_sizes": {k: dict(v) for k, v in sizes.items()},
                "peak_actor_rows_per_frame": max_actor_rows,
                "distinct_actor_word0_values": len(last_actor),
                "actor_word1_distribution": dict(actor_column1),
                "actor_word2_distribution": dict(actor_column2),
                "sampled_tick_words": tick_samples})
            def words(at, length):
                # Known records fit74 words; bound previews for future tags.
                return list(struct.unpack_from("<" + "I" * min(length // 4, 256), data, at))
            result["first_records_raw_words"] = {k: words(*v) for k, v in first.items()}
            result["last_records_raw_words"] = {k: words(*v) for k, v in last.items()}
            result["first_fact_raw_words_by_word0"] = {str(k): words(v, 296) for k, v in first_fact.items()}
            result["last_fact_raw_words_by_word0"] = {str(k): words(v, 296) for k, v in last_fact.items()}
            result["last_actor_raw_words_by_word0"] = {str(k): words(v, 164) for k, v in last_actor.items()}
            result["first_full_actor_raw_words_by_word0"] = {str(k): words(v, 164) for k, v in first_full_actor.items()}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recording", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--initial-positions", type=Path,
                        help="export raw ACTR words0,1,signed4,signed5,19 from first complete actor block")
    args = parser.parse_args()
    result = audit(args.recording)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    if args.initial_positions and not result["errors"]:
        signed = lambda value: value if value < 2**31 else value - 2**32
        rows = result["first_full_actor_raw_words_by_word0"]
        args.initial_positions.parent.mkdir(parents=True, exist_ok=True)
        args.initial_positions.write_text("".join(
            f"{w[0]} {w[1]} {signed(w[4])} {signed(w[5])} {w[19]}\n"
            for w in (rows[key] for key in sorted(rows, key=int))))
    print(json.dumps({k: result.get(k) for k in ["map_name", "size_bytes", "complete_frames",
        "record_counts", "peak_actor_rows_per_frame", "distinct_actor_word0_values", "errors"]}))
    return bool(result["errors"])


if __name__ == "__main__":
    raise SystemExit(main())
