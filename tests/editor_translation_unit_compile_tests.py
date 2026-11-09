#!/usr/bin/env python3
"""Compile the real editor_app.cpp against only minimal unavailable-SDK interface stubs.

This is deliberately not a Vulkan/GLFW/ImGui link test. It catches production translation-unit
syntax, project-header dependency, warning, and C++ type errors on hosts without those SDKs.
"""
from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import tempfile
from pathlib import Path


def require(value: bool, message: str) -> None:
    if not value:
        raise SystemExit(message)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--compiler", type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    stub = source / "tests" / "editor_gui_compile_stub"
    for relative in (
        "vulkan/vulkan.h",
        "GLFW/glfw3.h",
        "imgui.h",
        "backends/imgui_impl_glfw.h",
        "backends/imgui_impl_vulkan.h",
    ):
        require((stub / relative).is_file(), f"missing GUI compile fixture: {relative}")

    compiler = str(args.compiler) if args.compiler else (os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++") or shutil.which("clang++"))
    require(bool(compiler), "no C++ compiler available for V534 editor translation-unit compile gate")
    compiler = str(compiler)

    with tempfile.TemporaryDirectory(prefix="thandor-v534-editor-tu-") as temporary:
        obj = Path(temporary) / ("editor_app.obj" if Path(compiler).name.lower().startswith("cl") else "editor_app.o")
        is_msvc = Path(compiler).name.lower() in {"cl", "cl.exe"}
        if is_msvc:
            command = [
                compiler, "/nologo", "/std:c++20", "/W4", "/WX", "/permissive-", "/c",
                "/DGLFW_INCLUDE_NONE",
                f"/I{stub}", f"/I{source / 'include'}", f"/I{source / 'src/editor'}",
                str(source / "src/editor/editor_app.cpp"), f"/Fo{obj}",
            ]
        else:
            command = [
                compiler, "-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-c",
                "-DGLFW_INCLUDE_NONE",
                "-I", str(stub), "-I", str(source / "include"), "-I", str(source / "src/editor"),
                str(source / "src/editor/editor_app.cpp"), "-o", str(obj),
            ]
        for unit in ("editor_app.cpp", "editor_workspace_ui.cpp", "editor_campaign_ui.cpp", "editor_terrain_generator.cpp"):
            unit_command = [str(source / "src/editor" / unit) if arg == str(source / "src/editor/editor_app.cpp") else arg for arg in command]
            result = subprocess.run(unit_command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            require(result.returncode == 0,
                    f"actual {unit} compile failed against minimal SDK interfaces:\n" + result.stdout)
            require(obj.is_file() and obj.stat().st_size > 0, f"{unit} compile produced no object file")

    print("V534_EDITOR_TRANSLATION_UNIT_COMPILE PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
