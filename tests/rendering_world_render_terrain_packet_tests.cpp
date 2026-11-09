#include "retail_world_render.hpp"
#include "thandor/catalog.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not read source-contract input");
    std::ostringstream out; out << input.rdbuf(); return out.str();
}
}

int main(int argc, char** argv) try {
    using namespace editor;
    require(retail_world_model_pass_from_mdl_flags(0U) == RetailWorldModelPass::PostGeneratedShading,
            "zero MDL render flags must use the second retail model pass");
    require(retail_world_model_pass_from_mdl_flags(0x20U) == RetailWorldModelPass::PreTerrain,
            "MDL +0x68 bit 0x20 must select the first retail model pass");
    require(retail_world_model_pass_from_mdl_flags(0x60U) == RetailWorldModelPass::PreTerrain,
            "generated-shadow suppression must not erase the first-model-pass bit");

    // Two surface packets along the camera forward direction: V446's 0x6000 packet class is
    // depth sorted far-to-near, while equal depth retains source order.
    std::vector<thandor::TerrainVertex> surface(6);
    for (std::size_t i=0;i<3;++i) surface[i].position={0.0F,10.0F,0.0F};
    for (std::size_t i=3;i<6;++i) surface[i].position={0.0F,30.0F,0.0F};
    Camera camera{}; camera.target={0.0F,20.0F,0.0F}; camera.yaw=-1.57079632679F; camera.pitch=0.12F; camera.distance=20.0F;
    const auto order=retail_surface_back_to_front_indices(surface,camera,0U,false);
    require(order.size()==6U,"surface depth sort changed packet cardinality");
    require(order[0]==3U&&order[1]==4U&&order[2]==5U,"far secondary-surface packet was not drawn first");

    thandor::TerrainVertex animated{};
    animated.position={1.0F,2.0F,3.0F};
    animated.blend_materials={0U,0U,4096U | (4096U<<16U)};
    const auto displaced=retail_surface_position_at_tick(animated,0U,true);
    require(std::abs(displaced.x-2.0F)<0.0001F&&std::abs(displaced.y-2.0F)<0.0001F&&
            std::abs(displaced.z-4.0F)<0.0001F,"CPU surface sort does not match shader phase-zero displacement");

#ifdef THANDOR_SOURCE_DIR
    const std::filesystem::path source=THANDOR_SOURCE_DIR;
    const auto renderer=read_text(source/"src/editor/terrain_renderer.cpp");
    const auto app=read_text(source/"src/editor/editor_app.cpp");
    const auto header=read_text(source/"src/editor/retail_world_render.hpp");
    const auto pre=renderer.find("draw_static_models(RetailWorldModelPass::PreTerrain)");
    const auto terrain=renderer.find("if (index_count_)",pre);
    const auto water=renderer.find("if (draw_surface && depth_sorted_surface_index_counts_",terrain);
    const auto post=renderer.find("draw_static_models(RetailWorldModelPass::PostGeneratedShading)",water);
    require(pre!=std::string::npos&&terrain!=std::string::npos&&water!=std::string::npos&&
            post!=std::string::npos&&pre<terrain&&terrain<water&&water<post,
            "V446 world render pass order is not preserved in TerrainRenderer");
    require(renderer.find("update_depth_sorted_surface")!=std::string::npos&&
            app.find("renderer_.update_depth_sorted_surface")!=std::string::npos,
            "V446 secondary-surface depth queue is not updated per frame");
    require(app.find("retail_world_model_pass_from_mdl_flags(preset->model_runtime_render_flags)")!=std::string::npos,
            "ARM root MDL render flags do not reach static/dynamic model queues");
    require(header.find("0x20U")!=std::string::npos&&header.find("0x6000")!=std::string::npos,
            "V446 render authority constants are missing from the correspondence helper");
#endif

    if (argc >= 2) {
        const auto catalog=thandor::AssetCatalog::scan_game_root(argv[1]);
        std::size_t first_pass=0U;
        bool id391=false,id392=false,id853=false;
        for (const auto& preset:catalog.army_presets()) {
            if ((preset.model_runtime_render_flags&0x20U)==0U) continue;
            ++first_pass;
            id391|=preset.registry_id==391U; id392|=preset.registry_id==392U; id853|=preset.registry_id==853U;
        }
        require(first_pass==3U&&id391&&id392&&id853,
                "stock MDL +0x68 bit-0x20 first-model-pass census changed");
    }
    std::cout << "G412_V446_WORLD_RENDER_TERRAIN_PACKET PASS\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << "G412_V446_WORLD_RENDER_TERRAIN_PACKET FAIL: " << error.what() << '\n';
    return 1;
}
