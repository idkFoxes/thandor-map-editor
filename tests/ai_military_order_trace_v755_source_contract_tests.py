#!/usr/bin/env python3
from source_contract_support import require_editor_version_at_least
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
header = (root / "include/thandor/simulation_lab.hpp").read_text(encoding="utf-8")
core = (root / "src/core/simulation_lab.cpp").read_text(encoding="utf-8")
writer = (root / "src/editor/simulation_lab_diagnostic_writer.hpp").read_text(encoding="utf-8")

def require(value: bool, message: str) -> None:
    if not value:
        print(f"FAIL: {message}", file=sys.stderr)
        raise SystemExit(1)

require("kSimulationLabMilitaryOrderTraceCapacity = 64U" in header,
        "bounded 64-record military trace is missing")
for token in ("General05 = 5U", "Visible07 = 7U", "BoundedUnseen03 = 3U",
              "FactionAnchor = 0xFFFFFFFEU"):
    require(token in header, f"military trace workspace identity disappeared: {token}")
for token in ("selected_score", "best_site_score", "best_anchor_score", "best_hostile_score",
              "requested", "primary", "fallback", "stored", "command_generation", "movement_flags"):
    require(token in header, f"military trace lost required field: {token}")
require("simulation_lab_append_military_order_trace" in header and
        "next_military_order_trace_sequence" in header and
        "military_order_trace_overwrites" in header,
        "military trace ring API/state is incomplete")
require(core.count("simulation_lab_append_military_order_trace(run,military_order_trace_from_entity(") >= 3,
        "one or more military command writers are not traced")
require("SimulationLabMilitaryOrderWorkspace::General05" in core and
        "SimulationLabMilitaryOrderWorkspace::FactionAnchor" in core and
        "SimulationLabMilitaryOrderWorkspace::Visible07" in core and
        "SimulationLabMilitaryOrderWorkspace::BoundedUnseen03" in core,
        "WS05/anchor/WS07/bounded-WS03 trace classification is incomplete")
for token in ("MILITARY_ORDER run=", "workspace_entry=", "target_from_unseen=",
              "selected_score=", "primary_x_q12=", "fallback_x_q12=",
              "command_generation=", "movement_flags="):
    require(token in writer, f"diagnostic writer lost military evidence field: {token}")
require("last_military_order_trace_sequence" in writer,
        "diagnostic samples no longer deduplicate military trace sequences")
require_editor_version_at_least(root, 758)
print("V755_MILITARY_ORDER_TRACE_SOURCE_CONTRACT PASS")
