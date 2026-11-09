#include "thandor/catalog.hpp"
#include "thandor/text_resource.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

// Read-only stock name/group crosswalk. Build against thandor_map_core:
// c++ -std=c++20 -Iinclude tools/audit_ai_pck_names.cpp BUILD/libthandor_map_core.a
//     -pthread -o audit_ai_pck_names
// ./audit_ai_pck_names GAME_ROOT > ai_pck_names.json
namespace {
std::string quote(std::string_view text) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') out << '\\' << c;
        else if (c < 0x20U)
            out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << unsigned(c);
        else out << c;
    }
    out << '"';
    return out.str();
}
std::string text_at(const thandor::TextResourceAsset& page,
                    uint32_t index, uint32_t country) {
    return index < page.string_count(country)
        ? thandor::normalize_technology_text(page.string(index, country)) : "";
}
}

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("usage: audit_ai_pck_names GAME_ROOT");
    const auto catalog = thandor::AssetCatalog::scan_game_root(argv[1]);
    const auto* help_record = catalog.find("texte/help.str");
    const auto* techno_record = catalog.find("texte/techno.str");
    const auto* tec_record = catalog.find("engine/tech.tec");
    if (!help_record || !techno_record || !tec_record)
        throw std::runtime_error("required help/techno/tech resources are missing");
    const auto help = thandor::TextResourceAsset::parse(catalog.load(*help_record));
    const auto techno = thandor::TextResourceAsset::parse(catalog.load(*techno_record));
    const auto tec_bytes = catalog.load(*tec_record);
    std::cout << "{\n  \"schema\": \"thandor-ai-pck-name-crosswalk-v1\",\n"
              << "  \"help_archive\": " << quote(help_record->archive_name) << ",\n"
              << "  \"technology_archive\": " << quote(tec_record->archive_name) << ",\n"
              << "  \"technology_text_archive\": " << quote(techno_record->archive_name) << ",\n"
              << "  \"assets\": [\n";
    bool first = true;
    for (const auto& p : catalog.army_presets()) {
        if (p.browser_kind != thandor::PlacementBrowserKind::Unit &&
            p.browser_kind != thandor::PlacementBrowserKind::Building &&
            p.visual_kind != thandor::PlacementVisualKind::PlayerStart) continue;
        if (!first) std::cout << ",\n";
        first = false;
        const auto index = thandor::army_asset_name_text_index(p.model_name_text_offset);
        uint32_t required = 0;
        if (!p.model_variant_groups.empty() && !p.model_variant_groups.front().variants.empty())
            required = p.model_variant_groups.front().variants.front().technology_unlock_id;
        std::cout << "    {\"arm\":" << p.registry_id << ",\"model\":" << p.model_definition_id
                  << ",\"name_en\":" << quote(text_at(help, index, 44U))
                  << ",\"name_de\":" << quote(text_at(help, index, 49U))
                  << ",\"catalog_loadout_label\":" << quote(p.loadout_name)
                  << ",\"name_resource_id\":" << thandor::army_asset_name_text_resource_id(p.model_name_text_offset)
                  << ",\"source_arm\":" << quote(p.source_path)
                  << ",\"required_technology\":" << required
                  << ",\"creation_grant\":" << p.linked_technology_id
                  << ",\"runtime_class\":" << p.runtime_model_class_id
                  << ",\"panel\":[";
        bool panel_first = true;
        for (const auto& entry : p.technology_panel_entries) {
            if (!panel_first) std::cout << ',';
            panel_first = false;
            std::cout << "{\"slot\":" << entry.serialized_index
                      << ",\"technology\":" << entry.technology_id << '}';
        }
        std::cout << "]}";
    }
    std::cout << "\n  ],\n  \"technologies\": [\n";
    first = true;
    for (const auto& t : catalog.technologies()) {
        if (!first) std::cout << ",\n";
        first = false;
        const auto raw_category = thandor::read_u32(tec_bytes, 0x200U + t.technology_id * 0x40U + 0x34U);
        std::cout << "    {\"technology\":" << t.technology_id
                  << ",\"name_en\":" << quote(text_at(techno, t.technology_id * 2U, 44U))
                  << ",\"name_de\":" << quote(text_at(techno, t.technology_id * 2U, 49U))
                  << ",\"category_raw\":" << raw_category
                  << ",\"category_label\":" << quote(raw_category < 4U
                       ? thandor::technology_category_retail_label(static_cast<thandor::TechnologyCategory>(raw_category)) : "?")
                  << ",\"base_score\":" << t.ai_base_candidate_score << '}';
    }
    std::cout << "\n  ]\n}\n";
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
