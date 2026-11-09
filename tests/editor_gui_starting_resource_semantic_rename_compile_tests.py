#!/usr/bin/env python3
"""Compile the exact starting-resource zero-state condition without GUI SDK dependencies."""
from __future__ import annotations
import argparse, re, shutil, subprocess, tempfile
from pathlib import Path


def fail(msg: str) -> None:
    raise SystemExit(msg)


def compile_expr(cxx: str, expr: str, expect_success: bool) -> None:
    source = f'''int main() {{\n  int xenite_units=0;\n  int tritium_units=0;\n  if ({expr}) return 0;\n  return 1;\n}}\n'''
    with tempfile.TemporaryDirectory(prefix="thandor_gui_resource_compile_") as td:
        p = Path(td) / "probe.cpp"
        p.write_text(source, encoding="utf-8")
        run = subprocess.run([cxx, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-fsyntax-only", str(p)],
                             text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if (run.returncode == 0) != expect_success:
        fail(f"compile expectation failed for {expr!r}: rc={run.returncode}\n{run.stderr}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", type=Path, required=True)
    args = ap.parse_args()
    root = args.source.resolve()
    editor = (root / "src/editor/editor_app.cpp").read_text(encoding="utf-8")
    begin = editor.find('ImGui::SeparatorText("Starting resources by active faction")')
    end = editor.find('ImGui::SeparatorText("Placement readiness")', begin)
    if begin < 0 or end < 0:
        fail("starting-resource GUI block not found")
    block = editor[begin:end]
    if "building_units" in block or "energy_units" in block:
        fail("superseded starting-resource local remains in GUI block")
    for token in ["xenite_units", "tritium_units", '"Starts with no Xenite or Tritium."']:
        if token not in block:
            fail(f"required semantic token missing: {token}")
    m = re.search(r'if\(([^\n]+)\)\s*\n\s*ImGui::(?:TextColored|TextDisabled)\([^\n]*"Starts with no Xenite or Tritium\."\);', block)
    if not m:
        fail("zero-resource condition not found beside current message")
    expr = m.group(1).strip()
    cxx = shutil.which("c++") or shutil.which("g++") or shutil.which("clang++")
    if not cxx:
        fail("C++ compiler not found")
    compile_expr(cxx, expr, True)
    compile_expr(cxx, "building_units==0&&energy_units==0", False)
    print("GUI_STARTING_RESOURCE_SEMANTIC_RENAME_COMPILE PASS")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
