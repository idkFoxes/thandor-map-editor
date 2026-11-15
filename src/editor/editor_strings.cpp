#include "editor_strings.hpp"

#include <atomic>
#include <cctype>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace editor {
namespace {
struct Translation {
    std::string text;
    std::string german_label;
    std::string english_label;
};
std::map<std::string, Translation, std::less<>> strings;
std::atomic_bool german{};
std::atomic_bool loaded{};

std::string unescape(std::string_view value) {
    std::string decoded;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\' || i + 1 == value.size()) {
            decoded += value[i];
            continue;
        }
        switch (value[++i]) {
        case 'n': decoded += '\n'; break;
        case 'r': decoded += '\r'; break;
        case 't': decoded += '\t'; break;
        case 's': decoded += ' '; break;
        default: decoded += value[i]; break;
        }
    }
    return decoded;
}

std::vector<std::string_view> format_fields(std::string_view value) {
    std::vector<std::string_view> fields;
    constexpr std::string_view modifiers = "-+ #0'.*0123456789hljztLI";
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '%') continue;
        if (i && i + 1 < value.size() && std::isdigit(static_cast<unsigned char>(value[i - 1])) &&
            std::isspace(static_cast<unsigned char>(value[i + 1]))) continue;
        const auto start = i++;
        if (i == value.size()) break;
        if (value[i] == '%') continue;
        while (i < value.size() && modifiers.find(value[i]) != std::string_view::npos) ++i;
        if (i < value.size() && std::string_view("diuoxXfFeEgGaAcspn").find(value[i]) != std::string_view::npos)
            fields.push_back(value.substr(start, i - start + 1));
        else i = start;
    }
    return fields;
}
}

void set_editor_language(std::string_view language, const std::filesystem::path& executable_dir) {
    if (loaded) {
        german = language == "de";
        return;
    }
    std::ifstream file(executable_dir / "assets/localization/editor.tsv");
#ifdef THANDOR_EDITOR_LOCALE_DIR
    if (!file) file = std::ifstream(std::filesystem::path(THANDOR_EDITOR_LOCALE_DIR) / "editor.tsv");
#endif
    for (std::string line; std::getline(file, line);) {
        if (line.empty() || line.front() == '#') continue;
        if (line.back() == '\r') line.pop_back();
        const auto separator = line.find('\t');
        if (separator == std::string::npos) continue;
        auto english = unescape(std::string_view(line).substr(0, separator));
        auto translated = unescape(std::string_view(line).substr(separator + 1));
        if (translated.empty() || english == translated || format_fields(english) != format_fields(translated))
            continue;
        const auto suffix = english.find("##");
        const auto id = "###" + (suffix != std::string::npos && english.compare(suffix, 3, "###") == 0
            ? english.substr(suffix + 3) : english);
        const auto english_label = english.substr(0, suffix) + id;
        const auto german_label = translated.substr(0, translated.find("##")) + id;
        strings.emplace(std::move(english), Translation{std::move(translated), german_label, english_label});
    }
    loaded = true;
    german = language == "de";
}

std::string_view editor_language() noexcept { return german ? "de" : "en"; }
unsigned editor_text_country() noexcept { return german ? 49U : 44U; }

const char* tr(const char* english) {
    if (!german || !english) return english;
    const auto entry = strings.find(english);
    return entry == strings.end() ? english : entry->second.text.c_str();
}

const char* tr_label(const char* english) {
    if (!loaded || !english) return english;
    const auto entry = strings.find(english);
    if (entry == strings.end()) return english;
    return german ? entry->second.german_label.c_str() : entry->second.english_label.c_str();
}

}
