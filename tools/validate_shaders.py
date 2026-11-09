#!/usr/bin/env python3
"""Validate all editor GLSL sources and compile them with glslc when requested."""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

SHADERS = (
    "directional_shadow.vert", "directional_shadow.frag",
    "terrain.vert", "terrain.frag",
    "surface.vert", "surface.frag",
    "water.vert", "water.frag",
    "markers.vert", "markers.frag",
    "model.vert", "model.frag", "shadow.frag",
    "resource_glow.vert", "resource_glow.frag",
)

# GLSL words reserved for the language or for future revisions. They may be
# syntax tokens, but they may never be user-defined variable/member names.
RESERVED_IDENTIFIERS = {
    "active", "asm", "attribute", "cast", "class", "common", "filter",
    "fixed", "fvec2", "fvec3", "fvec4", "goto", "half", "hvec2",
    "hvec3", "hvec4", "inline", "input", "interface", "long", "namespace",
    "noinline", "output", "partition", "public", "sampler3DRect",
    "short", "sizeof", "static", "superp", "template", "this", "typedef",
    "union", "unsigned", "using", "volatile",
}

TYPE_PATTERN = re.compile(
    r"\b(?:bool|int|uint|float|double|[biud]?vec[234]|mat[234](?:x[234])?|"
    r"sampler\w*|image\w*|atomic_uint)\s+([A-Za-z_]\w*)\b"
)
BLOCK_PATTERN = re.compile(r"\}\s*([A-Za-z_]\w*)\s*;")
INTERFACE_PATTERN = re.compile(
    r"layout\s*\(\s*location\s*=\s*(\d+)\s*\)\s*"
    r"(?P<qualifiers>(?:(?:flat|smooth|noperspective|centroid|sample|patch|invariant)\s+)*)"
    r"(?P<direction>in|out)\s+(?P<type>[A-Za-z_]\w*)\s+(?P<name>[A-Za-z_]\w*)"
)
INTERPOLATION_QUALIFIERS = {"flat", "smooth", "noperspective"}


def strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", " ", source, flags=re.S)
    return re.sub(r"//[^\n]*", " ", source)


def validate_identifiers(path: Path) -> None:
    source = strip_comments(path.read_text(encoding="utf-8", errors="strict"))
    declared = [match.group(1) for match in TYPE_PATTERN.finditer(source)]
    declared.extend(match.group(1) for match in BLOCK_PATTERN.finditer(source))
    invalid = sorted({name for name in declared if name in RESERVED_IDENTIFIERS})
    if invalid:
        joined = ", ".join(invalid)
        raise RuntimeError(f"{path.name}: reserved GLSL identifier(s): {joined}")


def interface_signature(path: Path, direction: str) -> dict[int, tuple[str, str]]:
    source = strip_comments(path.read_text(encoding="utf-8", errors="strict"))
    result: dict[int, tuple[str, str]] = {}
    for match in INTERFACE_PATTERN.finditer(source):
        if match.group("direction") != direction:
            continue
        location = int(match.group(1))
        qualifiers = set(match.group("qualifiers").split())
        interpolation = next((q for q in INTERPOLATION_QUALIFIERS if q in qualifiers), "smooth")
        signature = (match.group("type"), interpolation)
        if location in result:
            raise RuntimeError(f"{path.name}: duplicate interface location {location}")
        result[location] = signature
    return result


def validate_stage_interfaces(shader_dir: Path) -> None:
    for stem in ("directional_shadow", "terrain", "surface", "water", "markers", "model", "resource_glow"):
        vertex = shader_dir / f"{stem}.vert"
        fragment = shader_dir / f"{stem}.frag"
        vertex_outputs = interface_signature(vertex, "out")
        fragment_inputs = interface_signature(fragment, "in")
        if vertex_outputs != fragment_inputs:
            raise RuntimeError(
                f"{stem}: vertex/fragment interface mismatch: "
                f"vertex={vertex_outputs}, fragment={fragment_inputs}"
            )


def compile_shader(glslc: Path, shader: Path, output: Path) -> None:
    output.parent.mkdir(parents=True, exist_ok=True)
    command = [str(glslc), str(shader), "-o", str(output)]
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT)
    if completed.returncode != 0:
        output.unlink(missing_ok=True)
        raise RuntimeError(
            f"glslc failed for {shader.name} (exit {completed.returncode})\n"
            f"{completed.stdout.rstrip()}"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--shader-dir", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--glslc", type=Path)
    parser.add_argument("--static-only", action="store_true")
    args = parser.parse_args()

    shader_dir = args.shader_dir.resolve()
    missing = [name for name in SHADERS if not (shader_dir / name).is_file()]
    if missing:
        raise SystemExit("missing shader source(s): " + ", ".join(missing))

    for name in SHADERS:
        validate_identifiers(shader_dir / name)
    validate_stage_interfaces(shader_dir)

    if args.static_only:
        print(f"Shader source validation PASS: {len(SHADERS)}/{len(SHADERS)}")
        return 0

    glslc = args.glslc
    if glslc is None:
        found = shutil.which("glslc")
        if found:
            glslc = Path(found)
    if glslc is None or not glslc.is_file():
        raise SystemExit("glslc is required unless --static-only is used")
    if args.output_dir is None:
        raise SystemExit("--output-dir is required when compiling shaders")

    output_dir = args.output_dir.resolve()
    for name in SHADERS:
        compile_shader(glslc.resolve(), shader_dir / name, output_dir / f"{name}.spv")

    print(f"Shader compile validation PASS: {len(SHADERS)}/{len(SHADERS)}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        raise SystemExit(1)
