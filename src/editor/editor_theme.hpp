#pragma once
#include <imgui.h>

namespace editor {
inline void apply_editor_theme() {
    ImGui::StyleColorsDark();
    auto& s = ImGui::GetStyle();
    s.WindowPadding = {12, 10}; s.FramePadding = {8, 5};
    s.ItemSpacing = {6, 7}; s.ItemInnerSpacing = {6, 4}; s.CellPadding = {8, 5};
    s.WindowRounding = 0; s.ChildRounding = 4; s.FrameRounding = 4;
    s.PopupRounding = 5; s.ScrollbarRounding = 5; s.GrabRounding = 4;
    s.TabRounding = 4; s.ScrollbarSize = 12;
    s.Colors[ImGuiCol_Text] = {0.87F, 0.91F, 0.97F, 1};
    s.Colors[ImGuiCol_TextDisabled] = {0.48F, 0.56F, 0.66F, 1};
    s.Colors[ImGuiCol_WindowBg] = {0.075F, 0.095F, 0.13F, 1};
    s.Colors[ImGuiCol_ChildBg] = {0.06F, 0.08F, 0.11F, 1};
    s.Colors[ImGuiCol_PopupBg] = {0.10F, 0.13F, 0.18F, 1};
    s.Colors[ImGuiCol_Border] = {0.19F, 0.25F, 0.33F, 1};
    s.Colors[ImGuiCol_FrameBg] = {0.11F, 0.15F, 0.20F, 1};
    s.Colors[ImGuiCol_FrameBgHovered] = {0.16F, 0.23F, 0.32F, 1};
    s.Colors[ImGuiCol_FrameBgActive] = {0.13F, 0.27F, 0.43F, 1};
    s.Colors[ImGuiCol_TitleBg] = s.Colors[ImGuiCol_WindowBg];
    s.Colors[ImGuiCol_TitleBgActive] = {0.10F, 0.14F, 0.20F, 1};
    s.Colors[ImGuiCol_MenuBarBg] = {0.08F, 0.11F, 0.15F, 1};
    s.Colors[ImGuiCol_Button] = {0.12F, 0.18F, 0.25F, 1};
    s.Colors[ImGuiCol_ButtonHovered] = {0.16F, 0.33F, 0.52F, 1};
    s.Colors[ImGuiCol_ButtonActive] = {0.12F, 0.42F, 0.75F, 1};
    s.Colors[ImGuiCol_Header] = {0.12F, 0.20F, 0.30F, 1};
    s.Colors[ImGuiCol_HeaderHovered] = {0.15F, 0.30F, 0.46F, 1};
    s.Colors[ImGuiCol_HeaderActive] = {0.12F, 0.40F, 0.70F, 1};
    s.Colors[ImGuiCol_Tab] = {0.09F, 0.13F, 0.18F, 1};
    s.Colors[ImGuiCol_TabHovered] = {0.15F, 0.34F, 0.55F, 1};
    s.Colors[ImGuiCol_TabActive] = {0.12F, 0.28F, 0.46F, 1};
    s.Colors[ImGuiCol_CheckMark] = {0.22F, 0.65F, 1.0F, 1};
    s.Colors[ImGuiCol_SliderGrab] = {0.18F, 0.55F, 0.91F, 1};
    s.Colors[ImGuiCol_SliderGrabActive] = {0.30F, 0.72F, 1.0F, 1};
    s.Colors[ImGuiCol_Separator] = {0.18F, 0.24F, 0.32F, 1};
    s.Colors[ImGuiCol_ResizeGrip] = {0.18F, 0.55F, 0.91F, 0.4F};
}
inline void apply_scaled_editor_theme(float scale) {
    // ScaleAllSizes mutates every metric, including ones not set by apply_editor_theme.
    // A fresh baseline prevents cumulative minimum-window sizes on monitor changes.
    ImGui::GetStyle()=ImGuiStyle{};
    apply_editor_theme();
    ImGui::GetStyle().ScaleAllSizes(scale);
}
} // namespace editor
