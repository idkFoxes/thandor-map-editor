#include "thandor/catalog.hpp"
#include "thandor/context_order.hpp"
#include "thandor/gfx.hpp"
#include "thandor/model.hpp"
#include "primitive_render.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
std::size_t opaque_black_pixels(const thandor::RgbaImage& image) {
    std::size_t count = 0;
    for (std::size_t i = 0; i + 3U < image.pixels.size(); i += 4U) {
        if (image.pixels[i] == 0U && image.pixels[i + 1U] == 0U &&
            image.pixels[i + 2U] == 0U && image.pixels[i + 3U] == 255U) ++count;
    }
    return count;
}
}

int main(int argc, char** argv) try {
    static_assert(thandor::model_runtime_class_uses_resource_field_support(14U));
    static_assert(!thandor::model_runtime_class_uses_resource_field_support(13U));
    static_assert(!thandor::model_runtime_class_uses_resource_field_support(16U));
    static_assert(thandor::resource_field_support_mask(0U) == 0x00000800U);
    static_assert(thandor::resource_field_support_mask(1U) == 0x00001000U);

    using thandor::ContextOrderActorCapabilities;
    using thandor::ContextOrderKind;
    constexpr ContextOrderActorCapabilities armed_unit{true, true, false, false, 1U};
    static_assert(thandor::resolve_context_order(armed_unit, 2U, true, true) ==
                  ContextOrderKind::AttackPlacement);
    static_assert(thandor::resolve_context_order(armed_unit, 1U, true, true) ==
                  ContextOrderKind::None);
    static_assert(thandor::resolve_context_order(armed_unit, 0U, true, true) ==
                  ContextOrderKind::None);
    static_assert(thandor::resolve_context_order(armed_unit, std::nullopt, false, true) ==
                  ContextOrderKind::MoveToTerrain);

    const std::filesystem::path source_root{THANDOR_SOURCE_DIR};
    const auto app = read_text(source_root / "src/editor/editor_app.cpp");
    const auto model_source = read_text(source_root / "src/core/model.cpp");
    require(app.find("pick_placement_hit") != std::string::npos &&
            app.find("nearest>terrain_hit->distance+kTerrainOcclusionTolerance") != std::string::npos,
            "mouse entity hit is not rejected when terrain occludes it");
    require(app.find("if(pointer_entity_hit) return ContextOrderKind::None") == std::string::npos,
            "context-order core unexpectedly leaked into editor source");
    require(app.find("entity_hit.has_value(),hovered_terrain_point_.has_value()") != std::string::npos,
            "visible entity does not block terrain drive fallback");
    require(app.find("runtime class 14 requires") != std::string::npos &&
            app.find("placement_has_required_resource_support") != std::string::npos,
            "class-14 Xenite/Tritium extractor placement support is not enforced");
    require(app.find("resource_support_active&&") != std::string::npos ||
            app.find("resource_support_active &&") != std::string::npos,
            "class-14 authored model animation is not gated by live field support");
    require(model_source.find("model_runtime_class_uses_resource_field_support(visual.runtime_class_id)") !=
                std::string::npos,
            "MDL +0xC0 is still treated as a resource support selector for unrelated runtime classes");

    if (argc >= 2) {
        const auto catalog = thandor::AssetCatalog::scan_game_root(argv[1]);
        const auto* xenite_mine = catalog.army_preset(330U);
        const auto* tritium_pump = catalog.army_preset(332U);
        require(xenite_mine && tritium_pump, "stock extractor ARM 330/332 missing");
        require(xenite_mine->runtime_model_class_id == 14U &&
                xenite_mine->has_placement_occupancy_class &&
                xenite_mine->placement_occupancy_class == 0U,
                "Xenite mine no longer resolves to class-14 Xenite support selector 0");
        require(tritium_pump->runtime_model_class_id == 14U &&
                tritium_pump->has_placement_occupancy_class &&
                tritium_pump->placement_occupancy_class == 1U,
                "Tritium pump no longer resolves to class-14 Tritium support selector 1");

        const auto* tritium_deposit = catalog.army_preset(811U);
        const auto* xenite_deposit = catalog.army_preset(815U);
        require(tritium_deposit && xenite_deposit, "canonical stock resource deposits missing");
        const auto find_timed_effect = [](const thandor::ArmyAssetPreset& preset) {
            const auto& h = preset.resolved_model_hierarchy.empty() ?
                preset.model_hierarchy : preset.resolved_model_hierarchy;
            for (const auto& node : h) if (node.simulation.timed_effect_id != 0U) return node.simulation.timed_effect_id;
            return 0U;
        };
        require(find_timed_effect(*tritium_deposit) == 114U,
                "Tritium deposit no longer owns EFF 114");
        require(find_timed_effect(*xenite_deposit) == 116U,
                "Xenite deposit no longer owns EFF 116");
        require(find_timed_effect(*tritium_pump) == 0U && find_timed_effect(*xenite_mine) == 0U,
                "extractor building incorrectly inherited the deposit's timed EFF");

        const auto army1 = thandor::GfxAsset::parse(catalog.load("gfx/mdl/army1.gfx"));
        const auto mine_decal = army1.decode(thandor::GraphicsSubresourceIndex{111U}, false);
        const auto pump_decal = army1.decode(thandor::GraphicsSubresourceIndex{112U}, false);
        require(opaque_black_pixels(mine_decal) != 0U && opaque_black_pixels(pump_decal) != 0U,
                "resource extractor footprint no longer contains its authored opaque dark ground pixels");
        const auto fully_opaque = [](const thandor::RgbaImage& image) {
            for (std::size_t i = 3U; i < image.pixels.size(); i += 4U)
                if (image.pixels[i] != 255U) return false;
            return true;
        };
        require(fully_opaque(mine_decal) && fully_opaque(pump_decal),
                "stock extractor footprint alpha changed; do not invent black-key transparency");

        const auto mine_sprite = thandor::SpriteAsset::parse(catalog.load("spr/gebaeude/grmi22.spr"));
        const auto pump_sprite = thandor::SpriteAsset::parse(catalog.load("spr/gebaeude/grmi13.spr"));
        require(mine_sprite.mesh().indices.size() == 6U && pump_sprite.mesh().indices.size() == 6U,
                "stock extractor footprint decals are no longer two triangles");
        const auto mine_vertex = mine_sprite.mesh().vertices.at(mine_sprite.mesh().indices.front());
        const auto pump_vertex = pump_sprite.mesh().vertices.at(pump_sprite.mesh().indices.front());
        require(mine_vertex.texture_subresource == 111U && pump_vertex.texture_subresource == 112U,
                "extractor footprint decal GFX subresources changed");
        require(editor::classify_directdraw_direct3d_sprite_primitive(mine_vertex.render_flags, true).blend ==
                    editor::PrimitiveBlendPreset::OpaqueDepthWrite &&
                editor::classify_directdraw_direct3d_sprite_primitive(pump_vertex.render_flags, true).blend ==
                    editor::PrimitiveBlendPreset::OpaqueDepthWrite,
                "extractor footprint is not the binary-proven opaque textured primitive");
    }

    std::cout << "G410 mouse entity occlusion and Xenite/Tritium extractor support pipeline PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G410_RESOURCE_EXTRACTOR_CONTEXT_ORDER FAIL: " << error.what() << '\n';
    return 1;
}
