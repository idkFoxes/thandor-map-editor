#!/usr/bin/env python3
import argparse
from pathlib import Path
import sys


def require(value: bool, message: str) -> None:
    if not value:
        print(f"FAIL: {message}", file=sys.stderr)
        raise SystemExit(1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    root = args.source
    header = (root / "include/thandor/simulation_lab_follow.hpp").read_text(encoding="utf-8")
    source = (root / "src/core/simulation_lab_follow.cpp").read_text(encoding="utf-8")
    metrics = (root / "include/thandor/simulation_lab.hpp").read_text(encoding="utf-8")
    log = (root / "src/editor/simulation_lab_diagnostic_writer.hpp").read_text(encoding="utf-8")

    require("kSimulationLabRoutePriorityPairCapacity = 32U" in header,
            "native fixed 32-pair route workspace is missing")
    require("SimulationLabRoutePriorityImage" in header and
            "simulation_lab_overlapping_route_priority_image" in header,
            "bounded route-priority image is not exposed for regression tests")
    require("for(std::size_t cursor=run.entities.size();cursor!=0U;--cursor)" in source,
            "MODEL-owner admission is no longer newest-first")
    require("if((flags&2U)!=0U)return 0" in source and
            "std::int32_t priority=1" in source and
            "if(candidate.faction_index==route_faction)" in source and
            "priority+=candidate.motion.speed_q12_per_tick" in source,
            "recovered locked/foreign/own/speed priority formula changed")
    require("priority_pair_sift_up" in source and "priority_pair_sift_down" in source and
            "std::swap(image.pairs[0],image.pairs[heap_size-1U])" in source,
            "native max-heap extraction order is no longer represented")
    require("append_segment_influence" in source and "0x240" in source,
            "bounded low-band route-segment sampling disappeared")
    require("coordinate_overlapping_group_routes" in source and
            "Dependencies deps{run,e,field,source,statics,cache,false,{},true}" in source,
            "WS14 group commands no longer invoke the issue-time coordinator")
    require("navigation_overlap_group_rebuilds" in metrics and
            "navigation_overlap_group_capacity_reached" in metrics and
            "navigation_overlap_group_source_replans" in metrics,
            "overlapping-route diagnostic counters are missing")
    require("overlap_route_rebuilds=" in log and "overlap_route_capacity=" in log and
            "overlap_route_replans=" in log,
            "overlapping-route diagnostics are not emitted in RUN records")
    print("V754_OVERLAPPING_GROUP_ROUTE_SOURCE_CONTRACT PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
