#!/usr/bin/env python3
"""Regression fixtures for all supported Dear ImGui Vulkan backend API families."""
from __future__ import annotations

import importlib.util
import sys
import tempfile
from pathlib import Path

sys.dont_write_bytecode = True

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("detect_imgui_vulkan_api", ROOT / "tools/detect_imgui_vulkan_api.py")
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)

COMMON_PREFIX = """
struct ImGui_ImplVulkan_InitInfo {
    VkInstance Instance;
    VkPhysicalDevice PhysicalDevice;
    VkDevice Device;
    uint32_t QueueFamily;
    VkQueue Queue;
    VkDescriptorPool DescriptorPool;
"""

FIXTURES = {
    "v1_90_9_render_pass_init_info": (
        COMMON_PREFIX + """
    VkRenderPass RenderPass;
    uint32_t MinImageCount;
    uint32_t ImageCount;
    VkSampleCountFlagBits MSAASamples;
    VkPipelineCache PipelineCache;
    uint32_t Subpass;
};
bool ImGui_ImplVulkan_Init(ImGui_ImplVulkan_InitInfo* info);
bool ImGui_ImplVulkan_CreateFontsTexture();
void ImGui_ImplVulkan_DestroyFontsTexture();
""",
        "render_pass_init_info",
        "automatic_or_zero_argument",
    ),
    "pipeline_info_main": (
        COMMON_PREFIX + """
    uint32_t ApiVersion;
    struct { VkRenderPass RenderPass; uint32_t Subpass; VkSampleCountFlagBits MSAASamples; } PipelineInfoMain;
};
bool ImGui_ImplVulkan_Init(ImGui_ImplVulkan_InitInfo* info);
bool ImGui_ImplVulkan_CreateFontsTexture();
""",
        "pipeline_info_main",
        "automatic_or_zero_argument",
    ),
    "legacy_two_argument": (
        COMMON_PREFIX + """
    uint32_t MinImageCount;
    uint32_t ImageCount;
    VkSampleCountFlagBits MSAASamples;
    uint32_t Subpass;
};
bool ImGui_ImplVulkan_Init(ImGui_ImplVulkan_InitInfo* info, VkRenderPass render_pass);
bool ImGui_ImplVulkan_CreateFontsTexture(VkCommandBuffer command_buffer);
void ImGui_ImplVulkan_DestroyFontUploadObjects();
""",
        "legacy_two_argument",
        "manual_command_buffer",
    ),
}


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="thandor-imgui-api-") as temp:
        base = Path(temp)
        for name, (text, expected_api, expected_font) in FIXTURES.items():
            header = base / f"{name}.h"
            header.write_text(text, encoding="utf-8")
            result = MODULE.detect(header)
            assert result["api_family"] == expected_api, (name, result)
            assert result["font_upload_family"] == expected_font, (name, result)

        mixed = base / "mixed.h"
        mixed.write_text(
            COMMON_PREFIX + "VkRenderPass RenderPass; struct { VkRenderPass RenderPass; } PipelineInfoMain; };\n"
            "bool ImGui_ImplVulkan_Init(ImGui_ImplVulkan_InitInfo* info);\n"
            "bool ImGui_ImplVulkan_Init(ImGui_ImplVulkan_InitInfo* info, VkRenderPass render_pass);\n",
            encoding="utf-8",
        )
        try:
            MODULE.detect(mixed)
        except ValueError:
            pass
        else:
            raise AssertionError("mixed initialization API was not rejected")

    print("Dear ImGui Vulkan API detector regression PASS: 4/4")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
