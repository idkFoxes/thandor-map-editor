#include "thandor/catalog.hpp"
#include "thandor/model.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
}

int main() try {
    require(thandor::kArmyAssetNameTextPage == 0x18U,
            "army-asset name text page is not the binary-proven page 0x18");
    require(thandor::army_asset_name_text_index(0U) == 79U &&
                thandor::army_asset_name_text_index(12U) == 91U &&
                thandor::army_asset_name_text_index(50U) == 129U,
            "MDL +0x04 to help.str index mapping changed");
    require(thandor::army_asset_name_text_resource_id(0U) == 0x18004FU &&
                thandor::army_asset_name_text_resource_id(12U) == 0x18005BU &&
                thandor::army_asset_name_text_resource_id(50U) == 0x180081U,
            "MDL +0x04 to packed text-resource ID mapping changed");

    const std::filesystem::path root = THANDOR_SOURCE_DIR;
    const auto catalog_source = read_text(root / "src/core/catalog.cpp");
    const auto model_source = read_text(root / "src/core/model.cpp");
    const auto editor_source = read_text(root / "src/editor/editor_app.cpp");

    require(catalog_source.find("catalog.find(\"texte/help.str\")") != std::string::npos &&
                catalog_source.find("TextResourceAsset::parse(catalog.load(*help_text_record))") !=
                    std::string::npos,
            "asset catalog does not load the PCK-backed help text resource");
    require(catalog_source.find("army_asset_name_text_index(preset.model_name_text_offset)") !=
                    std::string::npos &&
                catalog_source.find("preset.localized_name = std::move(localized)") !=
                    std::string::npos,
            "unit/building names are not propagated into placement presets");
    require(model_source.find("visual.name_text_offset = read_u32(data, offset + 4)") !=
                    std::string::npos &&
                model_source.find("0x18004F + value") != std::string::npos,
            "MDL +0x04 name offset is not preserved with its game.exe correspondence");
    require(editor_source.find("Localized PCK name:") != std::string::npos &&
                editor_source.find("Name source: %s · resource 0x%08X") != std::string::npos &&
                editor_source.find("preset.localized_name_source_archive") != std::string::npos,
            "asset-browser tooltip does not expose localized-name provenance");
    require(catalog_source.find("invent a localized unit name") == std::string::npos,
            "superseded SPR-only name blocker is still active");

    std::cout << "G350_PCK_ASSET_NAME_PROPAGATION PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G350 checks failed: " << error.what() << '\n';
    return 1;
}
