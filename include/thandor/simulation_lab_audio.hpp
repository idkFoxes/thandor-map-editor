#pragma once
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/audio.hpp"
#include <functional>
#include <unordered_map>
namespace thandor {
struct SimulationLabSoundRequest {
    uint32_t sound_table_index{};
    Vec3f world_position{};
    uint32_t gain_q15{0x8000U};
    uint32_t maximum_distance_q12{};
};
struct SimulationLabAudioFrame {
    std::vector<SimulationLabSoundRequest> one_shots;
    struct GlobalOneShot { uint32_t sound_table_index{},listener_faction_index{}; };
    // Global SFX gain in both channels. No camera attenuation or extra fog gate.
    std::vector<GlobalOneShot> global_one_shots;
    // Keep every owner until fog and attenuation have been evaluated. An
    // inaudible later owner must not erase an earlier audible shared slot.
    std::vector<SimulationLabSoundRequest> ordered_loops;
    // Unfiltered last-owner summary retained for existing diagnostic clients.
    std::unordered_map<uint32_t,SimulationLabSoundRequest> loops;
    uint64_t entity_owners_examined{},projectile_owners_examined{},events_examined{};
};
struct SimulationLabAudibleLoops {
    std::unordered_map<uint32_t,SimulationLabSoundRequest> requests;
    uint64_t fog_filtered{},inaudible{};
};
SimulationLabAudibleLoops simulation_lab_audible_loops(
    std::span<const SimulationLabSoundRequest> requests,
    const SpatialAudioListener& listener, uint32_t effects_gain_q15,
    const std::function<bool(Vec3f)>& visible = {});
// Selected immutable world only. Includes produced owners; event positions are historical.
// Visibility and camera attenuation are applied by the presentation consumer.
SimulationLabAudioFrame simulation_lab_audio_frame(const SimulationLabRun& run,
    const SimulationLabRuntimeCatalog& catalog,
    std::span<const SimulationLabOneShotEvent> events,
    const FieldGrid* field = nullptr);
}
