#include "model_transform.hpp"
#include "thandor/catalog.hpp"
#include "thandor/effect.hpp"
#include "thandor/model.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read source file: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
}

int main(int argc, char** argv) try {
    const auto root = std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto app_h = read_text(root / "src/editor/editor_app.hpp");
    const auto app_c = read_text(root / "src/editor/editor_app.cpp");
    const auto renderer_h = read_text(root / "src/editor/terrain_renderer.hpp");
    const auto effect_h = read_text(root / "include/thandor/effect.hpp");
    const auto effect_c = read_text(root / "src/core/effect.cpp");

    assert(app_h.find("animation_preview_multiplier_{1U}") != std::string::npos);
    assert(app_c.find("Exact game rate (1x)") != std::string::npos);
    assert(app_c.find("16x") != std::string::npos);
    assert(app_c.find("effect_transition2_displacement_at_age") != std::string::npos);
    assert(app_c.find("effect_alpha_at_age") != std::string::npos);
    assert(app_c.find("effect_sort_group") != std::string::npos);
    assert(app_c.find("run.sort_group!=0U") != std::string::npos);
    assert(renderer_h.find("uint32_t sort_group{}") != std::string::npos);
    assert(effect_h.find("movement_speed_q12") != std::string::npos);
    assert(effect_h.find("alpha_fade_in_ticks") != std::string::npos);
    assert(effect_h.find("alpha_fade_out_ticks") != std::string::npos);
    assert(effect_c.find("offset + 0x20") != std::string::npos);
    assert(effect_c.find("offset + 0x50") != std::string::npos);
    assert(effect_c.find("offset + 0x54") != std::string::npos);
    assert(app_c.find("volumetric_cross_plane") == std::string::npos);

    if (argc >= 2) {
        const auto catalog = thandor::AssetCatalog::scan_game_root(argv[1]);
        const auto* headquarters = catalog.army_preset(300U);
        if (headquarters == nullptr) throw std::runtime_error("stock Headquarters ARM 300 missing");
        if (headquarters->child0_rotation_step_turn16 != 1)
            throw std::runtime_error("stock Headquarters radar step changed");
        const auto* smoke = catalog.effect_definition(131U);
        if (smoke == nullptr) throw std::runtime_error("stock EFF 131 missing");
        if (smoke->movement_speed_q12 != 0x30 || smoke->alpha_fade_in_ticks != 8U ||
            smoke->alpha_fade_out_ticks != 80U || thandor::effect_lifetime_ticks(*smoke) != 80U)
            throw std::runtime_error("stock EFF 131 motion/alpha fields changed");
        const auto sprite = thandor::SpriteAsset::parse(catalog.load(smoke->sprite_path));
        if (sprite.mesh().indices.size() != 6U || sprite.lod_flags(0U) != 0xAU)
            throw std::runtime_error("stock EFF 131 billboard packet changed");

        const auto& hierarchy = headquarters->resolved_model_hierarchy;
        auto radar = std::find_if(hierarchy.begin(), hierarchy.end(), [](const auto& node) {
            return node.sprite_path == "spr/aufbau/aarad0.spr";
        });
        if (radar == hierarchy.end() || radar->parent_index >= hierarchy.size() || radar->child_slot != 0U)
            throw std::runtime_error("resolved Headquarters radar ownership changed");
        const auto basis0 = editor::fixed_model_rotation_basis(radar->local_rotation_angle2,
            radar->local_rotation_angle1, radar->local_rotation_angle0);
        const auto angle256 = static_cast<uint32_t>(static_cast<int64_t>(radar->local_rotation_angle2) +
            static_cast<int64_t>(headquarters->child0_rotation_step_turn16) * 256) & 0xFFFFU;
        const auto basis256 = editor::fixed_model_rotation_basis(angle256,
            radar->local_rotation_angle1, radar->local_rotation_angle0);
        const auto p0 = editor::transform_vector(basis0, {1.0F, 0.0F, 0.0F});
        const auto p256 = editor::transform_vector(basis256, {1.0F, 0.0F, 0.0F});
        assert(std::hypot(p0.x - p256.x, p0.y - p256.y) > 0.01F);
    }

    std::cout << "G369 Headquarters radar preview, transition-2 smoke and atomic billboard packet PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G369_HEADQUARTERS_FIDELITY FAIL: " << error.what() << '\n';
    return 1;
}
