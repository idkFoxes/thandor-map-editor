#include "thandor/binary.hpp"
#include "thandor/effect.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not open source file");
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_u32(std::vector<std::byte>& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned index = 0; index < 4; ++index)
        bytes.at(offset + index) = std::byte((value >> (index * 8U)) & 0xFFU);
}

void write_utf16_ascii(std::vector<std::byte>& bytes, std::size_t offset,
                       const std::string& value) {
    for (std::size_t index = 0; index < value.size(); ++index) {
        bytes.at(offset + index * 2U) = std::byte(static_cast<unsigned char>(value[index]));
        bytes.at(offset + index * 2U + 1U) = std::byte{0};
    }
}
} // namespace

int main() try {
    thandor::EffectDefinitionRecord controller{};
    controller.transition_kind = 1U;
    controller.runtime_value_0c = 257U;
    controller.linked_shot_present = 32U;
    controller.linked_shot_id = 28U;
    controller.frame_advance_threshold_q4 = 16U;
    const auto emissions = thandor::build_linked_shot_emissions(controller);
    require(emissions.size() == 8U, "EFF 25 did not produce eight linked Shot 28 emissions");
    require(emissions.front().parent_age_tick == 32U &&
            emissions.back().parent_age_tick == 256U,
            "EFF 25 linked-shot cadence is not 32 maintenance ticks");
    for (std::size_t index = 0; index < emissions.size(); ++index)
        require(emissions[index].attachment_index == index,
                "EFF 25 type-2 attachment order changed");

    thandor::EffectDefinitionRecord headquarters_arc{};
    headquarters_arc.runtime_value_0c = 8U;
    headquarters_arc.frame_advance_threshold_q4 = 160U;
    require(thandor::effect_lifetime_ticks(headquarters_arc) == 80U,
            "EFF 131 recovered 80-tick lifetime changed");
    require(thandor::effect_frame_index_at_age(headquarters_arc, 9U) == 0U &&
            thandor::effect_frame_index_at_age(headquarters_arc, 10U) == 1U,
            "EFF 131 frame threshold is not the recovered Q4 value 160");

    const std::array<thandor::ResourceEmitterTiming, 2> timings{{
        {270U, 0U, false, false},
        {8U, 0U, false, true},
    }};
    const auto schedule = thandor::build_resource_emitter_schedule(timings, 0x198FU, 512U, 256U);
    require(schedule.pulses.size() > 10U, "timed-effect schedule did not repeat");
    require(schedule.pulses[0].spawn_tick == 0U && schedule.pulses[1].spawn_tick == 0U,
            "map timed effects no longer emit at tick zero");
    require(schedule.pulses[0].attachment_random == 0U,
            "persistent attachment counter did not begin at zero");
    require(schedule.pulses[1].orientation_random != 0U,
            "transition-state-2 orientation RNG was not preserved");

    constexpr std::size_t header_size = 0x200U;
    constexpr std::size_t record_size = 0x2E0U;
    std::vector<std::byte> shot_bytes(header_size + record_size, std::byte{0});
    write_u32(shot_bytes, 0x00U, thandor::fourcc("sht"));
    write_u32(shot_bytes, 0x04U, static_cast<std::uint32_t>(shot_bytes.size()));
    write_u32(shot_bytes, 0x0CU, 0x00060006U);
    write_u32(shot_bytes, 0xB0U, 1U);
    const std::size_t record = header_size;
    write_u32(shot_bytes, record + 0x00U, 1U);
    write_u32(shot_bytes, record + 0x08U, 28U);
    write_u32(shot_bytes, record + 0x0CU, 450U);
    write_u32(shot_bytes, record + 0x10U, 31U);
    write_u32(shot_bytes, record + 0x14U, 2U);
    write_u32(shot_bytes, record + 0xD0U, 900U);
    write_u32(shot_bytes, record + 0xDCU, 4U);
    write_u32(shot_bytes, record + 0x268U, 111U);
    write_u32(shot_bytes, record + 0x26CU, 1U);
    write_utf16_ascii(shot_bytes, record + 0x29CU, "spr/schuesse/spmgw0");
    const auto shot_asset = thandor::ShotAsset::parse(shot_bytes);
    require(shot_asset.definitions().size() == 1U, "synthetic Shot 28 did not parse");
    const auto& shot = shot_asset.definitions().front();
    require(shot.definition_id == 28U && shot.trajectory_mode == 1 &&
            shot.launch_speed_q12 == 450 && shot.primary_effect_id == 31U &&
            shot.secondary_effect_id == 111U && shot.terrain_impact_effect_id(0U) == 2U &&
            shot.secondary_effect_interval_ticks == 1U && shot.projectile_lifetime_ticks == 900U &&
            shot.ballistic_divisor_q12 == 4,
            "Shot 28 visual-chain fields were parsed from the wrong offsets");

    const std::filesystem::path root = THANDOR_SOURCE_DIR;
    const auto app = read_text(root / "src/editor/editor_app.cpp");
    const auto renderer = read_text(root / "src/editor/terrain_renderer.cpp");
    const auto renderer_header = read_text(root / "src/editor/terrain_renderer.hpp");
    require(app.find("kSurfacePresentationTicksPerSecond=20.0") != std::string::npos &&
            app.find("renderer_.set_presentation_tick(current_presentation_tick_)") !=
                std::string::npos &&
            renderer.find("uniform.material_scale[3] = static_cast<float>(presentation_tick_)") !=
                std::string::npos &&
            (renderer + renderer_header).find("animation_epoch_") == std::string::npos,
            "surface/effect time is still reset by an object or resource buffer rebuild");
    for (const char* token : {"pending_timed_effects", "append_effect_visual",
                              "build_linked_shot_emissions", "shot_definition",
                              "effect_texture_layer", "append_linked_shot_visual_recursive",
                              "mul(sub(*emitter,source),2.0F)",
                              "simulation_resolve_projectile_surface_impact"})
        require(app.find(token) != std::string::npos,
                "lava timed-effect rendering chain is incomplete");

    std::cout << "G365_PRESENTATION_TIMELINE_LAVA_EFFECTS water=live lava=live "
                 "eff25_shot28_eff31_2_111=present PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G365 presentation/lava-effect checks failed: " << error.what() << '\n';
    return 1;
}
