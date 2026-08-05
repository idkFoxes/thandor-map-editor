#include "thandor/catalog.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
std::string read_text(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("unable to read source file: " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
}

int main(int argc,char** argv) try {
    const auto root=std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto app_h=read_text(root/"src/editor/editor_app.hpp");
    const auto app_c=read_text(root/"src/editor/editor_app.cpp");
    require(app_h.find("uint32_t animation_preview_multiplier_{1U}")!=std::string::npos,
            "shared timeline default changed");
    require(app_c.find("Exact game rate (1x)")!=std::string::npos&&
            app_c.find("16x")!=std::string::npos,"normal preview-rate menu missing");
    require(app_c.find("64x inspection")==std::string::npos&&
            app_c.find("256x inspection")==std::string::npos&&
            app_c.find("animation_preview_multiplier_=256U")==std::string::npos,
            "superseded G397 global-speed workaround survived");
    require(app_h.find("animation_preview_rate_user_selected_")==std::string::npos&&
            app_h.find("animation_preview_rate_auto_selected_for_buildings_")==std::string::npos,
            "superseded G397 auto-rate state survived");
    require(app_c.find("update_simulation(current_presentation_tick_)")!=std::string::npos&&
            app_c.find("current_building_animation_tick_=current_presentation_tick_")!=std::string::npos,
            "one shared 20 Hz presentation/simulation timeline was split");

    if(argc>=2){
        const auto catalog=thandor::AssetCatalog::scan_game_root(argv[1]);
        const auto* headquarters=catalog.army_preset(300U);
        require(headquarters!=nullptr,"stock Headquarters ARM 300 missing");
        require(headquarters->child0_rotation_step_turn16!=1,
                "ARM +0x10 value 1 is still misread as the Headquarters radar step");
        require(headquarters->child2_vertical_step_q12==16,
                "Headquarters root MDL bounded child channel was not recovered");
        const auto* pump=catalog.army_preset(332U);
        require(pump!=nullptr&&pump->child2_vertical_step_q12==45,
                "Tritium pump root MDL animation step was not recovered");
    }
    std::cout<<"G397 256x workaround superseded and removed by the game-runtime MDL animation correction PASS\n";
    return 0;
} catch(const std::exception& error){
    std::cerr<<"G397_SUPERSESSION_REGRESSION FAIL: "<<error.what()<<'\n';return 1;
}
