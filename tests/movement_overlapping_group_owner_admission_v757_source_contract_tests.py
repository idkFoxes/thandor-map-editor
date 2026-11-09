#!/usr/bin/env python3
from source_contract_support import require_editor_version_at_least
from pathlib import Path
import argparse
import re
import sys


def require(value: bool, message: str) -> None:
    if not value:
        print(f"FAIL: {message}", file=sys.stderr)
        raise SystemExit(1)


def window(text: str, token: str, span: int = 2200) -> str:
    start = text.find(token)
    require(start >= 0, f"missing source token: {token}")
    return text[start:start + span]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    root = args.source
    source = (root / "src/core/simulation_lab_follow.cpp").read_text(encoding="utf-8")

    helper = window(source, "bool native_route_owner_accelerates")
    require("runtime_model_hierarchy" in helper and
            "ground_acceleration_q12!=0U" in helper and
            "entity.motion.movable()" in helper,
            "authored acceleration gate with synthetic compatibility fallback is missing")

    collector = window(source, "SimulationLabRoutePriorityImage build_overlapping_route_priority_image")
    require("native_route_owner_accelerates(candidate)" in collector,
            "overlapping-route owner collection still uses speed instead of authored acceleration")

    blockers = window(source, "std::vector<OwnedRouteBlocker> group_route_owner_blockers")
    require("construction_complete" not in blockers,
            "group route owner blockers still discard linked unfinished MODEL owners")

    resolver = window(source, "MovementRouteResult resolve(PioneerCommandState& s", 6000)
    owner_loop = window(resolver, "for(size_t i=0;i<run.entities.size();++i)", 900)
    require("construction_complete" not in owner_loop,
            "generic issue-time route blockers still discard linked unfinished MODEL owners")
    require_editor_version_at_least(root, 758)
    print("V757_OVERLAP_OWNER_ADMISSION_SOURCE_CONTRACT PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
