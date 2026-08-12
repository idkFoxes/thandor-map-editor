#pragma once

struct ImVec2 {
    float x;
    float y;
    constexpr ImVec2(float x_value = 0.0F, float y_value = 0.0F) noexcept
        : x(x_value), y(y_value) {}
};

using ImGuiTableFlags = int;
inline constexpr ImGuiTableFlags ImGuiTableFlags_Borders = 1 << 0;
inline constexpr ImGuiTableFlags ImGuiTableFlags_RowBg = 1 << 1;
inline constexpr ImGuiTableFlags ImGuiTableFlags_ScrollY = 1 << 2;

namespace ImGui {
void TableNextRow();
void TableSetColumnIndex(int column_n);
void Text(const char* fmt, ...);
void TextUnformatted(const char* text, const char* text_end = nullptr);
bool IsItemHovered();
void SetTooltip(const char* fmt, ...);
void TextDisabled(const char* fmt, ...);
void SeparatorText(const char* label);
bool BeginCombo(const char* label, const char* preview_value);
bool Selectable(const char* label, bool selected = false);
void SetItemDefaultFocus();
void EndCombo();
bool Checkbox(const char* label, bool* value);
void SameLine();
void TextWrapped(const char* fmt, ...);
bool BeginTable(const char* str_id, int columns, ImGuiTableFlags flags = 0,
                const ImVec2& outer_size = ImVec2{}, float inner_width = 0.0F);
void TableSetupColumn(const char* label);
void TableHeadersRow();
void EndTable();
} // namespace ImGui
