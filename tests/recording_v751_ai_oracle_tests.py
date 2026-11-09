#!/usr/bin/env python3
"""Regression test: V751 excludes human factions from AIPH cadence."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("audit_retail_ai", ROOT / "tools/audit_retail_ai.py")
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


def record(tag: bytes, words: list[int]) -> bytes:
    payload = struct.pack("<" + "I" * len(words), *words)
    return tag + struct.pack("<I", len(payload)) + payload


def main() -> int:
    app = (ROOT / "src/editor/editor_app.cpp").read_text(encoding="utf-8")
    header_source = (ROOT / "src/editor/editor_app.hpp").read_text(encoding="utf-8")
    assert "simulation_lab_simulate_all_active_factions_as_ai_" in header_source
    assert "scenario_runtime.human_faction_mask=simulation_lab_human_faction_mask_;" in app
    assert "scenario_runtime.game_speed_q8=std::max(1U,simulation_lab_game_speed_q8_);" in app
    assert "human_faction_mask=" in app and "game_speed_q8=" in app

    header = bytearray(192)
    header[:8] = b"THDTGR1\0"
    struct.pack_into("<II", header, 8, 1, 192)
    "test".encode("utf-16-le")
    header[32:40] = "test".encode("utf-16-le")
    # Tick 16: slot faction 2, phase 0. Human mask bit 1 marks faction 1/Ares.
    tick = [16, 16, 0, 256, 0, 0, 1, 2, 0, 2, 1, 0]
    body = bytearray(header)
    body += record(b"TICK", tick)
    body += record(b"AIPH", [16, 1, 0, 0])  # valid metadata, excluded human faction
    body += record(b"AIPH", [16, 2, 0, 0])  # admitted AI faction and slot
    body += record(b"ENDT", [16, 4, 0, 0])
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "recording.tgr"
        path.write_bytes(body)
        result = MODULE.analyze(path)
    assert not result["errors"], result["errors"]
    assert result["human_faction_masks"] == {"2": 1}
    assert result["ai_cadence"]["1"]["events"] == 0
    assert result["ai_cadence"]["1"]["excluded_human_events"] == 1
    assert result["ai_cadence"]["2"]["events"] == 1
    assert result["ai_cadence"]["2"]["slot_guard_misses"] == 0
    print("PASS: V751 human faction is excluded from retail AIPH cadence")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
