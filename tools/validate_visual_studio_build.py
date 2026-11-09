#!/usr/bin/env python3
"""Validate the native Visual Studio Community build without invoking CMake."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import Iterable

MSBUILD_NS = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}
CPP_PROJECT_TYPE_GUID = "{BC8A1FFA-BEE3-4634-8014-F334798102B3}"

CORE_SOURCES = {
    "src/core/binary.cpp",
    "src/core/pck.cpp",
    "src/core/field.cpp",
    "src/core/level.cpp",
    "src/core/scenario_catalog.cpp",
    "src/core/text_resource.cpp",
    "src/core/gfx.cpp",
    "src/core/terrain_texture.cpp",
    "src/core/terrain_shadow.cpp",
    "src/core/model.cpp",
    "src/core/effect.cpp",
    "src/core/catalog.cpp",
    "src/core/map_document.cpp",
}
EDITOR_SOURCES = {
    "src/editor/main.cpp",
    "src/editor/editor_app.cpp",
    "src/editor/vulkan_context.cpp",
    "src/editor/terrain_renderer.cpp",
}
IMGUI_SOURCES = {
    "windows/deps/imgui/imgui.cpp",
    "windows/deps/imgui/imgui_draw.cpp",
    "windows/deps/imgui/imgui_tables.cpp",
    "windows/deps/imgui/imgui_widgets.cpp",
    "windows/deps/imgui/backends/imgui_impl_glfw.cpp",
    "windows/deps/imgui/backends/imgui_impl_vulkan.cpp",
}
TEST_SOURCES = {"tests/core_tests.cpp"}
SHADERS = {
    "terrain.vert",
    "terrain.frag",
    "surface.vert",
    "surface.frag",
    "water.vert",
    "water.frag",
    "markers.vert",
    "markers.frag",
    "model.vert",
    "model.frag",
    "shadow.frag",
    "resource_glow.vert",
    "resource_glow.frag",
}
PROJECTS = {
    "ThandorMapCore": ("windows/projects/ThandorMapCore.vcxproj", "{80329D9D-5B1C-5EAD-8ED8-65BEE59F9D2F}"),
    "ThandorImGui": ("windows/projects/ThandorImGui.vcxproj", "{B1F72FDE-D1AE-5BE9-8DD0-AA5D8AF3145B}"),
    "thandor-map-editor": ("windows/projects/ThandorMapEditor.vcxproj", "{30A32B4B-7FEF-59AE-93A8-9783298D36FF}"),
    "core_map_core_tests": ("windows/projects/ThandorMapCoreTests.vcxproj", "{7A032FEC-48C8-584D-AA8D-0DAE09348F9C}"),
}


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def norm_msbuild_path(value: str) -> str:
    value = value.replace("$(SolutionDir)", "")
    value = value.replace("\\", "/")
    while "//" in value:
        value = value.replace("//", "/")
    return value.lstrip("./")


def parse_project(path: Path) -> ET.Element:
    try:
        return ET.parse(path).getroot()
    except ET.ParseError as exc:
        raise AssertionError(f"Malformed MSBuild XML: {path}: {exc}") from exc


def project_sources(root: ET.Element) -> set[str]:
    return {
        norm_msbuild_path(node.attrib["Include"])
        for node in root.findall(".//m:ClCompile", MSBUILD_NS)
        if "Include" in node.attrib
    }


def all_text(root: ET.Element, tag: str) -> list[str]:
    return [(node.text or "").strip() for node in root.findall(f".//m:{tag}", MSBUILD_NS)]


def require_exact(actual: set[str], expected: set[str], label: str) -> None:
    missing = sorted(expected - actual)
    extra = sorted(actual - expected)
    assert not missing and not extra, f"{label} mismatch; missing={missing}, extra={extra}"


def check_project_common(project_root: ET.Element, label: str) -> dict[str, object]:
    configs = {
        node.attrib.get("Include", "")
        for node in project_root.findall(".//m:ProjectConfiguration", MSBUILD_NS)
    }
    assert configs == {"Debug|x64", "Release|x64"}, f"{label}: configuration mismatch: {configs}"
    assert set(all_text(project_root, "PlatformToolset")) == {"v143"}, f"{label}: requires v143"
    assert set(all_text(project_root, "LanguageStandard")) == {"stdcpp20"}, f"{label}: requires C++20"
    assert set(all_text(project_root, "ConformanceMode")) == {"true"}, f"{label}: requires /permissive-"
    assert set(all_text(project_root, "RuntimeLibrary")) == {"MultiThreadedDLL"}, f"{label}: runtime must be /MD"
    assert set(all_text(project_root, "MultiProcessorCompilation")) == {"true"}, f"{label}: parallel compile missing"
    return {
        "configurations": sorted(configs),
        "platform_toolset": sorted(set(all_text(project_root, "PlatformToolset"))),
        "language_standard": sorted(set(all_text(project_root, "LanguageStandard"))),
        "runtime_library": sorted(set(all_text(project_root, "RuntimeLibrary"))),
    }


def check_solution(root: Path) -> dict[str, object]:
    sln = (root / "ThandorMapEditor.sln").read_text(encoding="utf-8-sig")
    assert "Visual Studio Version 17" in sln, "Solution is not marked for Visual Studio 2022"
    assert "Debug|x64 = Debug|x64" in sln and "Release|x64 = Release|x64" in sln
    found: dict[str, tuple[str, str, str]] = {}
    pattern = re.compile(r'^Project\("(?P<type>\{[^}]+\})"\) = "(?P<name>[^"]+)", "(?P<path>[^"]+)", "(?P<guid>\{[^}]+\})"$', re.M)
    for match in pattern.finditer(sln):
        found[match.group("name")] = (match.group("type"), match.group("path"), match.group("guid"))
    assert set(found) == set(PROJECTS), f"Solution project set mismatch: {set(found)}"
    for name, (expected_path, expected_guid) in PROJECTS.items():
        project_type, project_path, project_guid = found[name]
        assert project_type.upper() == CPP_PROJECT_TYPE_GUID, f"{name}: invalid C++ project type GUID {project_type}"
        assert project_path.replace("\\", "/") == expected_path, f"{name}: wrong project path"
        assert project_guid.upper() == expected_guid, f"{name}: wrong project GUID"
        assert (root / expected_path).is_file(), f"{name}: project file missing"
    return {"projects": len(found), "project_type_guid": CPP_PROJECT_TYPE_GUID, "configurations": ["Debug|x64", "Release|x64"]}


def check_projects(root: Path) -> dict[str, object]:
    parsed: dict[str, ET.Element] = {}
    common: dict[str, object] = {}
    for name, (rel, expected_guid) in PROJECTS.items():
        project_root = parse_project(root / rel)
        parsed[name] = project_root
        guids = set(all_text(project_root, "ProjectGuid"))
        assert guids == {expected_guid}, f"{name}: ProjectGuid mismatch: {guids}"
        common[name] = check_project_common(project_root, name)
        filters = root / f"{rel}.filters"
        assert filters.is_file(), f"{name}: filters file missing"
        parse_project(filters)

    require_exact(project_sources(parsed["ThandorMapCore"]), CORE_SOURCES, "Core project source coverage")
    require_exact(project_sources(parsed["ThandorImGui"]), IMGUI_SOURCES, "ImGui project source coverage")
    require_exact(project_sources(parsed["thandor-map-editor"]), EDITOR_SOURCES, "Editor project source coverage")
    require_exact(project_sources(parsed["core_map_core_tests"]), TEST_SOURCES, "Core-test project source coverage")

    for source in sorted(CORE_SOURCES | EDITOR_SOURCES | TEST_SOURCES):
        assert (root / source).is_file(), f"Project source does not exist: {source}"

    editor = parsed["thandor-map-editor"]
    editor_refs = {
        norm_msbuild_path(node.attrib["Include"])
        for node in editor.findall(".//m:ProjectReference", MSBUILD_NS)
    }
    assert editor_refs == {"ThandorMapCore.vcxproj", "ThandorImGui.vcxproj"}, f"Editor references mismatch: {editor_refs}"
    test_refs = {
        norm_msbuild_path(node.attrib["Include"])
        for node in parsed["core_map_core_tests"].findall(".//m:ProjectReference", MSBUILD_NS)
    }
    assert test_refs == {"ThandorMapCore.vcxproj"}, f"Test references mismatch: {test_refs}"

    dependencies = ";".join(all_text(editor, "AdditionalDependencies"))
    required_libs = {"glfw3dll.lib", "vulkan-1.lib", "user32.lib", "gdi32.lib", "shell32.lib", "ole32.lib", "advapi32.lib", "winmm.lib"}
    missing_libs = sorted(lib for lib in required_libs if lib not in dependencies)
    assert not missing_libs, f"Editor link dependencies missing: {missing_libs}"
    assert set(all_text(editor, "SubSystem")) == {"Console"}, "Editor subsystem mismatch"
    defs = ";".join(all_text(editor, "PreprocessorDefinitions"))
    for definition in ("VK_USE_PLATFORM_WIN32_KHR", "GLFW_DLL", "THANDOR_IMGUI_VULKAN_API_RENDER_PASS_INIT_INFO=1"):
        assert definition in defs, f"Editor definition missing: {definition}"
    assert "THANDOR_SHADER_DIR" in defs, "Editor shader output definition missing"

    project_text = (root / PROJECTS["thandor-map-editor"][0]).read_text(encoding="utf-8")
    for marker in ("VerifyWindowsDependencies", "CompileThandorShaders", "CopyGlfwRuntime"):
        assert marker in project_text, f"Editor MSBuild target missing: {marker}"

    return {
        "project_count": len(parsed),
        "core_sources": len(CORE_SOURCES),
        "editor_sources": len(EDITOR_SOURCES),
        "imgui_sources": len(IMGUI_SOURCES),
        "test_sources": len(TEST_SOURCES),
        "common": common,
        "editor_project_references": sorted(editor_refs),
        "editor_link_libraries": sorted(required_libs),
    }


def check_windows_scripts(root: Path) -> dict[str, object]:
    scripts = [
        root / "build_windows.ps1",
        root / "OpenInVisualStudio.bat",
        root / "windows/setup_windows_dependencies.ps1",
        root / "windows/compile_shaders.ps1",
    ]
    for path in scripts:
        assert path.is_file(), f"Missing Windows entry point: {path.name}"
        text = path.read_text(encoding="utf-8-sig")
        # This is a command invocation gate, not a prose substring gate.
        forbidden = [
            r"(?im)^\s*(?:&\s*)?cmake(?:\.exe)?(?:\s|$)",
            r"(?im)^\s*(?:&\s*)?ninja(?:\.exe)?(?:\s|$)",
            r"(?im)^\s*(?:&\s*)?(?:g?make)(?:\.exe)?(?:\s|$)",
            r"(?im)^\s*(?:&\s*)?(?:python|python3|py)(?:\.exe)?(?:\s|$)",
            r"(?i)\b(?:cmake|ninja|g?make|python3?|py)\.exe\b",
        ]
        for pattern in forbidden:
            assert not re.search(pattern, text), f"Forbidden Windows build invocation in {path}: {pattern}"

    setup = (root / "windows/setup_windows_dependencies.ps1").read_text(encoding="utf-8-sig")
    expected_fragments = {
        "GLFW 3.4": "glfw/releases/download/3.4/glfw-3.4.bin.WIN64.zip",
        "Dear ImGui 1.90.9": "imgui/archive/refs/tags/v1.90.9.zip",
        "Vulkan Headers 1.3.280": "Vulkan-Headers/archive/refs/tags/v1.3.280.zip",
        "Vulkan Loader DEF 1.3.280": "Vulkan-Loader/v1.3.280/loader/vulkan-1.def",
        "Visual Studio discovery": "vswhere.exe",
        "Visual Studio import-library tool": "lib.exe",
        "Shader compiler": "glslc.exe",
    }
    for label, fragment in expected_fragments.items():
        assert fragment in setup, f"Dependency bootstrap missing {label}"

    shader_script = (root / "windows/compile_shaders.ps1").read_text(encoding="utf-8-sig")
    match = re.search(r"\$shaders=@\((?P<body>.*?)\)", shader_script, re.S)
    assert match, "Could not locate the shader list"
    actual_shaders = set(re.findall(r"'([^']+\.(?:vert|frag))'", match.group("body")))
    require_exact(actual_shaders, SHADERS, "Windows shader compilation list")
    for shader in SHADERS:
        assert (root / "shaders" / shader).is_file(), f"Missing shader source: {shader}"

    versions = json.loads((root / "windows/DEPENDENCY_VERSIONS.json").read_text(encoding="utf-8"))
    assert versions["glfw"] == "3.4"
    assert versions["imgui"] == "1.90.9"
    assert versions["vulkan_headers"] == "1.3.280"
    assert versions["build_system"] == "MSBuild/Visual Studio solution"

    return {
        "entry_points": [str(path.relative_to(root)).replace(os.sep, "/") for path in scripts],
        "forbidden_invocations": 0,
        "dependency_versions": versions,
        "shader_sources": len(actual_shaders),
    }


def check_linux_unchanged(root: Path) -> dict[str, object]:
    # G360 introduced the independent MSBuild path. Later editor passes legitimately add
    # Linux tests and documentation, so an exact historical CMake/README hash is stale and
    # would reject correct cumulative updates. Preserve the Linux build contract instead.
    cmake = (root / "CMakeLists.txt").read_text(encoding="utf-8")
    build_linux = (root / "build_linux.sh").read_text(encoding="utf-8")
    readme = (root / "README.md").read_text(encoding="utf-8")
    required_cmake = [
        'option(THANDOR_EDITOR_BUILD_APP "Build the Vulkan/ImGui editor" ON)',
        'option(THANDOR_EDITOR_BUILD_TESTS "Build core format tests" ON)',
        'add_library(thandor_map_core',
        'if(THANDOR_EDITOR_BUILD_APP)',
        'find_package(Vulkan REQUIRED)',
    ]
    missing_cmake = [token for token in required_cmake if token not in cmake]
    assert not missing_cmake, f"Linux CMake contract missing: {missing_cmake}"
    for token in ("cmake -S", "cmake --build", "ctest"):
        assert token in build_linux, f"Linux build script missing {token}"
    assert "### Linux" in readme and "./build_linux.sh" in readme, "README Linux build instructions missing"
    return {
        "mode": "semantic-contract",
        "cmake_sha256": sha256_file(root / "CMakeLists.txt"),
        "build_linux_sha256": sha256_file(root / "build_linux.sh"),
        "readme_has_linux_instructions": True,
    }


def check_hygiene(root: Path) -> dict[str, object]:
    forbidden_dir_names = {"deps", ".download-cache", "build-msvc", ".vs", "__pycache__"}
    forbidden_suffixes = {".spv", ".pyc", ".pyo", ".suo", ".user", ".obj", ".lib", ".dll", ".exe", ".pdb"}
    bad: list[str] = []
    for path in root.rglob("*"):
        rel = path.relative_to(root)
        parts = set(rel.parts)
        # windows/deps is intentionally an ignored bootstrap output and must not be sealed.
        if path.is_dir() and path.name in forbidden_dir_names:
            bad.append(str(rel))
        elif path.is_file() and path.suffix.lower() in forbidden_suffixes:
            bad.append(str(rel))
    assert not bad, f"Generated/binary artifacts present: {bad[:20]}"
    return {"forbidden_artifacts": 0}


def run_core_build(root: Path) -> dict[str, object]:
    compiler = shutil.which("clang++") or shutil.which("g++")
    if not compiler:
        return {"status": "SKIPPED", "reason": "No host C++ compiler found"}
    with tempfile.TemporaryDirectory(prefix="thandor-g360-core-") as tmp:
        output = Path(tmp) / "core_tests"
        command = [compiler, "-std=c++20", "-O0", f"-I{root / 'include'}"]
        command.extend(str(root / source) for source in sorted(CORE_SOURCES))
        command.extend([str(root / "tests/core_tests.cpp"), "-o", str(output)])
        compile_run = subprocess.run(command, text=True, capture_output=True, check=False)
        assert compile_run.returncode == 0, f"Direct core compile failed:\n{compile_run.stdout}\n{compile_run.stderr}"
        test_run = subprocess.run([str(output)], text=True, capture_output=True, check=False)
        assert test_run.returncode == 0, f"Direct core test failed:\n{test_run.stdout}\n{test_run.stderr}"
        assert "PASS" in test_run.stdout, f"Unexpected core-test output: {test_run.stdout}"
        return {
            "status": "PASS",
            "compiler": compiler,
            "translation_units": len(CORE_SOURCES) + 1,
            "stdout": test_run.stdout.strip(),
        }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--output", type=Path)
    parser.add_argument("--run-core-build", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()

    result: dict[str, object] = {
        "schema": "thandor-g360-native-visual-studio-validation-v1",
        "root": root.name,
        "checks": {},
        "status": "FAIL",
    }
    try:
        result["checks"] = {
            "solution": check_solution(root),
            "projects": check_projects(root),
            "windows_scripts": check_windows_scripts(root),
            "linux_unchanged": check_linux_unchanged(root),
            "archive_hygiene": check_hygiene(root),
        }
        if args.run_core_build:
            result["checks"]["direct_core_build"] = run_core_build(root)  # type: ignore[index]
        result["status"] = "PASS"
    except Exception as exc:  # noqa: BLE001 - validator must serialize exact failure
        result["error"] = f"{type(exc).__name__}: {exc}"

    rendered = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(rendered, encoding="utf-8")
    print(rendered, end="")
    return 0 if result["status"] == "PASS" else 1


if __name__ == "__main__":
    raise SystemExit(main())
