#!/usr/bin/env python3
"""Compile the actual AI knowledge panel without requiring Vulkan or GLFW."""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    source = args.source.resolve()
    implementation = source / "src/editor/ai_knowledge_panel.cpp"
    fixture = source / "tests/fixtures/imgui_ai_knowledge_panel_fixture.hpp"
    require(implementation.is_file(), f"missing {implementation}")
    require(fixture.is_file(), f"missing {fixture}")

    text = implementation.read_text(encoding="utf-8")
    include_token = '#include "thandor/binary.hpp"'
    first_use = text.find("thandor::q12_to_float")
    include_at = text.find(include_token)
    require(include_at >= 0, "ai_knowledge_panel.cpp must directly include thandor/binary.hpp")
    require(first_use >= 0 and include_at < first_use,
            "thandor/binary.hpp must be visible before q12_to_float use")

    compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    require(bool(compiler), "no C++ compiler available for AI knowledge panel compile regression")

    with tempfile.TemporaryDirectory(prefix="thandor-ai-panel-compile-") as tmp:
        tmp_path = Path(tmp)
        fixture_dir = tmp_path / "fixture"
        fixture_dir.mkdir()
        (fixture_dir / "imgui.h").write_text(
            '#include "imgui_ai_knowledge_panel_fixture.hpp"\n', encoding="utf-8")
        shutil.copy2(fixture, fixture_dir / "imgui_ai_knowledge_panel_fixture.hpp")
        command = [
            str(compiler), "-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
            "-fsyntax-only", "-I", str(fixture_dir), "-I", str(source / "include"),
            "-I", str(source / "src/editor"), str(implementation),
        ]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        require(result.returncode == 0,
                "AI knowledge panel target-toolchain compile regression failed:\n" + result.stdout)

    print("AI_KNOWLEDGE_PANEL_DIRECT_DEPENDENCY_COMPILE PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
