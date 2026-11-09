#!/usr/bin/env python3
"""Validate the G320b VulkanContext capability getter and its G320 caller."""
from __future__ import annotations

import argparse
import json
from pathlib import Path


def require(condition: bool, message: str) -> None:
    if not condition:
        raise SystemExit(message)


def class_slice(text: str) -> str:
    start = text.find("class VulkanContext")
    require(start >= 0, "VulkanContext class declaration not found")
    brace = text.find("{", start)
    require(brace >= 0, "VulkanContext opening brace not found")
    depth = 0
    for index in range(brace, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[brace:index]
    raise SystemExit("VulkanContext closing brace not found")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    header_path = source / "src/editor/vulkan_context.hpp"
    renderer_path = source / "src/editor/terrain_renderer.cpp"
    require(header_path.is_file(), f"missing {header_path}")
    require(renderer_path.is_file(), f"missing {renderer_path}")

    header = header_path.read_text(encoding="utf-8")
    renderer = renderer_path.read_text(encoding="utf-8")
    body = class_slice(header)
    getter = "[[nodiscard]] bool fill_mode_non_solid_supported() const noexcept"
    getter_pos = body.find(getter)
    member_pos = body.find("bool fill_mode_non_solid_supported_{};")
    public_pos = body.rfind("public:", 0, getter_pos if getter_pos >= 0 else 0)
    private_before_getter = body.rfind("private:", 0, getter_pos if getter_pos >= 0 else 0)

    checks = {
        "g320_caller_present": "context_.fill_mode_non_solid_supported()" in renderer,
        "private_capability_member_present": member_pos >= 0,
        "public_inline_getter_present": getter_pos >= 0,
        "getter_returns_private_capability": "return fill_mode_non_solid_supported_;" in body,
        "getter_is_in_public_section": public_pos >= 0 and public_pos > private_before_getter,
        "getter_precedes_private_member": getter_pos >= 0 and member_pos >= 0 and getter_pos < member_pos,
    }
    for name, passed in checks.items():
        require(passed, f"G320b getter contract failed: {name}")

    result = {
        "schema": "thandor-vulkan-map-editor-g320b-vulkan-capability-getter-v1",
        "source": str(source),
        "checks": checks,
        "status": "PASS",
    }
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
