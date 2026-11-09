#include "thandor/catalog.hpp"
#include "thandor/field.hpp"
#include "thandor/gfx.hpp"
#include "thandor/level.hpp"
#include "thandor/map_document.hpp"
#include "thandor/model.hpp"
#include "thandor/pck.hpp"
#include "thandor/terrain_texture.hpp"

#include <filesystem>
#include <iostream>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <set>
#include <cmath>

namespace fs = std::filesystem;

void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

int main(int argc, char** argv) try {
    using namespace thandor;
    auto field = FieldGrid::create(17, 19, float_to_q12(2.5F), 4);
    field.at(3, 4).water_delta_q12 = float_to_q12(1.25F);
    field.at(3, 4).flags_and_material = 0xFFFFFFFFU;
    field.at(3, 4).set_material(12);
    auto serialized_field = field.serialize();
    auto decoded_field = FieldGrid::parse(serialized_field);
    check(decoded_field.width() == 17 && decoded_field.height() == 19, "field dimensions changed");
    check(decoded_field.at(3, 4).material() == 12, "field material changed");
    check(decoded_field.at(3, 4).water_delta_q12 == float_to_q12(1.25F), "field water changed");
    const std::size_t serialized_cell_offset = 0x200U + (4U * 17U + 3U) * 0x80U;
    check(read_u32(serialized_field, serialized_cell_offset + 0x08U) ==
              FieldGrid::kRetailSerializedTriangle0NormalSeed,
          "retail FLD triangle0 serialization seed changed");
    check(read_u32(serialized_field, serialized_cell_offset + 0x50U) ==
              ((0xFFFFFF00U | 12U) & FieldGrid::kRetailPersistedFlagsAndMaterialMask),
          "retail FLD flags/material sanitization mask changed");
    check(read_u32(serialized_field, serialized_cell_offset + 0x54U) == 0U,
          "retail FLD save did not clear runtime cell dword +0x54");

    const auto waves = FieldGrid::build_water_wave_records(0x12345678U);
    check(waves.size() == FieldGrid::kWaterWaveRecordCount, "terrain direction record count changed");
    check(waves.front().scale_a >= 0x80U && waves.front().scale_a <= 0x9FU &&
          waves.front().scale_b >= 0x80U && waves.front().scale_b <= 0x9FU,
          "terrain direction A/B scales left the recovered range");
    check(FieldGrid::water_wave_record_index(0x1234, 0x5678) == 0x48U,
          "terrain direction record index no longer uses low nibbles as 16x16 coordinates");

    std::array<PckWriteEntry, 1> write_entries{PckWriteEntry{"level\\unit_test.fld", serialized_field, fourcc("fld")}};
    auto pck_bytes = build_stored_pck(write_entries);
    auto pck = PckArchive::parse("memory.pck", pck_bytes);
    check(pck.entries().size() == 1, "PCK entry count changed");
    check(pck.entries().front().compression_method == PckCompressionMethod::Stored,
          "PCK method 1 was not represented by the typed compression enum");
    check(pck.entries().front().directory_runtime_payload_offset == 0U,
          "new PCK directory runtime-offset field was not initialized to zero");
    check(pck.extract(pck.entries().front()) == serialized_field, "stored PCK roundtrip changed payload");

    TerrainLighting lighting{};
    lighting.base_color = 0x00102030U; // serialized +0x120: additive step
    lighting.ramp_124 = 0xFF201008U;    // serialized +0x124: ramp base and alpha
    const auto ramp = lighting.build_game_ramp();
    check((ramp.front() & 0xFF000000U) == 0xFF000000U, "lighting alpha changed");
    check(ramp.front() == 0xFF303038U,
          "lighting ramp no longer uses +0x124 as base and +0x120 as additive step");
    check(ramp.back() == 0xFF201008U,
          "lighting ramp final entry no longer converges to +0x124 base");


    std::vector<std::byte> terrain_dat(0x20 + 26U * 0x800U);
    write_u32(terrain_dat, 0, 26);
    for (uint32_t material = 0; material < 26; ++material) {
        for (uint32_t record = 0; record < 64; ++record) {
            const std::size_t offset = 0x20 + static_cast<std::size_t>(material) * 0x800 + record * 0x20;
            const std::array<int32_t, 6> uv = record == 3
                ? std::array<int32_t, 6>{float_to_q12(128.0F), float_to_q12(9.0F), float_to_q12(64.0F), float_to_q12(119.0F), float_to_q12(191.0F), float_to_q12(119.0F)}
                : std::array<int32_t, 6>{float_to_q12(64.0F), float_to_q12(9.0F), float_to_q12(128.0F), float_to_q12(119.0F), float_to_q12(191.0F), float_to_q12(9.0F)};
            for (std::size_t component = 0; component < uv.size(); ++component) write_i32(terrain_dat, offset + component * 4, uv[component]);
            write_u32(terrain_dat, offset + 0x18, material);
            write_u32(terrain_dat, offset + 0x1C, 0);
        }
    }
    const auto terrain_layout = TerrainTextureLayout::parse(terrain_dat);
    check(terrain_layout.material_count() == 26, "terrain DAT material count changed");

    auto mesh_field = FieldGrid::create(3, 3, float_to_q12(1.0F), 5);
    mesh_field.at(1, 0).set_material(6);
    mesh_field.at(0, 1).set_material(7);
    const auto mesh_vertices = mesh_field.build_vertices(
        lighting, 2.0F, TerrainMaterialPreviewMode::ExactSoilDatPackets, &terrain_layout,
        FieldGrid::kDefaultSoilRandomSeed);
    check(mesh_vertices.size() > 24U && (mesh_vertices.size() % 3U) == 0U,
          "exact terrain packet cardinality changed");
    std::set<uint32_t> packet_materials;
    for (std::size_t triangle = 0; triangle < mesh_vertices.size(); triangle += 3U) {
        const auto& va = mesh_vertices[triangle + 0U];
        const auto& vb = mesh_vertices[triangle + 1U];
        const auto& vc = mesh_vertices[triangle + 2U];
        const float signed_area = (vb.position.x - va.position.x) * (vc.position.y - va.position.y) -
                                  (vb.position.y - va.position.y) * (vc.position.x - va.position.x);
        check(signed_area > 0.0F, "terrain packet is not counter-clockwise from +Z");
        check(va.material == vb.material && va.material == vc.material,
              "terrain packet material is not flat");
        for (const auto* vertex : {&va, &vb, &vc}) {
            check(vertex->blend_weights[0] == 1.0F && vertex->blend_weights[1] == 0.0F &&
                  vertex->blend_weights[2] == 0.0F && vertex->blend_weights[3] == 0.0F,
                  "exact packet retained whole-material blend weights");
        }
        packet_materials.insert(va.material);
    }
    check(packet_materials == std::set<uint32_t>({5U, 6U, 7U}),
          "terrain packet selector lost a painted layer");

    RgbaImage alpha_tile{3, 1, {255, 0, 0, 255, 255, 255, 255, 0, 0, 0, 255, 128}};
    const auto closed_tile = make_terrain_preview_opaque(alpha_tile);
    check(closed_tile.pixels[3] == 255 && closed_tile.pixels[7] == 255 && closed_tile.pixels[11] == 255,
          "terrain preview alpha closure did not produce opaque texels");
    check(!(closed_tile.pixels[4] == 255 && closed_tile.pixels[5] == 255 && closed_tile.pixels[6] == 255),
          "transparent white transition texel leaked into terrain preview");
    check((read_u32(mesh_field.serialize(), 0xB0) & ((1U << 5) | (1U << 6) | (1U << 7))) ==
              ((1U << 5) | (1U << 6) | (1U << 7)),
          "serialized FLD material mask did not track painted materials");

    if (argc >= 2) {
        const fs::path extracted = argv[1];
        const auto lev_path = extracted / "LEVEL/level/asgard.lev";
        const auto fld_path = extracted / "LEVEL/level/asgard.fld";
        const auto gfx_path = extracted / "GRAPHIK/gfx/boden/soila.gfx";
        const auto terrain_dat_path = extracted / "GRAPHIK/gfx/boden/soil.dat";
        const auto pal_path = extracted / "GRAPHIK/gfx/boden/soil.pal";
        const auto spr_path = extracted / "MODELLE/spr/schuesse/spkan0.spr";
        const auto mdl_path = extracted / "DATEN/mdl/unterbau1.mdl";
        auto level = LevelAsset::parse(read_file(lev_path));
        auto stock_field = FieldGrid::parse(read_file(fld_path));
        check(level.paths().level == "level/asgard", "LEV level path mismatch");
        check(stock_field.width() == 123 && stock_field.height() == 123, "stock FLD dimensions mismatch");
        const auto stock_level_roundtrip = LevelAsset::parse(level.serialize());
        check(stock_level_roundtrip.placements().size() == level.placements().size(), "LEV roundtrip changed placements");
        for (uint32_t faction = 1U; faction <= LevelAsset::kFactionCameraStartCount; ++faction) {
            const auto before = level.faction_player_slot(faction);
            const auto after = stock_level_roundtrip.faction_player_slot(faction);
            check(before.start_camera_x_q12 == after.start_camera_x_q12 &&
                  before.start_camera_y_q12 == after.start_camera_y_q12 &&
                  before.start_camera_z_q12 == after.start_camera_z_q12 &&
                  before.start_camera_magnitude_q12 == after.start_camera_magnitude_q12 &&
                  before.packed_heading_low16_pitch_high16 == after.packed_heading_low16_pitch_high16 &&
                  before.start_xenite_q4 == after.start_xenite_q4 &&
                  before.start_tritium_q4 == after.start_tritium_q4 &&
                  before.ai_class_or_mode == after.ai_class_or_mode,
                  "LEV 0x20-byte player-slot record changed during roundtrip");
        }
        for (std::size_t i = 0; i < level.placements().size(); ++i) {
            check(level.placements()[i].preserved_tail_dwords ==
                  stock_level_roundtrip.placements()[i].preserved_tail_dwords,
                  "LEV placement tail dwords changed during roundtrip");
        }
        check(FieldGrid::parse(stock_field.serialize()).cells().size() == stock_field.cells().size(), "FLD roundtrip changed cells");
        auto stock_layout = TerrainTextureLayout::parse(read_file(terrain_dat_path));
        check(stock_layout.material_count() == 26, "stock terrain DAT material count mismatch");
        auto stock_mesh = stock_field.build_vertices(level.lighting(), 2.0F);
        check(stock_mesh.size() == static_cast<std::size_t>(122 * 122 * 6), "stock terrain mesh cardinality mismatch");
        for (std::size_t triangle = 0; triangle < stock_mesh.size(); triangle += 3) {
            const auto& a = stock_mesh[triangle].position; const auto& b = stock_mesh[triangle + 1].position; const auto& c = stock_mesh[triangle + 2].position;
            check((b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x) > 0.0F, "stock terrain mesh contains a culled top face");
        }
        auto gfx = GfxAsset::parse(read_file(gfx_path));
        auto image = gfx.decode(thandor::GraphicsSubresourceIndex{0U});
        check(image.width == 256 && image.height == 256 && image.pixels.size() == 256U * 256U * 4U, "GFX decode mismatch");
        if (fs::exists(pal_path)) {
            auto pal = PalAsset::parse(read_file(pal_path));
            check(!pal.colors_argb().empty() && pal.colors_argb().size() <= 256, "PAL bounded entry count mismatch");
        }
        auto sprite = SpriteAsset::parse(read_file(spr_path));
        check(!sprite.mesh().vertices.empty() && !sprite.mesh().indices.empty(), "SPR geometry decode mismatch");
        auto visuals = parse_model_visual_records(read_file(mdl_path));
        check(!visuals.empty() && visuals.front().definition_id == 100 && visuals.front().sprite_path.starts_with("spr/"), "MDL visual mapping mismatch");

        std::size_t sprite_files = 0, sprite_vertices = 0, sprite_triangles = 0;
        std::size_t model_files = 0, model_records = 0;
        for (const auto& entry : fs::recursive_directory_iterator(extracted)) {
            if (!entry.is_regular_file()) continue;
            auto extension = lower_ascii(entry.path().extension().string());
            if (extension == ".spr") {
                auto all_sprite = SpriteAsset::parse(read_file(entry.path()));
                ++sprite_files;
                sprite_vertices += all_sprite.mesh().vertices.size();
                sprite_triangles += all_sprite.mesh().indices.size() / 3U;
            } else if (extension == ".mdl") {
                auto all_records = parse_model_visual_records(read_file(entry.path()));
                ++model_files;
                model_records += all_records.size();
            }
        }
        check(sprite_files == 423, "stock SPR census changed");
        check(model_files > 0 && model_records == 1762, "stock MDL census changed");
        std::cout << "Stock visual census: SPR files=" << sprite_files
                  << " vertices=" << sprite_vertices << " triangles=" << sprite_triangles
                  << " MDL records=" << model_records << '\n';
    }

    if (argc >= 3) {
        auto catalog = AssetCatalog::scan_game_root(argv[2]);
        auto validation = catalog.validate_supported_assets();
        check(validation.ok(), "strict supported-asset validation failed");
        check(validation.decoded_entries == validation.catalog_entries, "not every non-movie catalog entry decoded");
        std::cout << "Strict asset validation: decoded=" << validation.decoded_entries
                  << " levels=" << validation.level_files << " fields=" << validation.field_files
                  << " textures=" << validation.texture_files << " palettes=" << validation.palette_files
                  << " armies=" << validation.army_files << " models=" << validation.model_files << '\n';
        check(catalog.map_groups().size() >= 50, "catalog did not discover stock map groups");
        check(catalog.find("level/asgard.lev") != nullptr, "catalog did not index asgard.lev");
        check(catalog.find("flm/eis.flm") == nullptr, "catalog included a movie");
        std::cout << "Catalog unique ARM registry IDs=" << catalog.army_presets().size() << '\n';
        check(catalog.army_presets().size() == 326, "catalog ARM registry census is unexpectedly small");
        const auto unresolved = std::count_if(catalog.army_presets().begin(), catalog.army_presets().end(), [](const auto& value) { return value.sprite_path.empty(); });
        std::cout << "Catalog unresolved visual IDs=" << unresolved << '\n';
        check(unresolved <= 16, "catalog unresolved ARM visual census changed");
        std::cout << "Placement visual census: ARM records=" << catalog.army_presets().size()
                  << " resolved=" << catalog.army_presets().size() - static_cast<std::size_t>(unresolved)
                  << " marker-only=" << unresolved << '\n';
        const auto* preset = catalog.army_preset(1);
        check(preset != nullptr && preset->model_definition_id == 100 && !preset->sprite_path.empty(), "catalog did not join ARM, MDL, and SPR visuals");
        const auto stock_group = std::find_if(catalog.map_groups().begin(), catalog.map_groups().end(), [](const auto& group) { return group.level_path == "level/asgard.lev"; });
        check(stock_group != catalog.map_groups().end(), "asgard map group was not discovered");
        auto document = MapDocument::open(catalog, *stock_group);
        const auto temporary = std::filesystem::temp_directory_path() / "thandor_editor_test_patch";
        std::filesystem::remove_all(temporary);
        std::filesystem::create_directories(temporary);
        const auto output = document.export_game_level_package(temporary, 1, false);
        auto patch = PckArchive::open(output);
        check(patch.entries().size() == 4, "exported level package lacks discovery entries");
        check(output.filename() == "level01.pck", "game-discoverable package filename changed");
        const auto* exported_level_entry = patch.find("level/asgard.lev");
        const auto* exported_field_entry = patch.find("level/asgard.fld");
        check(exported_level_entry != nullptr && exported_field_entry != nullptr, "exported level paths changed");
        check(patch.find("level/asgard.str") != nullptr && patch.find("level/level01.dat") != nullptr,
              "exported level package lacks STR/catalog discovery entries");
        const auto exported_level = LevelAsset::parse(patch.extract(*exported_level_entry));
        const auto exported_field = FieldGrid::parse(patch.extract(*exported_field_entry));
        check(exported_level.placements().size() == document.level().placements().size(), "exported LEV placements changed");
        check(exported_field.width() == document.field().width() && exported_field.height() == document.field().height(), "exported FLD dimensions changed");

        auto created = MapDocument::create_from_template(catalog, *stock_group, "unit_test_map", 37, 41);
        check(created.map_name() == "unit_test_map", "new-map name normalization changed");
        check(created.field().width() == 37 && created.field().height() == 41, "new-map dimensions changed");
        check(created.level().placements().empty(), "new map retained template placements");
        check(created.level().faction_graphics_family_selector(1U) == 1U &&
              created.level().faction_graphics_family_selector(2U) == 3U &&
              created.level().faction_graphics_family_selector(3U) == 2U,
              "template-derived new map did not set blue/red/green faction defaults");
        for (uint32_t faction = 1U; faction <= 2U; ++faction) {
            LevelPlacement placement{};
            placement.army_asset_id = 50U;
            placement.faction_index = faction;
            created.level().add_placement(placement);
            created.placement_tags().push_back({created.level().placements().size() - 1U,
                                                PlacementRole::PlayerStart,
                                                "Test start"});
        }
        const auto existing_level99 = temporary / "level99.pck";
        write_file_atomic(existing_level99, std::span<const std::byte>(pck_bytes.data(), pck_bytes.size()));
        const auto numbered = created.export_game_level_package(temporary, 99, true);
        check(numbered.filename() == "level99.pck", "numbered level package filename changed");
        check(std::filesystem::exists(temporary / "level99.pck.map-editor-backup"), "level package backup was not created");
        auto installed_level = PckArchive::open(numbered);
        check(installed_level.find("level/unit_test_map.lev") != nullptr &&
              installed_level.find("level/unit_test_map.fld") != nullptr &&
              installed_level.find("level/unit_test_map.str") != nullptr &&
              installed_level.find("level/level99.dat") != nullptr,
              "installed level package does not contain the complete discoverable map contract");
        std::filesystem::remove_all(temporary);
    }

    std::cout << "Thandor map editor core tests: PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "Thandor map editor core tests: FAIL: " << error.what() << '\n';
    return 1;
}
