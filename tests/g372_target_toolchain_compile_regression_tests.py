#!/usr/bin/env python3
"""G372 regression for the two G371 target-toolchain compile failures."""
from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import tempfile
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def compile_snippet(source: Path, text: str, name: str) -> None:
    compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    require(bool(compiler), "no C++ compiler available for G372 compile regression")
    with tempfile.TemporaryDirectory(prefix="thandor-g372-") as tmp:
        cpp = Path(tmp) / f"{name}.cpp"
        obj = Path(tmp) / f"{name}.o"
        cpp.write_text(text, encoding="utf-8")
        command = [
            str(compiler), "-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
            "-I", str(source / "include"), "-I", str(source / "src/editor"),
            "-I", str(source / "tests/fixtures"), "-c", str(cpp), "-o", str(obj),
        ]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        require(result.returncode == 0, f"{name} compile regression failed:\n{result.stdout}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    source = args.source.resolve()
    header = (source / "src/editor/editor_app.hpp").read_text(encoding="utf-8")
    implementation = (source / "src/editor/editor_app.cpp").read_text(encoding="utf-8")
    cmake = (source / "CMakeLists.txt").read_text(encoding="utf-8")

    model_include = header.find('#include "model_transform.hpp"')
    class_start = header.find("class EditorApp")
    first_type_use = header.find("ModelHierarchyTransform")
    require(model_include >= 0, "editor_app.hpp must include model_transform.hpp")
    require(class_start >= 0 and model_include < class_start, "model_transform.hpp include must precede EditorApp")
    require(first_type_use >= 0 and model_include < first_type_use,
            "ModelHierarchyTransform must be declared before member signatures use it")

    require("ImGuiChildFlags_Borders" not in implementation,
            "obsolete/nonexistent ImGuiChildFlags_Borders spelling returned")
    match = re.search(
        r'ImGui::BeginChild\("ingame-selection-panel",\{0\.0F,0\.0F\},([^;]+)\);',
        implementation,
    )
    require(match is not None, "selection-panel BeginChild expression is missing")
    flags_expression = match.group(1).strip()
    require(flags_expression == "ImGuiChildFlags_Border|ImGuiChildFlags_AutoResizeY",
            f"unexpected pinned ImGui child-flag expression: {flags_expression}")
    require("GIT_TAG v1.90.9" in cmake, "Dear ImGui pinned-version contract changed")

    compile_snippet(source, r'''
#include "model_transform.hpp"
#include "thandor/catalog.hpp"
#include "thandor/simulation.hpp"
#include <cstddef>
#include <vector>

namespace editor {
using thandor::Vec3f;
struct HeaderSignatureProbe {
    std::vector<Vec3f> simulation_weapon_launch_points(
        std::size_t, const thandor::SimulationWeaponProfile&,
        const std::vector<ModelHierarchyTransform>&);
    std::vector<ModelHierarchyTransform> simulation_hierarchy_transforms(
        std::size_t, const thandor::ArmyAssetPreset&,
        const thandor::SimulationActorState&);
};
}
int main() { return 0; }
''', "model_hierarchy_signature")

    compile_snippet(source, f'''
#include "imgui_v1_90_9_child_flags.hpp"
constexpr ImGuiChildFlags kSelectionPanelFlags = {flags_expression};
static_assert((kSelectionPanelFlags & ImGuiChildFlags_Border) != 0);
static_assert((kSelectionPanelFlags & ImGuiChildFlags_AutoResizeY) != 0);
int main() {{ return 0; }}
''', "imgui_child_flags")

    print("G372 target-toolchain header visibility and ImGui 1.90.9 child-flag regression PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
