#!/usr/bin/env python3
from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import tempfile


def require(text: str, token: str, label: str) -> None:
    if token not in text:
        raise SystemExit(f"G391 missing {label}: {token}")


def reject(text: str, token: str, label: str) -> None:
    if token in text:
        raise SystemExit(f"G391 rejected {label}: {token}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    source = args.source.resolve()

    editor = (source / "src/editor/editor_app.cpp").read_text(encoding="utf-8")
    model = (source / "src/core/model.cpp").read_text(encoding="utf-8")
    simulation = (source / "src/core/simulation.cpp").read_text(encoding="utf-8")
    test = (source / "tests/g391_tank_track_articulated_walker_unit_class_tests.cpp").read_text(encoding="utf-8")

    for token, label in [
        ("simulation_update_tank_track_offsets", "tank-track runtime update"),
        ("primary_track_texture_offset_u", "primary track UV"),
        ("secondary_track_texture_offset_u", "secondary track UV"),
        ("apply_walker_pose_to_instances", "walker hierarchy support"),
        ("Articulated support active", "walker diagnostic"),
        ("Tank tracks: selectors", "tank diagnostic"),
    ]:
        require(editor, token, label)
    require(model, "offset + 0x1B8U", "primary MDL track selector")
    require(model, "offset + 0x1BCU", "secondary MDL track selector")
    require(simulation, "wrap_model_texture_offset", "signed track wrap")
    reject(editor + model + simulation, "track_texture_travel_scale_q12", "false Q12 track multiplier")
    reject(editor, "terrain fallback is shown", "superseded flat-walker fallback")
    reject(test, "primary_track_texture_offset_u*", "overflow-prone sign-product assertion")
    reject(test, "secondary_track_texture_offset_u*", "overflow-prone sign-product assertion")

    compiler = os.environ.get("CXX", "c++")
    snippet = r'''#include "articulated_walker_pose.hpp"
#include "thandor/simulation.hpp"
#include <vector>
int main() {
    std::vector<thandor::ModelVisualNode> nodes;
    std::vector<editor::ModelHierarchyTransform> transforms;
    auto result = editor::apply_articulated_walker_support_pose(
        nodes, transforms, 0, 1, 0, 0, false,
        [](float, float) { return 0.0F; }, [](uint32_t) { return 0.0F; });
    thandor::SimulationActorState actor;
    thandor::simulation_update_tank_track_offsets(actor, {}, 0, {}, 0, -1.0F, 259);
    return result.applied ? 0 : 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="thandor-g391-") as temp:
        unit = Path(temp) / "g391_compile.cpp"
        unit.write_text(snippet, encoding="utf-8")
        command = [
            compiler, "-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
            "-fsyntax-only", str(unit),
            "-I", str(source / "include"), "-I", str(source / "src/editor"),
        ]
        completed = subprocess.run(command, text=True, capture_output=True)
        if completed.returncode != 0:
            raise SystemExit("G391 compile regression failed:\n" + completed.stdout + completed.stderr)

    print("G391 tank-track/articulated-walker compile regression PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
