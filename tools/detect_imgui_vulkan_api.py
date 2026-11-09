#!/usr/bin/env python3
"""Classify the resolved Dear ImGui Vulkan backend API without brittle exact-line matching."""
from __future__ import annotations

import argparse
import hashlib
import json
import re
from pathlib import Path


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    return re.sub(r"//[^\n]*", " ", text)


def has(pattern: str, text: str) -> bool:
    return re.search(pattern, text, flags=re.DOTALL | re.MULTILINE) is not None


def detect(header: Path) -> dict[str, object]:
    raw = header.read_text(encoding="utf-8", errors="strict")
    text = strip_comments(raw)
    struct_match = re.search(
        r"struct\s+ImGui_ImplVulkan_InitInfo\s*\{(?P<body>.*?)\}\s*;",
        text,
        flags=re.DOTALL,
    )
    if not struct_match:
        raise ValueError("ImGui_ImplVulkan_InitInfo structure was not found")
    body = struct_match.group("body")

    one_arg = has(
        r"\bImGui_ImplVulkan_Init\s*\(\s*ImGui_ImplVulkan_InitInfo\s*\*\s*(?:\w+)?\s*\)\s*;",
        text,
    )
    two_arg = has(
        r"\bImGui_ImplVulkan_Init\s*\(\s*ImGui_ImplVulkan_InitInfo\s*\*\s*(?:\w+)?\s*,\s*VkRenderPass(?:\s+\w+)?\s*\)\s*;",
        text,
    )
    pipeline_info = has(r"\bPipelineInfoMain\b", body)
    render_pass_field = has(r"\bVkRenderPass\s+RenderPass\s*;", body)
    api_version_field = has(r"\buint32_t\s+ApiVersion\s*;", body)

    if one_arg and not two_arg and pipeline_info:
        api_family = "pipeline_info_main"
        api_macro = "THANDOR_IMGUI_VULKAN_API_PIPELINE_INFO_MAIN=1"
    elif one_arg and not two_arg and render_pass_field and not pipeline_info:
        api_family = "render_pass_init_info"
        api_macro = "THANDOR_IMGUI_VULKAN_API_RENDER_PASS_INIT_INFO=1"
    elif two_arg and not one_arg and not pipeline_info:
        api_family = "legacy_two_argument"
        api_macro = "THANDOR_IMGUI_VULKAN_API_LEGACY_TWO_ARGUMENT=1"
    else:
        raise ValueError(
            "unsupported or mixed ImGui Vulkan initialization API: "
            f"one_arg={one_arg} two_arg={two_arg} pipeline_info={pipeline_info} "
            f"render_pass_field={render_pass_field}"
        )

    font_command_buffer = has(
        r"\bImGui_ImplVulkan_CreateFontsTexture\s*\(\s*VkCommandBuffer(?:\s+\w+)?\s*\)\s*;",
        text,
    )
    font_zero_argument = has(
        r"\bImGui_ImplVulkan_CreateFontsTexture\s*\(\s*\)\s*;",
        text,
    )
    destroy_upload_objects = has(r"\bImGui_ImplVulkan_DestroyFontUploadObjects\s*\(\s*\)\s*;", text)
    if font_command_buffer and destroy_upload_objects and not font_zero_argument:
        font_upload_family = "manual_command_buffer"
    elif font_zero_argument and not destroy_upload_objects and not font_command_buffer:
        font_upload_family = "automatic_or_zero_argument"
    elif not font_command_buffer and not font_zero_argument and not destroy_upload_objects:
        font_upload_family = "backend_internal"
    else:
        raise ValueError(
            "unsupported or mixed ImGui Vulkan font-upload API: "
            f"command_buffer={font_command_buffer} zero_argument={font_zero_argument} "
            f"destroy_upload_objects={destroy_upload_objects}"
        )

    return {
        "schema": "thandor-imgui-vulkan-api-detection-v1",
        "header": str(header.resolve()),
        "header_sha256": hashlib.sha256(raw.encode("utf-8")).hexdigest(),
        "api_family": api_family,
        "api_compile_definition": api_macro,
        "has_api_version_field": api_version_field,
        "font_upload_family": font_upload_family,
        "manual_font_upload": font_upload_family == "manual_command_buffer",
        "evidence": {
            "one_argument_init": one_arg,
            "two_argument_init": two_arg,
            "pipeline_info_main_field": pipeline_info,
            "render_pass_init_info_field": render_pass_field,
            "api_version_field": api_version_field,
            "font_command_buffer_signature": font_command_buffer,
            "font_zero_argument_signature": font_zero_argument,
            "destroy_font_upload_objects": destroy_upload_objects,
        },
    }


def cmake_bool(value: bool) -> str:
    return "ON" if value else "OFF"


def write_cmake(path: Path, result: dict[str, object]) -> None:
    lines = [
        f'set(THANDOR_IMGUI_VULKAN_API_FAMILY "{result["api_family"]}")',
        f'set(THANDOR_IMGUI_VULKAN_API_COMPILE_DEFINITION "{result["api_compile_definition"]}")',
        f'set(THANDOR_IMGUI_VULKAN_HAS_API_VERSION {cmake_bool(bool(result["has_api_version_field"]))})',
        f'set(THANDOR_IMGUI_VULKAN_MANUAL_FONT_UPLOAD {cmake_bool(bool(result["manual_font_upload"]))})',
        f'set(THANDOR_IMGUI_VULKAN_FONT_UPLOAD_FAMILY "{result["font_upload_family"]}")',
        f'set(THANDOR_IMGUI_VULKAN_HEADER_SHA256 "{result["header_sha256"]}")',
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--cmake-output", type=Path)
    parser.add_argument("--json-output", type=Path)
    args = parser.parse_args()
    try:
        result = detect(args.header)
    except (OSError, UnicodeError, ValueError) as exc:
        raise SystemExit(f"Dear ImGui Vulkan API detection failed: {exc}") from exc
    if args.cmake_output:
        write_cmake(args.cmake_output, result)
    if args.json_output:
        args.json_output.parent.mkdir(parents=True, exist_ok=True)
        args.json_output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
