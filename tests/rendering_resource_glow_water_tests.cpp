#include "thandor/catalog.hpp"
#include "thandor/effect.hpp"
#include "thandor/field.hpp"
#include "thandor/gfx.hpp"
#include "thandor/model.hpp"
#include "../src/editor/primitive_render.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(float a, float b, float epsilon = 0.001F) {
    return std::abs(a - b) <= epsilon;
}

thandor::Brush point_brush() {
    thandor::Brush brush;
    brush.radius_cells = 0.1F;
    brush.strength = 1.0F;
    brush.shape = thandor::BrushShape::Circle;
    brush.falloff = thandor::BrushFalloff::Constant;
    return brush;
}

void test_water_edit_policies() {
    using thandor::TerrainWaterEditPolicy;
    const auto brush = point_brush();

    auto absolute = thandor::FieldGrid::create(3, 3, 0, 0);
    absolute.at(1, 1).water_delta_q12 = thandor::float_to_q12(10.0F);
    absolute.raise_lower(1.0F, 1.0F, brush, 2.0F,
                         TerrainWaterEditPolicy::PreserveAbsoluteSurface);
    check(near(absolute.at(1, 1).terrain_height(), 2.0F), "absolute policy did not edit terrain");
    check(near(absolute.at(1, 1).water_height(), 10.0F), "absolute policy moved water surface");
    check(near(thandor::q12_to_float(absolute.at(1, 1).water_delta_q12), 8.0F),
          "absolute policy did not recompute FLD water delta");

    auto depth = thandor::FieldGrid::create(3, 3, 0, 0);
    depth.at(1, 1).water_delta_q12 = thandor::float_to_q12(10.0F);
    depth.raise_lower(1.0F, 1.0F, brush, 2.0F,
                      TerrainWaterEditPolicy::PreserveWaterDepth);
    check(near(depth.at(1, 1).terrain_height(), 2.0F), "depth policy did not edit terrain");
    check(near(thandor::q12_to_float(depth.at(1, 1).water_delta_q12), 10.0F),
          "depth policy changed relative water depth");
    check(near(depth.at(1, 1).water_height(), 12.0F), "depth policy did not move water with terrain");

    auto ignored = thandor::FieldGrid::create(3, 3, 0, 0);
    ignored.at(1, 1).water_delta_q12 = thandor::float_to_q12(10.0F);
    ignored.raise_lower(1.0F, 1.0F, brush, 2.0F,
                        TerrainWaterEditPolicy::IgnoreWetCells);
    check(near(ignored.at(1, 1).terrain_height(), 0.0F) &&
          near(ignored.at(1, 1).water_height(), 10.0F),
          "ignore policy changed a wet cell");
    ignored.raise_lower(0.0F, 0.0F, brush, 2.0F,
                        TerrainWaterEditPolicy::IgnoreWetCells);
    check(near(ignored.at(0, 0).terrain_height(), 2.0F),
          "ignore policy incorrectly skipped a dry cell");

    auto drying = thandor::FieldGrid::create(3, 3, 0, 0);
    drying.at(1, 1).water_delta_q12 = thandor::float_to_q12(3.0F);
    drying.raise_lower(1.0F, 1.0F, brush, 5.0F,
                       TerrainWaterEditPolicy::PreserveAbsoluteSurface);
    check(near(drying.at(1, 1).water_height(), 3.0F) &&
          near(thandor::q12_to_float(drying.at(1, 1).water_delta_q12), -2.0F),
          "absolute policy no longer mirrors retail paired terrain/water delta editing");

    auto dry_plane = thandor::FieldGrid::create(3, 3, 0, 0);
    const float dry_absolute_before = dry_plane.at(1, 1).water_height();
    dry_plane.raise_lower(1.0F, 1.0F, brush, 2.0F,
                          TerrainWaterEditPolicy::PreserveAbsoluteSurface);
    check(near(dry_plane.at(1, 1).water_height(), dry_absolute_before) &&
          dry_plane.at(1, 1).water_delta_q12 == thandor::float_to_q12(-3.0F),
          "absolute policy moved the hidden water plane for a dry cell");

    auto flattened = thandor::FieldGrid::create(3, 3, 0, 0);
    flattened.at(1, 1).water_delta_q12 = thandor::float_to_q12(10.0F);
    flattened.flatten(1.0F, 1.0F, brush, 4.0F,
                      TerrainWaterEditPolicy::PreserveAbsoluteSurface);
    check(near(flattened.at(1, 1).terrain_height(), 4.0F) &&
          near(flattened.at(1, 1).water_height(), 10.0F),
          "flatten did not honor absolute water policy");

    auto smoothed = thandor::FieldGrid::create(3, 3, thandor::float_to_q12(-8.0F), 0);
    smoothed.at(1, 1).terrain_height_q12 = 0;
    smoothed.at(1, 1).water_delta_q12 = thandor::float_to_q12(10.0F);
    thandor::Brush smooth_brush = point_brush();
    smoothed.smooth(1.0F, 1.0F, smooth_brush, 1,
                    TerrainWaterEditPolicy::PreserveAbsoluteSurface);
    check(smoothed.at(1, 1).terrain_height() < 0.0F &&
          near(smoothed.at(1, 1).water_height(), 10.0F, 0.01F),
          "smooth did not honor absolute water policy");

    check(std::string(thandor::terrain_water_edit_policy_name(
              TerrainWaterEditPolicy::PreserveAbsoluteSurface)) == "Keep absolute water level",
          "water policy UI name changed");
}


void test_terrain_resource_shading_shader() {
    const std::filesystem::path source_root = THANDOR_SOURCE_DIR;
    const auto read_text = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        if (!input) throw std::runtime_error("unable to open shader source: " + path.string());
        return std::string(std::istreambuf_iterator<char>(input),
                           std::istreambuf_iterator<char>());
    };
    const auto terrain_vertex = read_text(source_root / "shaders/terrain.vert");
    const auto terrain_fragment = read_text(source_root / "shaders/terrain.frag");
    const auto model_vertex = read_text(source_root / "shaders/model.vert");
    const auto model_fragment = read_text(source_root / "shaders/model.frag");
    const auto renderer = read_text(source_root / "src/editor/terrain_renderer.cpp");
    const auto editor_app = read_text(source_root / "src/editor/editor_app.cpp");

    check(terrain_vertex.find("layout(std430,set=0,binding=3)") != std::string::npos &&
          terrain_vertex.find("directDrawResourceShadingWords(inPosition)") != std::string::npos &&
          terrain_vertex.find("directDrawFinalizeVertexLighting") != std::string::npos,
          "terrain vertex shader no longer performs DirectDraw resource-light accumulation");
    check(terrain_vertex.find("remainingSquared * 128.0 / radiusSquared") != std::string::npos &&
          terrain_vertex.find("lookupIndex * 257.0 / 16.0") != std::string::npos,
          "terrain shader lost the 0x004CCA90/0x0041DE80 attenuation path");
    check(terrain_fragment.find("generatedResourceShadingOverlay") == std::string::npos &&
          terrain_fragment.find("mix(result") == std::string::npos &&
          terrain_fragment.find("gameByteModulate(tex.rgb,lightingBytes)") != std::string::npos,
          "terrain fragment shader reintroduced a translucent resource aura overlay");
    check(terrain_vertex.find("layout(location=3) out vec3 lightingBytes") != std::string::npos &&
          terrain_fragment.find("layout(location=3) in vec3 lightingBytes") != std::string::npos,
          "terrain resource lighting is not smoothly interpolated between vertices");
    check(model_vertex.find("direct_draw_resource_shading_words(in_position)") != std::string::npos &&
          model_vertex.find("direct_draw_finalize_vertex_lighting") != std::string::npos &&
          model_fragment.find("resource_shading_addition") == std::string::npos,
          "model resource lighting is still evaluated per fragment instead of per vertex");
    check(renderer.find("VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT") !=
              std::string::npos,
          "resource shading storage buffer is unavailable to vertex shaders");
    check(editor_app.find("ResourcePrimitiveRole::DenseBody") != std::string::npos &&
          editor_app.find("ResourcePrimitiveRole::LuminousParticle") != std::string::npos &&
          editor_app.find("if(untextured){") != std::string::npos &&
          editor_app.find("armyN.gfx carries the faction colour") != std::string::npos,
          "resource body/particle split or source-color preservation is missing");
}


void check_resource_definition(const thandor::EffectDefinitionRecord& definition,
                               uint32_t expected_id, uint32_t expected_argb) {
    check(definition.definition_id == expected_id, "resource EFF ID mismatch");
    check(definition.runtime_value_0c == 80U, "resource EFF lifetime changed");
    check(definition.frame_advance_threshold_q4 == 16U, "resource EFF frame threshold changed");
    check(definition.shading_color_argb == expected_argb, "resource EFF shading color changed");
    check(definition.shading_transition_ticks == 35 &&
          definition.shading_release_transition_ticks == 35,
          "resource EFF grow/release transition changed");
    check(definition.shading_start_frame == 0U && definition.shading_stop_frame == 79U,
          "resource EFF shading frame window changed");
    const auto profile = thandor::make_resource_shading_glow_profile(definition);
    check(near(profile.target_radius_world, 1.875F), "resource shading target radius changed");
    check(near(thandor::graphics_shading_radius_at_age(profile, 0.0F), 0.0F),
          "resource shading does not start at zero radius");
    check(near(thandor::graphics_shading_radius_at_age(profile, 17.5F), 0.9375F),
          "resource shading grow interpolation changed");
    check(near(thandor::graphics_shading_radius_at_age(profile, 35.0F), 1.875F) &&
          near(thandor::graphics_shading_radius_at_age(profile, 79.0F), 1.875F),
          "resource shading hold radius changed");
    check(near(thandor::graphics_shading_radius_at_age(profile, 96.5F), 0.9375F),
          "resource shading release interpolation changed");
    check(near(thandor::graphics_shading_radius_at_age(profile, 114.0F), 0.0F),
          "resource shading did not expire after release");
    check(near(thandor::graphics_shading_radial_factor(0.0F, 1.875F), 1.0F) &&
          near(thandor::graphics_shading_radial_factor(0.9375F, 1.875F), 0.75F) &&
          near(thandor::graphics_shading_radial_factor(1.875F, 1.875F), 0.0F),
          "binary linear remaining-radius-squared falloff changed");

    check(thandor::graphics_shading_directdraw_lookup_index(0.0F, 1.875F) == 128U &&
          thandor::graphics_shading_directdraw_lookup_index(0.9375F, 1.875F) == 96U &&
          thandor::graphics_shading_directdraw_lookup_index(1.875F, 1.875F) == 0U,
          "DirectDraw remaining-radius lookup indices changed");
    const auto center = thandor::graphics_shading_rgb_contribution(profile, 35.0F, 0.0F);
    const auto expected_red = thandor::graphics_shading_directdraw_channel_addition(
        static_cast<uint8_t>((expected_argb >> 16U) & 0xFFU), 128U);
    const auto expected_green = thandor::graphics_shading_directdraw_channel_addition(
        static_cast<uint8_t>((expected_argb >> 8U) & 0xFFU), 128U);
    const auto expected_blue = thandor::graphics_shading_directdraw_channel_addition(
        static_cast<uint8_t>(expected_argb & 0xFFU), 128U);
    check(near(center[0], static_cast<float>(expected_red) / 255.0F) &&
          near(center[1], static_cast<float>(expected_green) / 255.0F) &&
          near(center[2], static_cast<float>(expected_blue) / 255.0F),
          "resource shading no longer follows the DirectDraw qword lookup and MMX lanes");
    const auto saturated = thandor::graphics_shading_directdraw_vertex_light(
        {0.9F, 0.9F, 0.9F}, profile, 35.0F, 0.0F);
    check(saturated[0] <= 1.0F && saturated[1] <= 1.0F && saturated[2] <= 1.0F,
          "resource vertex-light addition is no longer saturating");
    const auto start = thandor::graphics_shading_directdraw_vertex_light(
        {0.25F, 0.25F, 0.25F}, profile, 0.0F, 0.0F);
    const auto full = thandor::graphics_shading_directdraw_vertex_light(
        {0.25F, 0.25F, 0.25F}, profile, 35.0F, 0.0F);
    const auto bright = thandor::graphics_shading_directdraw_vertex_light(
        {1.0F, 1.0F, 1.0F}, profile, 35.0F, 0.0F);
    const auto edge = thandor::graphics_shading_directdraw_vertex_light(
        {0.25F, 0.25F, 0.25F}, profile, 35.0F, profile.target_radius_world);
    check(near(start[0], 0.25F) && near(start[1], 0.25F) && near(start[2], 0.25F),
          "zero-radius pulse altered terrain vertex light");
    constexpr uint8_t quarter_light_byte = 64U;
    const std::array<float, 3> expected_full{
        static_cast<float>(thandor::graphics_shading_directdraw_modulated_channel(
            quarter_light_byte, thandor::graphics_shading_directdraw_raw_word(
                static_cast<uint8_t>((expected_argb >> 16U) & 0xFFU), 128U))) / 255.0F,
        static_cast<float>(thandor::graphics_shading_directdraw_modulated_channel(
            quarter_light_byte, thandor::graphics_shading_directdraw_raw_word(
                static_cast<uint8_t>((expected_argb >> 8U) & 0xFFU), 128U))) / 255.0F,
        static_cast<float>(thandor::graphics_shading_directdraw_modulated_channel(
            quarter_light_byte, thandor::graphics_shading_directdraw_raw_word(
                static_cast<uint8_t>(expected_argb & 0xFFU), 128U))) / 255.0F,
    };
    check(near(full[0], expected_full[0]) && near(full[1], expected_full[1]) &&
          near(full[2], expected_full[2]),
          "full pulse did not add the packed resource color to vertex lighting");
    check(near(bright[0], 1.0F) && near(bright[1], 1.0F) && near(bright[2], 1.0F),
          "bright terrain was tinted instead of saturated like DirectDraw");
    check(near(edge[0], 0.25F) && near(edge[1], 0.25F) && near(edge[2], 0.25F),
          "resource vertex light escaped its recovered radius");
}

enum class PrimitiveRouteMode { Direct3D, Glide3, EditorPreview };

struct StockPrimitiveCensus {
    std::size_t body_triangles{};
    std::size_t particle_triangles{};
    std::size_t unsupported_triangles{};
    std::array<std::size_t, 4> body_blends{};
    std::array<std::size_t, 4> particle_blends{};
    std::uint64_t body_selector_mask{};
    std::uint64_t particle_selector_mask{};
};

void merge_census(StockPrimitiveCensus& destination, const StockPrimitiveCensus& source) {
    destination.body_triangles += source.body_triangles;
    destination.particle_triangles += source.particle_triangles;
    destination.unsupported_triangles += source.unsupported_triangles;
    destination.body_selector_mask |= source.body_selector_mask;
    destination.particle_selector_mask |= source.particle_selector_mask;
    for (std::size_t index = 0; index < destination.body_blends.size(); ++index) {
        destination.body_blends[index] += source.body_blends[index];
        destination.particle_blends[index] += source.particle_blends[index];
    }
}

StockPrimitiveCensus census_resource_primitive_routing(
    thandor::AssetCatalog& catalog,
    const thandor::ArmyAssetPreset& preset,
    thandor::ResourceEnergyKind kind,
    PrimitiveRouteMode mode) {
    StockPrimitiveCensus result{};
    const auto sprite = thandor::SpriteAsset::parse(catalog.load(preset.sprite_path));
    const auto& mesh = sprite.mesh();
    check(mesh.indices.size() % 3U == 0U, "resource SPR index stream is not triangle aligned");
    const std::uint32_t body_subresource = thandor::resource_body_subresource(kind);
    const std::uint32_t effect_subresource = thandor::resource_luminous_particle_subresource(kind);
    for (std::size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3U) {
        const std::array<std::uint32_t, 3> indices{
            mesh.indices[triangle], mesh.indices[triangle + 1U], mesh.indices[triangle + 2U]};
        for (const auto index : indices) {
            if (index >= mesh.vertices.size()) {
                throw std::runtime_error("resource SPR triangle index exceeds vertex stream");
            }
        }
        const auto& first = mesh.vertices[indices[0]];
        const auto& second = mesh.vertices[indices[1]];
        const auto& third = mesh.vertices[indices[2]];
        if (first.texture_subresource != second.texture_subresource ||
            first.texture_subresource != third.texture_subresource ||
            first.render_flags != second.render_flags || first.render_flags != third.render_flags) {
            throw std::runtime_error("resource SPR triangle mixes subresources or render flags");
        }
        const bool body = first.texture_subresource == body_subresource;
        const bool particle = first.texture_subresource == effect_subresource;
        if (!body && !particle) continue;
        const bool textured = first.texture_subresource != std::numeric_limits<std::uint32_t>::max();
        editor::PrimitiveRenderDecision decision{};
        if (mode == PrimitiveRouteMode::EditorPreview) {
            const auto role = body ? editor::ResourcePrimitiveRole::DenseBody
                                   : editor::ResourcePrimitiveRole::LuminousParticle;
            decision = editor::classify_resource_preview_primitive(
                first.render_flags, textured, role);
        } else {
            const auto backend = mode == PrimitiveRouteMode::Glide3
                ? editor::LegacyGraphicsBackend::Glide3
                : editor::LegacyGraphicsBackend::Direct3D;
            decision = editor::classify_sprite_primitive(first.render_flags, textured, backend);
        }
        if (!decision.supported) {
            ++result.unsupported_triangles;
            continue;
        }
        const auto blend_index = static_cast<std::size_t>(decision.blend);
        if (body) {
            ++result.body_triangles;
            ++result.body_blends[blend_index];
            result.body_selector_mask |= std::uint64_t{1} << decision.selector;
        } else {
            ++result.particle_triangles;
            ++result.particle_blends[blend_index];
            result.particle_selector_mask |= std::uint64_t{1} << decision.selector;
        }
    }
    return result;
}
} // namespace

int main(int argc, char** argv) try {
    test_water_edit_policies();
    test_terrain_resource_shading_shader();
    const std::array<thandor::ResourceEmitterTiming, 2> schedule_timings{{{80U, 10U}, {80U, 10U}}};
    const auto schedule = thandor::build_resource_emitter_schedule(
        schedule_timings, thandor::FieldGrid::kDefaultSoilRandomSeed, 512U, 64U);
    check(schedule.pulses.size() > schedule_timings.size(),
          "shared-RNG resource schedule has no repeated events");
    check(schedule.pulses[0].spawn_tick == 0U && schedule.pulses[1].spawn_tick == 0U,
          "map resources did not emit immediately");

    if (argc >= 2) {
        auto catalog = thandor::AssetCatalog::scan_game_root(argv[1]);
        const auto* blue = catalog.effect_definition(114U);
        const auto* gold = catalog.effect_definition(116U);
        check(blue != nullptr && gold != nullptr, "stock resource EFF definitions are missing");
        check_resource_definition(*blue, 114U, 0x1E001E9BU);
        check_resource_definition(*gold, 116U, 0x1E9B5A00U);

        std::size_t power_presets = 0;
        std::size_t building_presets = 0;
        StockPrimitiveCensus power_d3d_primitives{};
        StockPrimitiveCensus building_d3d_primitives{};
        StockPrimitiveCensus power_preview_primitives{};
        StockPrimitiveCensus building_preview_primitives{};
        for (const auto& preset : catalog.army_presets()) {
            if (preset.timed_effect_id == 0U) continue;
            if (preset.visual_kind == thandor::PlacementVisualKind::PowerResource) {
                ++power_presets;
                check(preset.timed_effect_id == 114U && preset.timed_effect_interval_ticks == 80U &&
                      preset.timed_effect_random_ticks == 10U,
                      "power resource MDL timed-effect fields changed");
                merge_census(power_d3d_primitives, census_resource_primitive_routing(
                    catalog, preset, thandor::ResourceEnergyKind::Power,
                    PrimitiveRouteMode::Direct3D));
                merge_census(power_preview_primitives, census_resource_primitive_routing(
                    catalog, preset, thandor::ResourceEnergyKind::Power,
                    PrimitiveRouteMode::EditorPreview));
            } else if (preset.visual_kind == thandor::PlacementVisualKind::BuildingResource) {
                ++building_presets;
                check(preset.timed_effect_id == 116U && preset.timed_effect_interval_ticks == 80U &&
                      preset.timed_effect_random_ticks == 10U,
                      "building resource MDL timed-effect fields changed");
                merge_census(building_d3d_primitives, census_resource_primitive_routing(
                    catalog, preset, thandor::ResourceEnergyKind::Building,
                    PrimitiveRouteMode::Direct3D));
                merge_census(building_preview_primitives, census_resource_primitive_routing(
                    catalog, preset, thandor::ResourceEnergyKind::Building,
                    PrimitiveRouteMode::EditorPreview));
            }
        }
        check(power_presets == 2U && building_presets == 2U,
              "stock periodically glowing resource preset inventory changed");
        check(power_d3d_primitives.body_triangles > 0U &&
              power_d3d_primitives.particle_triangles > 0U &&
              building_d3d_primitives.body_triangles > 0U &&
              building_d3d_primitives.particle_triangles > 0U,
              "stock resource SPR body/effect triangle inventory is empty");
        check(power_d3d_primitives.unsupported_triangles == 0U &&
              building_d3d_primitives.unsupported_triangles == 0U,
              "stock resource SPR uses an unsupported/null DirectDraw primitive selector");
        const auto additive_index = static_cast<std::size_t>(
            editor::PrimitiveBlendPreset::AdditiveNoDepthWrite);
        check(power_d3d_primitives.body_blends[additive_index] ==
                  power_d3d_primitives.body_triangles &&
              power_d3d_primitives.particle_blends[additive_index] ==
                  power_d3d_primitives.particle_triangles &&
              building_d3d_primitives.body_blends[additive_index] ==
                  building_d3d_primitives.body_triangles &&
              building_d3d_primitives.particle_blends[additive_index] ==
                  building_d3d_primitives.particle_triangles,
              "DirectDraw/Direct3D selector 18 did not route to preset 4");
        check(power_preview_primitives.body_blends[additive_index] ==
                  power_preview_primitives.body_triangles &&
              building_preview_primitives.body_blends[additive_index] ==
                  building_preview_primitives.body_triangles &&
              power_preview_primitives.particle_blends[additive_index] ==
                  power_preview_primitives.particle_triangles &&
              building_preview_primitives.particle_blends[additive_index] ==
                  building_preview_primitives.particle_triangles,
              "editor preview did not follow DirectDraw preset 4 for all resource triangles");
        std::cout << "STOCK_RESOURCE_SHADING_GLOW_VALIDATION PASS"
                  << " effect114_color=0x" << std::hex << blue->shading_color_argb
                  << " effect116_color=0x" << gold->shading_color_argb << std::dec
                  << " radius=1.875 grow=35 stop=79 release=35"
                  << " emitter_period=80..89"
                  << " power_presets=" << power_presets
                  << " building_presets=" << building_presets << '\n';
        std::cout << "G340_STOCK_DIRECTDRAW_RESOURCE_LAYER_ROUTING PASS"
                  << " power_body_triangles=" << power_d3d_primitives.body_triangles
                  << " power_particle_triangles=" << power_d3d_primitives.particle_triangles
                  << " building_body_triangles=" << building_d3d_primitives.body_triangles
                  << " building_particle_triangles=" << building_d3d_primitives.particle_triangles
                  << " directdraw_body_selector_mask=0x" << std::hex
                  << (power_d3d_primitives.body_selector_mask |
                      building_d3d_primitives.body_selector_mask)
                  << " directdraw_particle_selector_mask=0x"
                  << (power_d3d_primitives.particle_selector_mask |
                      building_d3d_primitives.particle_selector_mask)
                  << std::dec
                  << " directdraw_selector18_additive="
                  << (power_d3d_primitives.body_blends[additive_index] +
                      power_d3d_primitives.particle_blends[additive_index] +
                      building_d3d_primitives.body_blends[additive_index] +
                      building_d3d_primitives.particle_blends[additive_index])
                  << " preview_all_additive="
                  << (power_preview_primitives.particle_blends[additive_index] +
                      building_preview_primitives.particle_blends[additive_index])
                  << '\n';
        const auto blue_profile = thandor::make_resource_shading_glow_profile(*blue);
        const auto gold_profile = thandor::make_resource_shading_glow_profile(*gold);
        const auto blue_center = thandor::graphics_shading_rgb_contribution(
            blue_profile, 35.0F, 0.0F);
        const auto gold_center = thandor::graphics_shading_rgb_contribution(
            gold_profile, 35.0F, 0.0F);
        const auto clipped = thandor::graphics_shading_saturating_add(
            {0.95F, 0.95F, 0.95F}, gold_center);
        check(clipped[0] <= 1.0F && clipped[1] <= 1.0F && clipped[2] <= 1.0F,
              "stock resource shading addition did not saturate");
        const auto neutral_gfx = thandor::GfxAsset::parse(catalog.load("gfx/mdl/sarmy0.gfx"));
        struct TextureCensus {
            std::size_t alpha_zero{};
            std::size_t alpha_full{};
            std::size_t alpha_intermediate{};
            std::size_t black{};
            std::size_t luminous{};
        };
        const auto census_texture = [](const thandor::RgbaImage& image) {
            TextureCensus census{};
            for (std::size_t offset = 0; offset + 3U < image.pixels.size(); offset += 4U) {
                const uint32_t sum = static_cast<uint32_t>(image.pixels[offset]) +
                    image.pixels[offset + 1U] + image.pixels[offset + 2U];
                const uint8_t alpha = image.pixels[offset + 3U];
                if (alpha == 0U) ++census.alpha_zero;
                else if (alpha == 255U) ++census.alpha_full;
                else ++census.alpha_intermediate;
                if (sum <= 3U) ++census.black;
                if (sum > 30U) ++census.luminous;
            }
            return census;
        };
        const auto power_body = census_texture(neutral_gfx.decode(thandor::GraphicsSubresourceIndex{26U}, false));
        const auto power_particle = census_texture(neutral_gfx.decode(thandor::GraphicsSubresourceIndex{27U}, false));
        const auto building_body = census_texture(neutral_gfx.decode(thandor::GraphicsSubresourceIndex{32U}, false));
        const auto building_particle = census_texture(neutral_gfx.decode(thandor::GraphicsSubresourceIndex{33U}, false));
        check(power_body.alpha_full == 4096U && power_body.alpha_zero == 0U &&
              power_body.alpha_intermediate == 0U,
              "blue dense body coverage census changed");
        check(building_body.alpha_zero > 8000U && building_body.alpha_intermediate > 1900U &&
              building_body.alpha_full > 6200U,
              "gold dense body alpha-coverage census changed");
        check(power_particle.alpha_full == 4096U && building_particle.alpha_full == 4096U &&
              power_particle.black > 2500U && power_particle.luminous > 500U &&
              building_particle.black > 2500U && building_particle.luminous > 500U,
              "resource luminous-particle black-canvas fixture changed");
        const auto additive_channel = [](uint32_t destination, uint32_t source) {
            return std::min(255U, destination + source);
        };
        check(additive_channel(91U, 0U) == 91U && additive_channel(240U, 32U) == 255U,
              "RGB additive particle composition changed");
        std::cout << "G340_RESOURCE_PROFILE_REFERENCE PASS"
                  << " blue_center=" << blue_center[0] << ',' << blue_center[1] << ',' << blue_center[2]
                  << " gold_center=" << gold_center[0] << ',' << gold_center[1] << ',' << gold_center[2]
                  << " terrain_resource_shading=directdraw-per-vertex-saturating-light"
                  << " model_lighting=retained"
                  << " resource_body_pipeline=directdraw-preset4-additive-no-zwrite particle_pipeline=directdraw-preset4-additive-no-zwrite"
                  << " power_black_key_pixels=" << power_particle.black
                  << " building_black_key_pixels=" << building_particle.black << '\n';
    }

    std::cout << "Resource glow and water policy tests: PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "Resource glow and water policy tests: FAIL: " << error.what() << '\n';
    return 1;
}
