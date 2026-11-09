#include "thandor/audio.hpp"
#include "thandor/binary.hpp"
#include "thandor/simulation.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string read_text(const std::filesystem::path& path){
    std::ifstream stream(path,std::ios::binary);std::ostringstream text;text<<stream.rdbuf();return text.str();
}
}

int main() try {
    using thandor::Vec3f;
    // The exact retail radius contract is additive. The editor applies it over the entire preview
    // segment to prevent high-multiplier tunnelling through a runtime that the per-tick point test
    // would have hit.
    require(thandor::simulation_segment_intersects_expanded_radius(
                {0,0,0},{4,0,0},{2,0,0},0x1000,0x1000),
            "summed MDL +0xDC radii did not block a crossing segment");
    require(!thandor::simulation_segment_intersects_expanded_radius(
                {0,0,0},{4,0,0},{2,2.01F,0},0x1000,0x1000),
            "summed collision radius expanded beyond its exact Q12 sum");
    require(!thandor::simulation_segment_intersects_expanded_radius(
                {0,0,0},{4,0,0},{2,0,0},0,0x1000),
            "zero mover radius did not preserve retail no-test behavior");

    // A transient waypoint must be consumed without destroying the final order.
    thandor::SimulationActorState actor;
    actor.position={0,0,0};actor.body_yaw_turn16=0;
    actor.move_target=Vec3f{3,0,0};actor.move_waypoint=Vec3f{0.25F,0,0};
    thandor::SimulationMotionProfile motion;
    motion.speed_q12_per_tick=0x1000;motion.yaw_max_velocity_turn16=0x1000;
    motion.yaw_acceleration_turn16=0x1000;
    require(thandor::simulation_tick_actor_motion(actor,motion),"waypoint motion did not advance");
    require(!actor.move_waypoint.has_value(),"reached local waypoint was not consumed");
    require(actor.move_target.has_value(),"reached local waypoint destroyed the final move order");

    // Runtime spatial-sound identity comes from the trailing decimal number before .sam, not PCK
    // entry order and not the DirectSound first-free voice registry.
    require(thandor::sam_trailing_decimal_index("mdl\\army\\gorilla36.sam")==36U,
            "trailing SAM runtime index parse failed");
    require(thandor::sam_trailing_decimal_index("fx/orang0007.SAM")==7U,
            "case-insensitive SAM index parse failed");
    require(!thandor::sam_trailing_decimal_index("sound/music.sam"),
            "SAM without a trailing runtime number acquired an invented index");

    // Zero-coded coefficients are an exact cheap decoder fixture: one retail block produces 256
    // stereo s16 frames, all zero. This verifies header geometry, packed-code progression, output
    // geometry and the synthesis path without shipping a copyrighted stock sound in the tests.
    std::vector<std::byte> sam(0x240U,std::byte{0});
    thandor::write_u32(sam,0x00U,0x006D6173U);
    thandor::write_u32(sam,0x04U,static_cast<uint32_t>(sam.size()));
    thandor::write_u32(sam,0x08U,1U);
    thandor::write_u32(sam,0x0CU,0x00010000U);
    thandor::write_u32(sam,0xB0U,1U);
    const auto pcm=thandor::decode_sam_to_pcm(sam);
    require(pcm.frame_count()==256U,"one SAM block did not produce 256 stereo frames");
    require(std::all_of(pcm.interleaved_stereo.begin(),pcm.interleaved_stereo.end(),
                        [](int16_t sample){return sample==0;}),
            "zero SAM block synthesized nonzero PCM");

    const std::filesystem::path root=THANDOR_SOURCE_DIR;
    const auto model=read_text(root/"src/core/model.cpp");
    const auto catalog=read_text(root/"src/core/catalog.cpp");
    const auto editor=read_text(root/"src/editor/editor_app.cpp");
    const auto header=read_text(root/"src/editor/editor_app.hpp");
    require(model.find("offset + 0xDCU")!=std::string::npos &&
            model.find("movement_collision_radius_q12")!=std::string::npos,
            "MDL +0xDC collision radius is not parsed");
    require(catalog.find("root_node->simulation.movement_collision_radius_q12")!=std::string::npos,
            "building/unit presets do not retain the MDL collision radius");
    require(editor.find("simulation_path_segment_clear")!=std::string::npos &&
            (editor.find("simulation_choose_local_detour")!=std::string::npos ||
             editor.find("simulation_resolve_v528_route")!=std::string::npos),
            "editor collision/route integration is absent");
    require(editor.find("Sound playback")!=std::string::npos &&
            header.find("sound_playback_enabled_{true}")!=std::string::npos,
            "audible sound toggle is absent or not enabled by default");
    require(editor.find("document_->level().paths().sounds")!=std::string::npos &&
            editor.find("sam_trailing_decimal_index")!=std::string::npos &&
            editor.find("resource_paths()") == std::string::npos,
            "editor sound resolver does not follow the LEV sound-template producer chain");

    std::cout<<"V451_COLLISION_AUDIO_COMPLETION PASS\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"V451 collision/audio completion failed: "<<error.what()<<'\n';
    return 1;
}
