#!/usr/bin/env python3
"""Validate that the G320 resource renderer and VulkanContext getter contract is complete."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import NoReturn

SIGNATURE = "[[nodiscard]] bool fill_mode_non_solid_supported() const noexcept"
CALL = "context_.fill_mode_non_solid_supported()"
MEMBER = "bool fill_mode_non_solid_supported_{};"
RETURN = "return fill_mode_non_solid_supported_;"


def fail(message: str) -> NoReturn:
    raise SystemExit(f"G320c validation failed: {message}")


def class_body(text: str) -> str:
    start = text.find("class VulkanContext")
    if start < 0:
        fail("class VulkanContext not found")
    opening = text.find("{", start)
    if opening < 0:
        fail("VulkanContext opening brace not found")
    depth = 0
    for index in range(opening, len(text)):
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
            if depth == 0:
                return text[opening + 1 : index]
    fail("VulkanContext closing brace not found")


def public_signature(body: str) -> bool:
    access = "private"
    for line in body.splitlines():
        stripped = line.strip()
        if stripped in {"public:", "protected:", "private:"}:
            access = stripped[:-1]
        if SIGNATURE in line:
            return access == "public"
    return False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    header_path = source / "src/editor/vulkan_context.hpp"
    renderer_path = source / "src/editor/terrain_renderer.cpp"
    if not header_path.is_file() or not renderer_path.is_file():
        fail("source root does not contain the expected editor files")
    header = header_path.read_text(encoding="utf-8")
    renderer = renderer_path.read_text(encoding="utf-8")
    body = class_body(header)
    checks = {
        "renderer_calls_capability_getter": CALL in renderer,
        "getter_signature_present_once": header.count(SIGNATURE) == 1,
        "getter_is_public": public_signature(body),
        "getter_returns_backing_member": RETURN in body,
        "backing_member_present_once": header.count(MEMBER) == 1,
        "no_direct_private_member_access_from_renderer": "context_.fill_mode_non_solid_supported_" not in renderer,
    }
    failed = [name for name, passed in checks.items() if not passed]
    if failed:
        fail(", ".join(failed))
    result = {
        "schema": "thandor-map-editor-g320c-vulkan-getter-contract-v1",
        "source": str(source),
        "checks": checks,
        "status": "PASS",
    }
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("G320c VulkanContext getter contract PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
