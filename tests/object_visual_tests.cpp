#include "thandor/catalog.hpp"
#include "thandor/effect.hpp"
#include "thandor/field.hpp"
#include "thandor/gfx.hpp"
#include "thandor/level.hpp"
#include "thandor/map_document.hpp"
#include "thandor/model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(float a, float b, float epsilon = 0.0001F) {
    return std::abs(a - b) <= epsilon;
}

std::string base_path(std::string value) {
    value = thandor::normalize_asset_path(value);
    const auto slash = value.find_last_of('/');
    const auto dot = value.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) value.resize(dot);
    return value;
}

std::string asset_stem(std::string_view value) {
    const auto slash = value.find_last_of('/');
    const auto dot = value.find_last_of('.');
    const auto begin = slash == std::string_view::npos ? 0U : slash + 1U;
    const auto end = dot == std::string_view::npos || dot < begin ? value.size() : dot;
    return std::string(value.substr(begin, end - begin));
}

struct ColorClassCounts { std::size_t blue{}, warm{}, visible{}; };
ColorClassCounts classify_visible_colors(const thandor::RgbaImage& image) {
    ColorClassCounts result;
    for (std::size_t offset = 0; offset + 3U < image.pixels.size(); offset += 4U) {
        const uint32_t red = image.pixels[offset];
        const uint32_t green = image.pixels[offset + 1U];
        const uint32_t blue = image.pixels[offset + 2U];
        if (image.pixels[offset + 3U] == 0U || red + green + blue <= 30U) continue;
        ++result.visible;
        if (blue > green && green >= red) ++result.blue;
        if (red > green && green > blue) ++result.warm;
    }
    return result;
}

void check_mesh_subresource(const thandor::SpriteMesh& mesh, uint32_t subresource,
                            const char* message) {
    check(!mesh.indices.empty(), message);
    for (const auto& vertex : mesh.vertices) check(vertex.texture_subresource == subresource, message);
}

struct Family {
    thandor::GfxAsset gfx;
    thandor::PalAsset pal;
};

struct MapObjectStats {
    std::size_t starts{};
    std::size_t selected_triangles{};
    std::size_t alternate_lod_triangles{};
    std::size_t trees{};
    std::size_t tree_triangles{};
    std::size_t rocks{};
    std::size_t rock_triangles{};
    std::size_t power{};
    std::size_t power_body_triangles{};
    std::size_t power_effect_triangles{};
    std::size_t building{};
    std::size_t building_body_triangles{};
    std::size_t building_effect_triangles{};
    std::size_t terrain_conformed_resource_vertices{};
    std::size_t wrapped_uv_vertices{};
    std::set<uint64_t> texture_keys;
};

MapObjectStats audit_map(thandor::AssetCatalog& catalog, std::string_view stem,
                         const char* expected_texture_family) {
    const auto group = std::find_if(catalog.map_groups().begin(), catalog.map_groups().end(),
        [&](const auto& value) { return value.level_path == "level/" + std::string(stem) + ".lev"; });
    check(group != catalog.map_groups().end(), "stock map group is missing");
    const auto level = thandor::LevelAsset::parse(catalog.load(group->level_path));
    const auto field = thandor::FieldGrid::parse(catalog.load(group->field_path));
    check(level.paths().army_textures == expected_texture_family, "stock army texture family changed");

    std::map<uint32_t, Family> families;
    const std::string base = base_path(level.paths().army_textures);
    const auto family = [&](uint32_t faction) -> const Family& {
        faction = std::min(faction, 7U);
        auto found = families.find(faction);
        if (found == families.end()) {
            found = families.emplace(faction, Family{
                thandor::GfxAsset::parse(catalog.load(base + std::to_string(faction) + ".gfx")),
                thandor::PalAsset::parse(catalog.load(base + std::to_string(faction) + ".pal"))}).first;
        }
        return found->second;
    };

    MapObjectStats result;
    for (const auto& placement : level.placements()) {
        if (placement.army_asset_id == 50U) ++result.starts;
        const auto* preset = catalog.army_preset(placement.army_asset_id);
        if (preset == nullptr || preset->sprite_path.empty() ||
            preset->visual_kind == thandor::PlacementVisualKind::PlayerStart) continue;

        const auto sprite = thandor::SpriteAsset::parse(catalog.load(preset->sprite_path));
        check(sprite.group_count() == sprite.lod_groups().size() && sprite.group_count() >= 1U,
              "SPR LOD group inventory is invalid");
        check(sprite.mesh().vertices.size() == sprite.mesh().indices.size() &&
              sprite.mesh().vertices.size() % 3U == 0U,
              "selected SPR LOD is not one indexed corner per triangle corner");
        result.selected_triangles += sprite.mesh().indices.size() / 3U;
        result.alternate_lod_triangles += sprite.alternate_lod_triangle_count();

        thandor::ResourceEnergyKind kind = thandor::ResourceEnergyKind::None;
        if (preset->visual_kind == thandor::PlacementVisualKind::PowerResource) {
            kind = thandor::ResourceEnergyKind::Power;
        } else if (preset->visual_kind == thandor::PlacementVisualKind::BuildingResource) {
            kind = thandor::ResourceEnergyKind::Building;
        }
        if (kind != thandor::ResourceEnergyKind::None) {
            const auto body = thandor::extract_resource_body_mesh(sprite.mesh(), kind);
            const auto effect = thandor::extract_resource_luminous_particle_mesh(sprite.mesh(), kind);
            const uint32_t body_subresource = thandor::resource_body_subresource(kind);
            const uint32_t effect_subresource = thandor::resource_luminous_particle_subresource(kind);
            check_mesh_subresource(body, body_subresource, "resource body mesh is missing or mixed");
            if (!effect.indices.empty()) {
                check_mesh_subresource(effect, effect_subresource, "resource effect mesh is mixed");
            }
            const float radians = static_cast<float>(placement.orientation_turn16 & 0xFFFFU) *
                (2.0F * std::numbers::pi_v<float> / 65536.0F);
            const float cosine = std::cos(radians), sine = std::sin(radians);
            for (const auto* part : {&body, &effect}) {
                for (const auto& vertex : part->vertices) {
                    const float world_x = placement.render_x() +
                        vertex.position.x * cosine - vertex.position.y * sine;
                    const float world_y = placement.render_y() +
                        vertex.position.x * sine + vertex.position.y * cosine;
                    const auto sample = field.sample_terrain(world_x, world_y);
                    check(sample.inside && std::isfinite(sample.height) && sample.normal.z > 0.0F,
                          "resource terrain projection returned an invalid surface sample");
                    ++result.terrain_conformed_resource_vertices;
                }
            }
            result.texture_keys.insert(body_subresource);
            if (!effect.indices.empty()) result.texture_keys.insert(effect_subresource);
            if (kind == thandor::ResourceEnergyKind::Power) {
                ++result.power;
                result.power_body_triangles += body.indices.size() / 3U;
                result.power_effect_triangles += effect.indices.size() / 3U;
            } else {
                ++result.building;
                result.building_body_triangles += body.indices.size() / 3U;
                result.building_effect_triangles += effect.indices.size() / 3U;
            }
            continue;
        }

        const auto& textures = family(0U); // neutral scenery uses source colors from family zero
        for (const auto& vertex : sprite.mesh().vertices) {
            if (vertex.texture_subresource == std::numeric_limits<uint32_t>::max()) continue;
            check(vertex.texture_subresource < textures.gfx.subresources().size(),
                  "SPR selects a missing neutral GFX subresource");
            const auto& sub = textures.gfx.subresources()[vertex.texture_subresource];
            const auto uv = thandor::normalize_sprite_texture_uv(
                vertex.texture_u, vertex.texture_v, sub.pixel_width, sub.pixel_height);
            check(std::isfinite(uv.u) && std::isfinite(uv.v), "normalized SPR UV is not finite");
            if (uv.u < 0.0F || uv.v < 0.0F || uv.u > 1.0F || uv.v > 1.0F) {
                ++result.wrapped_uv_vertices;
            }
            result.texture_keys.insert(vertex.texture_subresource);
        }
        if (preset->visual_kind == thandor::PlacementVisualKind::Tree) {
            ++result.trees;
            result.tree_triangles += sprite.mesh().indices.size() / 3U;
        } else if (preset->visual_kind == thandor::PlacementVisualKind::Rock) {
            ++result.rocks;
            result.rock_triangles += sprite.mesh().indices.size() / 3U;
        }
    }
    return result;
}
} // namespace

int main(int argc, char** argv) try {
    if (argc < 2) {
        std::cout << "Object visual stock-data test: SKIP (no game root)\n";
        return 0;
    }
    auto catalog = thandor::AssetCatalog::scan_game_root(argv[1]);

    const auto square_uv = thandor::normalize_sprite_texture_uv(256.0F, 256.0F, 64U, 64U);
    check(near(square_uv.u, 1.0F) && near(square_uv.v, 1.0F),
          "square SPR UV no longer uses the universal 256 basis");
    const auto rectangular_uv = thandor::normalize_sprite_texture_uv(256.0F, 256.0F, 128U, 64U);
    check(near(rectangular_uv.u, 1.0F) && near(rectangular_uv.v, 0.5F),
          "rectangular SPR UV aspect correction changed");

    thandor::RgbaImage rectangular_image{128U, 64U,
        std::vector<uint8_t>(128U * 64U * 4U, 0U)};
    rectangular_image.pixels[0] = 17U;
    rectangular_image.pixels[1] = 34U;
    rectangular_image.pixels[2] = 51U;
    rectangular_image.pixels[3] = 255U;
    const auto square_canvas = thandor::make_sprite_texture_square_canvas(rectangular_image);
    check(square_canvas.width == 128U && square_canvas.height == 128U &&
          square_canvas.pixels[0] == 17U && square_canvas.pixels[3] == 255U &&
          square_canvas.pixels[(static_cast<std::size_t>(96U) * 128U) * 4U + 3U] == 0U,
          "rectangular SPR texture was stretched instead of padded into the square hardware canvas");

    check(thandor::placement_uses_neutral_source_color(thandor::PlacementVisualKind::Tree) &&
          thandor::placement_uses_neutral_source_color(thandor::PlacementVisualKind::Rock) &&
          thandor::placement_uses_neutral_source_color(thandor::PlacementVisualKind::PowerResource) &&
          thandor::placement_uses_neutral_source_color(thandor::PlacementVisualKind::BuildingResource) &&
          !thandor::placement_uses_neutral_source_color(thandor::PlacementVisualKind::Standard) &&
          !thandor::placement_uses_neutral_source_color(thandor::PlacementVisualKind::PlayerStart),
          "neutral static-placement source-color classification changed");

    const auto known_tree = thandor::SpriteAsset::parse(catalog.load("spr/extras/fbnba0.spr"));
    check(known_tree.group_count() == 4U && known_tree.mesh().indices.size() / 3U == 25U &&
          known_tree.alternate_lod_triangle_count() == 67U,
          "tree SPR LOD separation changed");
    const auto known_rock = thandor::SpriteAsset::parse(catalog.load("spr/extras/ffstb0.spr"));
    check(known_rock.group_count() == 3U && known_rock.mesh().indices.size() / 3U == 19U &&
          known_rock.alternate_lod_triangle_count() == 38U,
          "rock SPR LOD separation changed");
    const auto known_resource = thandor::SpriteAsset::parse(catalog.load("spr/extras/ffkrm0.spr"));
    check(known_resource.group_count() == 1U && known_resource.mesh().indices.size() / 3U == 130U,
          "resource SPR single-group inventory changed");

    std::size_t localized_runtime_presets = 0;
    std::size_t pck_leaf_fallback_presets = 0;
    for (const auto& preset : catalog.army_presets()) {
        check(!preset.display_name.empty() && !preset.short_name.empty(),
              "placement preset has no PCK-derived display name");
        const bool runtime_named =
            preset.browser_kind == thandor::PlacementBrowserKind::Unit ||
            preset.browser_kind == thandor::PlacementBrowserKind::Building ||
            preset.visual_kind == thandor::PlacementVisualKind::PlayerStart;
        if (runtime_named) {
            if (!preset.localized_name.empty()) {
                check(preset.localized_name_source_path == "texte/help.str" &&
                      preset.name_text_resource_id ==
                          thandor::army_asset_name_text_resource_id(preset.model_name_text_offset) &&
                      preset.display_name.find(preset.localized_name) != std::string::npos &&
                      preset.short_name == preset.localized_name,
                      "unit/building name is not propagated from the PCK help text page");
                ++localized_runtime_presets;
            } else {
                check(preset.registry_id == 345U &&
                      preset.name_text_resource_id ==
                          thandor::army_asset_name_text_resource_id(preset.model_name_text_offset) &&
                      preset.display_name == "Laser coil" && preset.short_name == "Laser coil",
                      "runtime asset with an empty PCK name did not use its exact research/loadout fallback");
                ++pck_leaf_fallback_presets;
            }
        } else if (!preset.sprite_path.empty() &&
                   preset.visual_kind != thandor::PlacementVisualKind::PowerResource &&
                   preset.visual_kind != thandor::PlacementVisualKind::BuildingResource) {
            const std::string leaf = asset_stem(preset.sprite_path);
            check(!leaf.empty() && preset.display_name.find(leaf) != std::string::npos &&
                  preset.short_name == leaf,
                  "non-runtime placement fallback is not derived from the joined SPR leaf");
            ++pck_leaf_fallback_presets;
        }
    }
    check(localized_runtime_presets >= 170U,
          "too few unit/building presets received localized PCK names");
    check(pck_leaf_fallback_presets >= 100U,
          "too few non-runtime presets retained deterministic SPR fallback names");

    const auto* start_preset = catalog.army_preset(50U);
    check(start_preset != nullptr &&
          start_preset->visual_kind == thandor::PlacementVisualKind::PlayerStart &&
          start_preset->source_path == "arm/unit.arm",
          "asset ID 50 player / AI start classification changed");

    const auto asgard_group = std::find_if(catalog.map_groups().begin(), catalog.map_groups().end(),
        [](const auto& value) { return value.level_path == "level/asgard.lev"; });
    check(asgard_group != catalog.map_groups().end(), "Asgard map group is missing");
    const auto asgard_level = thandor::LevelAsset::parse(catalog.load(asgard_group->level_path));
    const auto asgard_document = thandor::MapDocument::open(catalog, *asgard_group);
    std::set<uint32_t> start_factions;
    for (const auto& placement : asgard_level.placements()) {
        if (placement.army_asset_id == 50U) start_factions.insert(placement.faction_index);
    }
    check(start_factions == std::set<uint32_t>({1U, 2U, 3U, 4U}),
          "Asgard player / AI start factions changed");
    const auto tagged_starts = std::count_if(asgard_document.placement_tags().begin(),
        asgard_document.placement_tags().end(), [](const auto& tag) {
            return tag.role == thandor::PlacementRole::PlayerStart;
        });
    check(tagged_starts == 4, "Asgard start records were not inferred as PlayerStart tags");

    const auto lighting_ramp = asgard_level.lighting().build_game_ramp();
    // Water consumes the LEV ramp alpha, while opaque object modulation intentionally forces
    // alpha to one.  Do not couple this object regression to the stock water-alpha value.
    const uint32_t opaque_preview_modulation =
        thandor::multiply_rgb_opaque(lighting_ramp[128], 0xFFFFFFFFU);
    check((opaque_preview_modulation & 0xFF000000U) == 0xFF000000U,
          "object preview modulation inherited zero LEV lighting alpha");

    const auto neutral_gfx = thandor::GfxAsset::parse(catalog.load("gfx/mdl/sarmy0.gfx"));
    const auto neutral_pal = thandor::PalAsset::parse(catalog.load("gfx/mdl/sarmy0.pal"));
    check(neutral_gfx.subresources().size() > 33U && neutral_pal.colors_argb().size() == 64U,
          "neutral army GFX/PAL family changed");
    const auto power_colors = classify_visible_colors(neutral_gfx.decode(thandor::GraphicsSubresourceIndex{27U}, false));
    const auto building_colors = classify_visible_colors(neutral_gfx.decode(thandor::GraphicsSubresourceIndex{33U}, false));
    check(power_colors.visible > 100U && power_colors.blue == power_colors.visible,
          "neutral subresource 27 is no longer blue power energy");
    check(building_colors.visible > 100U && building_colors.warm == building_colors.visible,
          "neutral subresource 33 is no longer gold building energy");
    check(((neutral_pal.colors_argb()[4U] >> 16U) & 0xFFU) >
          (neutral_pal.colors_argb()[4U] & 0xFFU),
          "neutral PAL hue regression fixture changed");

    const auto asgard = audit_map(catalog, "asgard", "gfx/mdl/sarmy");
    check(asgard.starts == 4U && asgard.trees == 38U && asgard.tree_triangles == 603U &&
          asgard.rocks == 6U && asgard.rock_triangles == 120U,
          "Asgard selected high-detail scenery inventory changed");
    check(asgard.power == 10U && asgard.power_body_triangles == 2236U &&
          asgard.power_effect_triangles == 552U && asgard.building == 11U &&
          asgard.building_body_triangles == 640U && asgard.building_effect_triangles == 400U,
          "Asgard resource body/effect inventory changed");
    check(asgard.selected_triangles == 4551U && asgard.alternate_lod_triangles == 2135U,
          "Asgard selected/alternate SPR LOD totals changed");
    check(asgard.terrain_conformed_resource_vertices ==
          (asgard.power_body_triangles + asgard.power_effect_triangles +
           asgard.building_body_triangles + asgard.building_effect_triangles) * 3U,
          "Asgard resource terrain-conformed vertex count changed");

    const auto kreuzgang = audit_map(catalog, "kreuzgang", "gfx/mdl/army");
    check(kreuzgang.trees == 124U && kreuzgang.tree_triangles == 2130U &&
          kreuzgang.rocks == 28U && kreuzgang.rock_triangles == 548U,
          "Kreuzgang selected high-detail scenery inventory changed");
    check(kreuzgang.power == 12U && kreuzgang.power_body_triangles == 1720U &&
          kreuzgang.power_effect_triangles == 416U && kreuzgang.building == 12U &&
          kreuzgang.building_body_triangles == 720U && kreuzgang.building_effect_triangles == 450U,
          "Kreuzgang resource body/effect inventory changed");
    check(kreuzgang.selected_triangles == 5988U && kreuzgang.alternate_lod_triangles == 7395U,
          "Kreuzgang selected/alternate SPR LOD totals changed");
    check(kreuzgang.wrapped_uv_vertices > 1000U,
          "Kreuzgang stock UVs no longer exercise repeat addressing");

    std::size_t sentinel_files = 0, sentinel_triangles = 0;
    for (const auto& asset : catalog.assets()) {
        if (asset.kind != thandor::AssetKind::Model || !asset.normalized_path.ends_with(".spr")) continue;
        const auto sprite = thandor::SpriteAsset::parse(catalog.load(asset));
        bool has_sentinel = false;
        for (const auto& lod : sprite.lod_groups()) {
            for (std::size_t triangle = 0; triangle < lod.mesh.indices.size(); triangle += 3U) {
                const auto& vertex = lod.mesh.vertices[lod.mesh.indices[triangle]];
                if (vertex.texture_subresource == std::numeric_limits<uint32_t>::max()) {
                    ++sentinel_triangles;
                    has_sentinel = true;
                }
            }
        }
        if (has_sentinel) ++sentinel_files;
    }
    check(sentinel_files == 18U && sentinel_triangles == 452U,
          "stock untextured SPR sentinel inventory changed");

    std::size_t effect_file_count = 0, effect_definition_count = 0;
    std::set<std::string> effect_sprite_paths;
    for (const auto& asset : catalog.assets()) {
        if (asset.kind != thandor::AssetKind::Effect) continue;
        ++effect_file_count;
        const auto effect_asset = thandor::EffectAsset::parse(catalog.load(asset));
        effect_definition_count += effect_asset.definitions().size();
        for (const auto& definition : effect_asset.definitions()) {
            check(definition.transition_kind <= 4U && !definition.sprite_path.empty(),
                  "stock EFF definition is malformed");
            const auto* sprite_record = catalog.find(definition.sprite_path);
            check(sprite_record != nullptr, "stock EFF nested SPR is missing from the PCK catalog");
            (void)thandor::SpriteAsset::parse(catalog.load(*sprite_record));
            effect_sprite_paths.insert(definition.sprite_path);
        }
    }
    check(effect_file_count == 10U && effect_definition_count == 140U &&
          effect_sprite_paths.size() == 114U,
          "stock EFF registry inventory changed");

    const auto* tree_preset = catalog.army_preset(561U);
    const auto* rock_preset = catalog.army_preset(632U);
    check(tree_preset != nullptr && tree_preset->visual_kind == thandor::PlacementVisualKind::Tree &&
          !tree_preset->shared_sprite_effect_ids.empty(),
          "tree placement did not retain recursively joined EFF metadata");
    check(rock_preset != nullptr && rock_preset->visual_kind == thandor::PlacementVisualKind::Rock,
          "rock placement classification changed");

    std::cout << "G316_SPR_LOD_UV_NEUTRAL_SCENERY_VALIDATION PASS"
              << " asgard_selected=" << asgard.selected_triangles
              << " asgard_alternate=" << asgard.alternate_lod_triangles
              << " asgard_trees=" << asgard.trees
              << " asgard_tree_triangles=" << asgard.tree_triangles
              << " asgard_rocks=" << asgard.rocks
              << " asgard_rock_triangles=" << asgard.rock_triangles
              << " kreuzgang_selected=" << kreuzgang.selected_triangles
              << " kreuzgang_alternate=" << kreuzgang.alternate_lod_triangles
              << " kreuzgang_trees=" << kreuzgang.trees
              << " kreuzgang_tree_triangles=" << kreuzgang.tree_triangles
              << " kreuzgang_rocks=" << kreuzgang.rocks
              << " kreuzgang_rock_triangles=" << kreuzgang.rock_triangles
              << " power_body=" << kreuzgang.power_body_triangles
              << " power_effect=" << kreuzgang.power_effect_triangles
              << " building_body=" << kreuzgang.building_body_triangles
              << " building_effect=" << kreuzgang.building_effect_triangles
              << " wrapped_uv_vertices=" << kreuzgang.wrapped_uv_vertices
              << " untextured_files=" << sentinel_files
              << " untextured_triangles=" << sentinel_triangles
              << " effect_files=" << effect_file_count
              << " effect_definitions=" << effect_definition_count
              << " localized_runtime_presets=" << localized_runtime_presets
              << " pck_leaf_fallback_presets=" << pck_leaf_fallback_presets
              << " opaque_preview_alpha=" << ((opaque_preview_modulation >> 24U) & 0xFFU) << '\n';
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G316_SPR_LOD_UV_NEUTRAL_SCENERY_VALIDATION FAIL: " << error.what() << '\n';
    return 1;
}
