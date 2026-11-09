#include "thandor/binary.hpp"
#include "thandor/effect.hpp"
#include "thandor/field.hpp"
#include "thandor/fog_preview.hpp"
#include "thandor/model.hpp"

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
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void write_ascii_utf16(std::vector<std::byte>& data, std::size_t offset,
                       const std::string& text) {
    for (unsigned char c : text) {
        thandor::write_u16(data, offset, static_cast<uint16_t>(c));
        offset += 2U;
    }
    thandor::write_u16(data, offset, 0U);
}

std::vector<std::byte> timed_model_fixture() {
    constexpr std::size_t record_offset = 0x200U;
    constexpr std::size_t root_node_offset = 0x500U;
    std::vector<std::byte> data(0x600U, std::byte{});
    thandor::write_u32(data, 0x00U, thandor::fourcc("mdl"));
    thandor::write_u32(data, 0x04U, static_cast<uint32_t>(data.size()));
    thandor::write_u32(data, 0xB0U, 1U);
    thandor::write_u32(data, record_offset + 0x00U, 0x300U);
    thandor::write_u32(data, record_offset + 0x08U, 909U);
    thandor::write_u32(data, record_offset + 0x4CU, 21U);
    thandor::write_u32(data, record_offset + 0x58U, 147U);
    thandor::write_u32(data, record_offset + 0x68U, 1U);
    thandor::write_i32(data, record_offset + 0x168U, 28);
    thandor::write_u32(data, record_offset + 0x16CU, 33U);
    thandor::write_u32(data, record_offset + 0x170U, 7U);
    thandor::write_u32(data, record_offset + 0x174U, 144U);
    thandor::write_u32(data, record_offset + 0x178U, 80U);
    thandor::write_u32(data, record_offset + 0x17CU, 10U);
    thandor::write_u32(data, record_offset + 0x64U, static_cast<uint32_t>(root_node_offset));
    thandor::write_u32(data, root_node_offset + 0x00U, 0x80U);
    thandor::write_u32(data, root_node_offset + 0x04U, 0U);
    thandor::write_u32(data, root_node_offset + 0x0CU, 0x4000U);
    thandor::write_u32(data, root_node_offset + 0x14U, 0U);
    write_ascii_utf16(data, root_node_offset + 0x38U, "spr/test/timed.spr");
    return data;
}
} // namespace

int main() try {
    using namespace thandor;

    // Exact nearest-water selection uses the persisted FLD lattice coordinates rather than a
    // screen/world-space Euclidean guess.
    auto field = FieldGrid::create(5U, 5U, 0, 0U);
    field.at(2U, 3U).water_delta_q12 = 0x2200;
    const auto& wet = field.at(2U, 3U);
    const auto& dry = field.at(1U, 1U);
    require(retail_nearest_water_delta_q12(field, wet.world_x_q12, wet.world_y_q12) == 0x2200,
            "timed EFF nearest-water selector missed the exact wet FLD vertex");
    require(retail_nearest_water_delta_q12(field, dry.world_x_q12, dry.world_y_q12) ==
                FieldGrid::kDryWaterDeltaQ12,
            "timed EFF nearest-water selector misclassified a dry FLD vertex");

    const auto model = parse_model_visual_records(timed_model_fixture(), "mdl/g409_timed.mdl");
    require(model.size() == 1U, "timed MDL fixture did not parse");
    const auto& simulation = model.front().simulation;
    require(simulation.timed_water_effect_id == 147U && simulation.timed_effect_id == 144U,
            "MDL +0x58/+0x174 land-water EFF pair was not preserved");
    require(simulation.timed_shot_definition_id == 28 && simulation.timed_shot_interval_ticks == 33U &&
                simulation.timed_shot_random_ticks == 7U,
            "MDL +0x168..+0x170 timed SHT channel was not preserved");
    require(simulation.timed_effect_interval_ticks == 80U &&
                simulation.timed_effect_random_ticks == 10U &&
                (simulation.runtime_render_flags & 1U) != 0U,
            "MDL timed EFF cadence/class-6 selector policy was not preserved");
    require(!model.front().hierarchy_nodes.empty() &&
                model.front().hierarchy_nodes.front().simulation.timed_water_effect_id == 147U &&
                model.front().hierarchy_nodes.front().simulation.timed_effect_id == 144U,
            "resolved hierarchy node did not inherit late-parsed timed MDL semantics");

    EffectDefinitionRecord frame_driven{};
    frame_driven.transition_kind = 1U;
    frame_driven.runtime_value_0c = 6U;
    frame_driven.frame_advance_threshold_q4 = 160U; // 10 retail ticks/frame
    frame_driven.linked_shot_present = 2U;          // two completed frames per shot
    frame_driven.linked_shot_id = 28U;
    const auto linked = build_linked_shot_emissions(frame_driven);
    require(linked.size() == 2U && linked[0].parent_age_tick == 20U && linked[0].attachment_index == 0U &&
                linked[1].parent_age_tick == 40U && linked[1].attachment_index == 1U,
            "linked-shot cadence was flattened back to 20 Hz ticks instead of animation frames");

    frame_driven.linked_effect_present = 2U;
    frame_driven.linked_effect_id = 25U;
    const auto linked_effect_tick = effect_linked_effect_spawn_tick(frame_driven);
    require(linked_effect_tick.has_value() && *linked_effect_tick == 20U,
            "linked-effect cadence was flattened back to 20 Hz ticks instead of animation frames");
    frame_driven.linked_effect_present = frame_driven.runtime_value_0c;
    require(!effect_linked_effect_spawn_tick(frame_driven).has_value(),
            "terminal animation frame incorrectly emitted a linked effect after retail unlink");

    const std::array<ResourceEmitterTiming, 2> timings{{
        {5U, 0U, true, false}, {5U, 0U, true, false}}};
    const auto no_orientation = build_resource_emitter_schedule(timings, 0x198FU, 32U, 8U,
        [](uint32_t, uint32_t, uint32_t){ return false; });
    const auto water_orientation = build_resource_emitter_schedule(timings, 0x198FU, 32U, 8U,
        [](uint32_t emitter, uint32_t, uint32_t){ return emitter == 0U; });
    require(no_orientation.pulses.size() >= 4U && water_orientation.pulses.size() >= 4U,
            "timed EFF schedule fixture did not produce enough events");
    require(no_orientation.pulses[2].attachment_random != water_orientation.pulses[2].attachment_random,
            "conditional transition-kind-2 orientation RNG did not shift the shared primary RNG");

    const std::filesystem::path root = THANDOR_SOURCE_DIR;
    const auto app = read_text(root / "src/editor/editor_app.cpp");
    const auto header = read_text(root / "src/editor/editor_app.hpp");
    const auto catalog = read_text(root / "src/core/catalog.cpp");
    const auto effect = read_text(root / "src/core/effect.cpp");

    require(app.find("Whole map already explored") != std::string::npos &&
                app.find("explored_mask") != std::string::npos &&
                header.find("fog_preview_all_explored_") != std::string::npos,
            "editor-only pre-explored fog baseline is missing");
    require(app.find("Debug radius multiplier") == std::string::npos &&
                app.find("Fog nominal radius") != std::string::npos &&
                app.find("return preset.fog_uncover_radius_q12<=0?0U:") != std::string::npos &&
                app.find("static_cast<uint32_t>(preset.fog_uncover_radius_q12)") != std::string::npos,
            "fog distance circle is not bound to the unscaled retail MDL +0x48 input radius");
    require(app.find("glfwSetCursorPos(window_,right_pointer_capture_center_.x") != std::string::npos &&
                app.find("right_pointer_capture_active_") != std::string::npos &&
                app.find("Capture at canvas center") != std::string::npos,
            "right-button continuous camera capture/recenter contract is missing");
    require(app.find("retail_nearest_water_delta_q12") != std::string::npos &&
                app.find("source.water_effect_id:source.land_effect_id") != std::string::npos &&
                app.find("runtime_definition_root") != std::string::npos,
            "timed map-effect runtime-root/land-water selector pipeline is incomplete");
    require(effect.find("cadence_frames = definition.linked_shot_present") != std::string::npos &&
                effect.find("consumes_orientation_random_for_pulse") != std::string::npos,
            "effect transition frame cadence or conditional orientation RNG closure is missing");
    require(app.find("TEC entries %zu · variant groups %zu") != std::string::npos &&
                app.find("Runtime rules: timed %u · damage %u · notifications %u") != std::string::npos &&
                app.find("Effect pipeline: %s") != std::string::npos &&
                app.find("technical_details") != std::string::npos,
            "compact research/attachment/effect summary is not exposed in the asset UI");
    require(app.find("SHT %u · dmg %.3f..%.3f · range %.3f · reload %ut") != std::string::npos &&
                app.find("target-class damage raw (+0xB0)") != std::string::npos &&
                app.find("Sound: mask %d · gain %.3fx · max %.3f") != std::string::npos,
            "compact weapon damage/timing diagnostics are missing from the Shift detail view");
    require(catalog.find("target_class_impact_effect_ids") != std::string::npos &&
                catalog.find("terrain_impact_effect_ids") != std::string::npos,
            "catalog flattened the indexed SHT impact tables again");

    std::cout << "G409_FOG_EFFECT_ATTACHMENT_CAMERA_DAMAGE PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G409 fog/effect/attachment/camera/damage checks failed: " << error.what() << '\n';
    return 1;
}
