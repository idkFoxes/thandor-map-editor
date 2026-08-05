#!/usr/bin/env python3
"""Regression for the full-editor Vec3f/PreviewVec2 helper collision reported after G389."""
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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    args = parser.parse_args()
    source = args.source.resolve()
    math_header = (source / "src/editor/math.hpp").read_text(encoding="utf-8")
    preview_header = (source / "src/editor/asset_preview.hpp").read_text(encoding="utf-8")
    implementation = (source / "src/editor/editor_app.cpp").read_text(encoding="utf-8")

    require("inline float vec3_length(Vec3f value) noexcept" in math_header,
            "domain-specific Vec3f magnitude helper is missing")
    require("inline float length(PreviewVec2 value) noexcept" in preview_header,
            "2D preview length authority unexpectedly changed")
    require("const float distance=vec3_length(delta);" in implementation,
            "simulation line-of-fire does not use the domain-specific Vec3f helper")
    require(re.search(r"\b(?:const\s+float\s+distance\s*=)?\s*length\s*\(\s*delta\s*\)",
                      implementation) is None,
            "unqualified length(delta) reintroduced the PreviewVec2 overload collision")

    compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    require(bool(compiler), "no C++ compiler available for vector-domain compile regression")
    snippet = r'''
#include "asset_preview.hpp"
#include "math.hpp"
#include <cmath>
int main() {
    const editor::PreviewVec2 preview{3.0F, 4.0F};
    const thandor::Vec3f world{2.0F, 3.0F, 6.0F};
    const float preview_distance = editor::length(preview);
    const float world_distance = editor::vec3_length(world);
    return (std::abs(preview_distance - 5.0F) < 1.0e-6F &&
            std::abs(world_distance - 7.0F) < 1.0e-6F) ? 0 : 1;
}
'''
    with tempfile.TemporaryDirectory(prefix="thandor-g390-") as temporary:
        cpp = Path(temporary) / "vector_domain.cpp"
        exe = Path(temporary) / "vector_domain"
        cpp.write_text(snippet, encoding="utf-8")
        command = [str(compiler), "-std=c++20", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
                   "-I", str(source / "include"), "-I", str(source / "src/editor"),
                   str(cpp), "-o", str(exe)]
        result = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        require(result.returncode == 0, "vector-domain compile regression failed:\n" + result.stdout)
        run = subprocess.run([str(exe)], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        require(run.returncode == 0, "vector-domain runtime regression failed:\n" + run.stdout)

    print("G390 Vec3f/PreviewVec2 domain-helper compile regression PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
