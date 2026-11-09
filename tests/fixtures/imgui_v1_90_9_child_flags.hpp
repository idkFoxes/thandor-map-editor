#pragma once

// Minimal compile fixture copied from the pinned Dear ImGui 1.90.9 public API naming contract.
typedef int ImGuiChildFlags;
enum ImGuiChildFlags_ {
    ImGuiChildFlags_None = 0,
    ImGuiChildFlags_Border = 1 << 0,
    ImGuiChildFlags_AlwaysUseWindowPadding = 1 << 1,
    ImGuiChildFlags_ResizeX = 1 << 2,
    ImGuiChildFlags_ResizeY = 1 << 3,
    ImGuiChildFlags_AutoResizeX = 1 << 4,
    ImGuiChildFlags_AutoResizeY = 1 << 5,
    ImGuiChildFlags_AlwaysAutoResize = 1 << 6,
    ImGuiChildFlags_FrameStyle = 1 << 7,
};
