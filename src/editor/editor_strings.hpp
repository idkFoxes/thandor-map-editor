#pragma once

#include <filesystem>
#include <string_view>

namespace editor {

void set_editor_language(std::string_view language, const std::filesystem::path& executable_dir);
std::string_view editor_language() noexcept;
unsigned editor_text_country() noexcept;
const char* tr(const char* english);
// Keep Dear ImGui IDs stable across languages and preserve hidden label suffixes.
const char* tr_label(const char* english);

}
