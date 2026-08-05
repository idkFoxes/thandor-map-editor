#include "primitive_render.hpp"
#include "thandor/catalog.hpp"
#include "thandor/model.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace {
const char* kind_name(thandor::PlacementBrowserKind kind) {
    return thandor::placement_browser_kind_name(kind);
}
}

int main(int argc, char** argv) try {
    using namespace thandor;
    using namespace editor;
    const auto d3d = classify_sprite_primitive(0x8007U, true, LegacyGraphicsBackend::Direct3D);
    const auto glide = classify_sprite_primitive(0x8007U, true, LegacyGraphicsBackend::Glide3);
    if (d3d.blend != PrimitiveBlendPreset::OpaqueDepthWrite ||
        glide.blend != PrimitiveBlendPreset::OpaqueDepthWrite) {
        throw std::runtime_error("0x8007 exact backend routing invariant failed");
    }
    if (argc < 2) {
        std::cout << "G325_STOCK_COMPOSITION_CENSUS PASS unit-only 0x8007=directdraw-opaque/glide-opaque\n";
        return 0;
    }

    auto catalog = AssetCatalog::scan_game_root(argv[1]);
    std::set<std::pair<std::string, PlacementBrowserKind>> visited;
    std::map<std::string, std::size_t> counters;
    std::map<std::uint32_t, std::size_t> flags;
    for (const auto& preset : catalog.army_presets()) {
        if (preset.sprite_path.empty() || placement_is_resource(preset)) continue;
        if (!visited.emplace(preset.sprite_path, preset.browser_kind).second) continue;
        const auto sprite = SpriteAsset::parse(catalog.load(preset.sprite_path));
        const auto& mesh = sprite.mesh();
        for (std::size_t offset = 0; offset + 2 < mesh.indices.size(); offset += 3) {
            const auto index = mesh.indices[offset];
            if (index >= mesh.vertices.size()) throw std::runtime_error("SPR index out of range");
            const auto render_flags = mesh.vertices[index].render_flags;
            const bool textured = mesh.vertices[index].texture_subresource != std::numeric_limits<std::uint32_t>::max();
            const auto direct3d = classify_sprite_primitive(render_flags, textured, LegacyGraphicsBackend::Direct3D);
            const auto glide3 = classify_sprite_primitive(render_flags, textured, LegacyGraphicsBackend::Glide3);
            ++counters[std::string("triangles.") + kind_name(preset.browser_kind)];
            ++counters["triangles.total"];
            ++flags[render_flags];
            if (direct3d.blend == PrimitiveBlendPreset::AdditiveNoDepthWrite) ++counters["direct3d.additive"];
            if (glide3.blend == PrimitiveBlendPreset::OpaqueDepthWrite) ++counters["glide3.opaque"];
            if (direct3d.blend != glide3.blend) {
                ++counters["backend_divergent"];
                ++counters[std::string("backend_divergent.") + kind_name(preset.browser_kind)];
            }
            if (render_flags == 0x8007U) ++counters["flags.0x8007"];
        }
    }

    std::ostringstream json;
    json << "{\n  \"schema\": \"thandor-g325-stock-composition-census-v1\",\n";
    json << "  \"unique_sprite_category_pairs\": " << visited.size() << ",\n";
    json << "  \"counters\": {\n";
    bool first = true;
    for (const auto& [name, value] : counters) {
        if (!first) json << ",\n";
        first = false;
        json << "    \"" << name << "\": " << value;
    }
    json << "\n  },\n  \"top_render_flags\": [\n";
    std::vector<std::pair<std::uint32_t, std::size_t>> sorted(flags.begin(), flags.end());
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
    for (std::size_t i = 0; i < std::min<std::size_t>(12, sorted.size()); ++i) {
        if (i != 0) json << ",\n";
        json << "    {\"flags\": \"0x" << std::hex << std::uppercase << sorted[i].first
             << std::dec << "\", \"triangles\": " << sorted[i].second << "}";
    }
    json << "\n  ]\n}\n";
    std::cout << json.str();
    if (argc >= 3) {
        std::ofstream output(argv[2], std::ios::binary | std::ios::trunc);
        if (!output) throw std::runtime_error("could not create census output");
        output << json.str();
    }
    if (counters["flags.0x8007"] == 0) {
        throw std::runtime_error("stock census did not include the expected 0x8007 family");
    }
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G325_STOCK_COMPOSITION_CENSUS FAIL: " << error.what() << '\n';
    return 1;
}
