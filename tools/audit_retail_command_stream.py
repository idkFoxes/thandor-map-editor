#!/usr/bin/env python3
"""Audit THDTGR1 command capture from a raw recording or its ZIP container.

This tool deliberately does not interpret V1 ACTR ownership, ARM ids, targets or
commands.  It uses only the recording header, TICK framing, UCMD payloads and
ENDT framing.  A zero UCMD count is a capture-boundary result, not proof that no
player input occurred in the retail session.
"""
from __future__ import annotations

import argparse
from collections import Counter
from contextlib import contextmanager
import json
from pathlib import Path
import struct
import zipfile
from typing import BinaryIO, Iterator

HEADER_SIZE = 192
SIZES = {b"TICK": 48, b"FACT": 296, b"ENDT": 16, b"AIPH": 16,
         b"UCMD": 32, b"AIFA": 16, b"ACTR": 164, b"RMOV": 8, b"AIAA": 20}
TARGET_POSITION_COMMAND = 0xDE0

@contextmanager
def recording_stream(path: Path) -> Iterator[tuple[BinaryIO, str]]:
    if path.suffix.lower() != ".zip":
        with path.open("rb") as stream:
            yield stream, path.name
        return
    with zipfile.ZipFile(path) as archive:
        members = [name for name in archive.namelist() if name.lower().endswith("/replay.tgr") or name.lower() == "replay.tgr"]
        if len(members) != 1:
            raise ValueError(f"expected exactly one replay.tgr in {path}, found {len(members)}")
        with archive.open(members[0], "r") as stream:
            yield stream, members[0]

def read_exact(stream: BinaryIO, size: int) -> bytes:
    data = stream.read(size)
    if len(data) != size:
        raise EOFError(f"truncated recording: wanted {size} bytes, got {len(data)}")
    return data

def audit(path: Path) -> dict:
    result = {
        "format": "THDTGR1-V755-command-stream-audit",
        "input": path.name,
        "errors": [],
        "oracle_boundary": {
            "admitted": ["header", "TICK", "UCMD", "ENDT", "record tag/size counts"],
            "excluded": ["V1 ACTR faction", "V1 ACTR ARM id", "V1 ACTR target/command/movement",
                         "AIAA/AIFA semantic attribution"],
        },
    }
    counts: Counter[str] = Counter()
    commands: Counter[int] = Counter()
    sources: Counter[int] = Counter()
    factions: Counter[int] = Counter()
    human_masks: Counter[int] = Counter()
    game_speeds: Counter[int] = Counter()
    command_samples: list[dict] = []
    frames = 0
    current_tick = None
    records_in_frame = 0
    actors_in_frame = 0
    factions_in_frame = 0
    try:
        with recording_stream(path) as (stream, member):
            result["recording_member"] = member
            header = read_exact(stream, HEADER_SIZE)
            if header[:8] != b"THDTGR1\0":
                raise ValueError("not a THDTGR1 recording")
            version, header_bytes, flags = struct.unpack_from("<III", header, 8)
            result.update(version=version, header_bytes=header_bytes, header_flags=flags,
                          map_name=header[32:160].decode("utf-16-le", errors="replace").split("\0", 1)[0])
            if version != 1 or header_bytes != HEADER_SIZE:
                raise ValueError("V755 command audit admits only THDTGR1 V1 / 192-byte headers")
            while True:
                record_header = stream.read(8)
                if not record_header:
                    break
                if len(record_header) != 8:
                    raise EOFError("truncated record header")
                tag = record_header[:4]
                length = struct.unpack_from("<I", record_header, 4)[0]
                if tag not in SIZES or SIZES[tag] != length:
                    raise ValueError(f"unknown tag or invalid size: {tag!r}/{length}")
                payload = read_exact(stream, length)
                name = tag.decode("ascii")
                counts[name] += 1
                if tag == b"TICK":
                    if current_tick is not None:
                        raise ValueError("nested TICK before ENDT")
                    words = struct.unpack("<12I", payload)
                    current_tick = words[0]
                    human_masks[words[9]] += 1
                    game_speeds[words[3]] += 1
                    records_in_frame = 1
                    actors_in_frame = 0
                    factions_in_frame = 0
                    continue
                if current_tick is None:
                    raise ValueError(f"{name} outside a TICK/ENDT frame")
                records_in_frame += 1
                if tag == b"ACTR":
                    actors_in_frame += 1
                elif tag == b"FACT":
                    factions_in_frame += 1
                elif tag == b"UCMD":
                    words = struct.unpack("<8I", payload)
                    source, captured_tick, player, faction, command, payload0c, payload08, payload04 = words
                    commands[command] += 1
                    sources[source] += 1
                    factions[faction] += 1
                    if len(command_samples) < 32:
                        command_samples.append({
                            "frame_tick": current_tick, "captured_tick": captured_tick,
                            "source": source, "player": player, "faction": faction,
                            "command": command, "payload_dword_0c": payload0c,
                            "payload_dword_08": payload08, "payload_dword_04": payload04,
                        })
                elif tag == b"ENDT":
                    tick, record_count, actor_count, faction_count = struct.unpack("<4I", payload)
                    if (tick != current_tick or record_count != records_in_frame or
                            actor_count != actors_in_frame or faction_count != factions_in_frame):
                        raise ValueError("ENDT framing/count mismatch")
                    frames += 1
                    current_tick = None
            if current_tick is not None:
                raise EOFError("recording ended before ENDT")
    except Exception as exc:  # Keep a structured result for batch audits.
        result["errors"].append(str(exc))
    result.update(
        complete_frames=frames,
        record_counts=dict(counts),
        human_faction_masks={str(k): v for k, v in sorted(human_masks.items())},
        game_speed_q8_values={str(k): v for k, v in sorted(game_speeds.items())},
        user_command_count=sum(commands.values()),
        user_commands_by_code={f"0x{k:X}": v for k, v in sorted(commands.items())},
        user_command_sources={str(k): v for k, v in sorted(sources.items())},
        user_command_factions={str(k): v for k, v in sorted(factions.items())},
        target_position_command_code=f"0x{TARGET_POSITION_COMMAND:X}",
        target_position_command_count=commands[TARGET_POSITION_COMMAND],
        command_samples=command_samples,
        selection_command_stream_available=sum(commands.values()) != 0,
        conclusion=("UCMD evidence is available for decoder validation" if commands
                    else "No UCMD records: these files cannot validate selection target-army/target-position decoding"),
    )
    return result

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("recordings", nargs="+", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    results = [audit(path) for path in args.recordings]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps({"recordings": results}, indent=2) + "\n", encoding="utf-8")
    print(json.dumps([{key: item.get(key) for key in
                       ("input", "map_name", "complete_frames", "user_command_count", "conclusion", "errors")}
                      for item in results]))
    return 1 if any(item["errors"] for item in results) else 0

if __name__ == "__main__":
    raise SystemExit(main())
