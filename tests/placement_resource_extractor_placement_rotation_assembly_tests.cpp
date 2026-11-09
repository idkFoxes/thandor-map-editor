#include "thandor/binary.hpp"
#include "thandor/effect.hpp"
#include "thandor/field.hpp"
#include "thandor/level.hpp"
#include "thandor/model.hpp"
#include "thandor/simulation.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void write_ascii_utf16(std::span<std::byte> data, std::size_t offset, std::string_view value) {
    for (const char character : value) {
        thandor::write_u16(data, offset,
            static_cast<uint16_t>(static_cast<unsigned char>(character)));
        offset += 2U;
    }
    thandor::write_u16(data, offset, 0U);
}

std::vector<std::byte> make_class14_model_fixture() {
    constexpr std::size_t record_offset = 0x200U;
    constexpr std::size_t record_size = 0x300U;
    constexpr std::size_t root_node_offset = 0x500U;
    std::vector<std::byte> data(0x600U, std::byte{});
    thandor::write_u32(data, 0x00U, thandor::fourcc("mdl"));
    thandor::write_u32(data, 0x04U, static_cast<uint32_t>(data.size()));
    thandor::write_u32(data, 0xB0U, 1U);
    thandor::write_u32(data, record_offset + 0x00U, static_cast<uint32_t>(record_size));
    thandor::write_u32(data, record_offset + 0x08U, 913U);
    thandor::write_u32(data, record_offset + 0x4CU, 14U);
    thandor::write_u32(data, record_offset + 0xC0U, 1U);
    thandor::write_u32(data, record_offset + 0xC8U, 0x123456ABU);
    thandor::write_u32(data, record_offset + 0x64U, static_cast<uint32_t>(root_node_offset));
    thandor::write_u32(data, root_node_offset + 0x00U, 0x80U);
    thandor::write_u32(data, root_node_offset + 0x04U, 0U);
    thandor::write_u32(data, root_node_offset + 0x0CU, 0x4000U);
    thandor::write_u32(data, root_node_offset + 0x14U, 0U);
    write_ascii_utf16(data, root_node_offset + 0x38U, "spr/test/g413.spr");
    return data;
}
} // namespace

int main() try {
    const auto model = thandor::parse_model_visual_records(make_class14_model_fixture(),
        "mdl/g413_class14.mdl");
    require(model.size() == 1U && model.front().runtime_class_id == 14U,
        "synthetic class-14 MDL did not parse");
    require(model.front().placement_occupancy_class == 1U &&
            model.front().has_placement_occupancy_class,
        "class-14 MDL +0xC0 resource selector was not preserved");
    require(model.front().simulation.resource_extraction_weight_u8 == 0xABU,
        "class-14 MDL +0xC8 must preserve only the low byte packed into FieldGridCell.runtime7C");

    auto field = thandor::FieldGrid::create(9U, 9U, 0, 0U);
    field.at(0U, 0U).flags_and_material |= 0x1000U;
    field.at(1U, 1U).flags_and_material |= 0x1000U;
    require(field.terrain_rejects_placement_occupancy_class(0U, 0U, 1U),
        "class-14 placement accepted the retail-forbidden outer field border");
    require(!field.terrain_rejects_placement_occupancy_class(1U, 1U, 1U),
        "class-14 placement rejected a strict-interior cell carrying the Tritium support bit");
    require(field.terrain_rejects_placement_occupancy_class(1U, 1U, 0U),
        "Tritium support incorrectly satisfied a Xenite extractor");

    require(thandor::placement_heading_after_pointer_delta(0U, 1) == 0x40U,
        "one horizontal placement-rotation pixel must add exactly 0x40 turn16");
    require(thandor::placement_heading_after_pointer_delta(0U, -1) == 0xFFC0U,
        "negative placement rotation did not wrap to turn16");
    require(thandor::placement_heading_after_pointer_delta(0xFFF0U, 1) == 0x30U,
        "positive placement rotation did not wrap to turn16");

    const auto angles = thandor::timed_effect_transition2_runtime_angles(0xABCDEF01U);
    require(angles[0] == 0xEF01U && angles[1] == 0x3544U && angles[2] == 0xEF01U,
        "timed transition-2 EFF orientation does not match game.exe low16 / 0x4000-high12 / low16");

    require(thandor::simulation_launch_policy_for_runtime_class(9U) ==
                thandor::SimulationLaunchPolicy::SequentialKind2Attachments &&
            thandor::simulation_launch_policy_for_runtime_class(21U) ==
                thandor::SimulationLaunchPolicy::SequentialKind2Attachments &&
            thandor::simulation_launch_policy_for_runtime_class(7U) ==
                thandor::SimulationLaunchPolicy::AllKind2Attachments,
        "weapon launch policy is not owned by linked MDL runtime class 9/21");

    const auto root = std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto editor = read_text(root / "src/editor/editor_app.cpp");
    const auto editor_header = read_text(root / "src/editor/editor_app.hpp");
    const auto catalog = read_text(root / "src/core/catalog.cpp");
    const auto field_source = read_text(root / "src/core/field.cpp");

    const auto placement_rotation = editor.find("const auto rotate_pending_building");
    const auto camera_capture = editor.find("if(right_pointer_capture_active_&&right_pointer_dragged_");
    require(placement_rotation != std::string::npos && camera_capture != std::string::npos &&
            editor.find("tool_!=Tool::EffectScenery||io.KeyCtrl||io.KeyAlt||io.KeyShift") != std::string::npos,
        "ordinary camera capture can consume a building rotation gesture");
    require(editor.find("placement_rotation_pointer_anchor_={io.MousePos.x,io.MousePos.y};") != std::string::npos &&
            editor.find("ImGui::IsMouseDown(ImGuiMouseButton_Left)") != std::string::npos &&
            editor.find("placement_heading_after_pointer_delta") != std::string::npos,
        "retail non-right/left-hold placement rotation contract is missing");
    require(editor.find("const auto release_cell=placement_rotation_cell_") != std::string::npos &&
            editor.find("add_asset_placement(release_cell->first,release_cell->second,selected_asset_id_)") != std::string::npos &&
            editor.find("placement.orientation_turn16=selected_orientation_") != std::string::npos,
        "building release position and live heading are not the commit authority");
    require(editor_header.find("drag_preview_cell_") != std::string::npos &&
            editor.find("release to place") != std::string::npos,
        "building placement preview no longer exposes held-rotation/release semantics");
    require(editor.find("Field extraction weight: %u") != std::string::npos &&
            editor.find("connected field region") != std::string::npos,
        "class-14 connected-region extraction semantics are missing from the asset UI");
    require(catalog.find("preset.runtime_model_class_id == 21U") == std::string::npos &&
            catalog.find("simulation_launch_policy_for_runtime_class") != std::string::npos,
        "class-21 weapon behavior still incorrectly depends on the ARM root class");
    require(field_source.find("x + 1U >= width_") != std::string::npos &&
            field_source.find("y + 1U >= height_") != std::string::npos,
        "strict class-14 field-border placement gate is absent");
    require(editor.find("timed_effect_transition2_runtime_angles") != std::string::npos &&
            editor.find("exact_runtime_angles=EffectRuntimeAngles") != std::string::npos,
        "timed environmental transition-2 EFFs still use the old yaw-only approximation");

    std::cout << "G413_RESOURCE_EXTRACTOR_PLACEMENT_ROTATION_ASSEMBLY PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G413_RESOURCE_EXTRACTOR_PLACEMENT_ROTATION_ASSEMBLY FAIL: "
              << error.what() << '\n';
    return 1;
}
