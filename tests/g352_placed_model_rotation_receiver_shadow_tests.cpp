#include "model_transform.hpp"
#include "thandor/catalog.hpp"
#include "thandor/field.hpp"
#include "thandor/model.hpp"
#include "thandor/terrain_shadow.hpp"

#include <array>
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
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool near(float value, float expected, float tolerance = 0.0002F) {
    return std::abs(value - expected) <= tolerance;
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void require_vector(thandor::Vec3f value, thandor::Vec3f expected, const char* message) {
    require(near(value.x, expected.x) && near(value.y, expected.y) && near(value.z, expected.z),
            message);
}
}

int main(int argc, char** argv) try {
    using namespace editor;
    using namespace thandor;

    // game.exe passes runtime fields {angle2, angle1, angle0} into the fixed-transform
    // routine's formal {angle0, angle1, angle2}.  Power-plant generators use the four
    // cardinal angle2 values below with angle1=0x4000.
    constexpr std::array<uint32_t, 4> turns{0x0000U, 0x4000U, 0x8000U, 0xC000U};
    constexpr std::array<Vec3f, 4> expected_x{{
        {1.0F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F},
        {-1.0F, 0.0F, 0.0F}, {0.0F, -1.0F, 0.0F}}};
    for (std::size_t index = 0; index < turns.size(); ++index) {
        const auto transform = fixed_model_rotation_basis(turns[index], 0x4000U, 0U);
        require_vector(transform_vector(transform, {1.0F, 0.0F, 0.0F}), expected_x[index],
                       "binary field-to-formal rotation mapping lost a cardinal orientation");
        require_vector(transform_vector(transform, {0.0F, 0.0F, 1.0F}), {0.0F, 0.0F, 1.0F},
                       "cardinal linked-child rotation tilted the vertical axis");
    }

    FieldGrid field = FieldGrid::create(8U, 8U, 0, 0U);
    const auto& a = field.at(2U, 2U);
    const auto& b = field.at(3U, 2U);
    const auto& c = field.at(2U, 3U);
    const std::array<Vec3f, 3> receiver_decal{{
        {q12_to_float(a.world_x_q12), q12_to_float(a.world_y_q12), 0.01F},
        {q12_to_float(b.world_x_q12), q12_to_float(b.world_y_q12), 0.01F},
        {q12_to_float(c.world_x_q12), q12_to_float(c.world_y_q12), 0.01F}}};
    require(triangle_lies_on_field_receiver(field, receiver_decal, 0.08F),
            "flat ground decal was not recognized as already lying on the FLD receiver");
    auto elevated = receiver_decal;
    for (auto& point : elevated) point.z += 0.5F;
    require(!triangle_lies_on_field_receiver(field, elevated, 0.08F),
            "raised model geometry was incorrectly suppressed from projected shadows");

    const std::filesystem::path source_root = THANDOR_SOURCE_DIR;
    const auto transforms = read_text(source_root / "src/editor/model_transform.hpp");
    const auto editor_source = read_text(source_root / "src/editor/editor_app.cpp");
    require(transforms.find("angle0 = model_turn32_to_radians(runtime_angle2)") != std::string::npos &&
            transforms.find("angle2 = model_turn32_to_radians(runtime_angle0)") != std::string::npos,
            "placed-model transform no longer preserves the binary field-to-formal permutation");
    require(editor_source.find("sprite.generated_shadow_mesh()") != std::string::npos &&
            editor_source.find("sprite.has_generated_shadow_mesh()") != std::string::npos &&
            editor_source.find("receiver_surface_decal") == std::string::npos,
            "placed renderer did not replace visible receiver-decal projection with authored shadow records");

    if (argc >= 2) {
        const auto catalog = AssetCatalog::scan_game_root(argv[1]);
        const auto* power_plant = catalog.army_preset(310U);
        require(power_plant != nullptr && power_plant->resolved_model_hierarchy.size() == 5U,
                "stock Power plant complete hierarchy is absent");
        std::array<bool, 4> saw_cardinal{};
        std::size_t generator_count = 0U;
        for (const auto& node : power_plant->resolved_model_hierarchy) {
            if (node.sprite_path != "spr/gebaeude/gkgen0.spr") continue;
            ++generator_count;
            const auto basis = fixed_model_rotation_basis(node.local_rotation_angle2,
                                                           node.local_rotation_angle1,
                                                           node.local_rotation_angle0);
            const Vec3f direction = transform_vector(basis, {1.0F, 0.0F, 0.0F});
            for (std::size_t i = 0; i < expected_x.size(); ++i) {
                if (near(direction.x, expected_x[i].x) && near(direction.y, expected_x[i].y))
                    saw_cardinal[i] = true;
            }
        }
        require(generator_count == 4U, "stock Power plant generator count changed");
        for (bool present : saw_cardinal)
            require(present, "stock Power plant generators did not resolve to four cardinal rotations");

        const auto* xenite_mine = catalog.army_preset(330U);
        require(xenite_mine != nullptr, "stock Xenite mine asset 330 missing");
        bool saw_ground_footprint = false;
        for (const auto& node : xenite_mine->resolved_model_hierarchy) {
            if (node.sprite_path != "spr/gebaeude/grmi22.spr") continue;
            const auto sprite = SpriteAsset::parse(catalog.load(node.sprite_path));
            require(sprite.mesh().indices.size() / 3U == 2U,
                    "Xenite mine ground-footprint SPR is no longer a two-triangle decal");
            saw_ground_footprint = true;
        }
        require(saw_ground_footprint,
                "Xenite mine hierarchy no longer exposes the grounded grmi22 footprint");
        std::cout << "G352_STOCK placed_power_plant_generators=4 xenite_ground_decal=grmi22 PASS\n";
    }

    std::cout << "G352 placed-model rotation and receiver-decal shadow regression PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G352 test failure: " << error.what() << '\n';
    return 1;
}
