#include "thandor/catalog.hpp"
#include "thandor/model_animation.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
std::string read_text(const std::filesystem::path& path){
    std::ifstream stream(path,std::ios::binary);
    if(!stream)throw std::runtime_error("unable to read source file: "+path.string());
    return {std::istreambuf_iterator<char>(stream),std::istreambuf_iterator<char>()};
}
void require(bool v,const std::string& m){if(!v)throw std::runtime_error(m);}
std::size_t find_node(const std::vector<thandor::ModelVisualNode>& h,uint32_t model,uint32_t cls){
    for(std::size_t i=0;i<h.size();++i)if(h[i].model_definition_id==model&&h[i].runtime_class_id==cls)return i;
    return std::numeric_limits<std::size_t>::max();
}
}

int main(int argc,char** argv) try {
    const auto root=std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto catalog_source=read_text(root/"src/core/catalog.cpp");
    const auto editor_source=read_text(root/"src/editor/editor_app.cpp");
    const auto editor_header=read_text(root/"src/editor/editor_app.hpp");
    const auto animation_header=read_text(root/"include/thandor/model_animation.hpp");

    require(catalog_source.find("read_i32(bytes, offset + 0x10U)")==std::string::npos,
            "top-level ARM +0x10 is still parsed as a child rotation rate");
    require(catalog_source.find("root_node->simulation.yaw_max_velocity_turn16")!=std::string::npos&&
            catalog_source.find("hierarchy_has_authored_model_subnode_animation")!=std::string::npos,
            "catalog does not derive animation from the recursive selected MDL hierarchy");
    require(animation_header.find("runtime_class_id == 4U")!=std::string::npos&&
            animation_header.find("runtime_class_id == 11U")!=std::string::npos&&
            animation_header.find("runtime_class_id == 13U")!=std::string::npos&&
            animation_header.find("runtime_class_id == 14U")!=std::string::npos,
            "binary-proven generic callback classes are incomplete");
    require(editor_source.find("model_runtime_child_yaw_step_turn16")!=std::string::npos&&
            editor_source.find("animation_owner=hierarchy[node.parent_index]")!=std::string::npos&&
            editor_source.find("authored_parent_node.simulation")!=std::string::npos,
            "renderer does not apply per-parent linked-MDL animation channels");
    require(editor_source.find("64x inspection")==std::string::npos&&
            editor_source.find("256x inspection")==std::string::npos&&
            editor_header.find("animation_preview_rate_auto_selected_for_buildings_")==std::string::npos,
            "global-speed workaround was not fully removed");

    if(argc>=2){
        const auto catalog=thandor::AssetCatalog::scan_game_root(argv[1]);
        const auto* headquarters=catalog.army_preset(300U);
        require(headquarters!=nullptr,"stock Headquarters ARM 300 missing");
        const auto& hq=headquarters->resolved_model_hierarchy.empty()
            ?headquarters->model_hierarchy:headquarters->resolved_model_hierarchy;
        const auto radar_index=find_node(hq,229U,4U);
        require(radar_index!=std::numeric_limits<std::size_t>::max(),
                "Headquarters linked MDL 229 class-4 radar root missing");
        const auto& radar=hq[radar_index];
        require(radar.simulation.yaw_max_velocity_turn16==512,
                "Headquarters linked radar did not retain MDL +0x10 step 512");
        require(thandor::model_runtime_parent_has_live_child(hq,radar_index,0U),
                "Headquarters radar runtime has no live child[0]");
        require(thandor::model_runtime_child_yaw_step_turn16(radar,0U)==512,
                "class-4 radar child[0] does not use its owning MDL profile");
        constexpr double revolution_seconds=65536.0/(512.0*20.0);
        static_assert(revolution_seconds>6.39&&revolution_seconds<6.41);
        require(headquarters->child0_rotation_step_turn16==0&&
                headquarters->child2_vertical_step_q12==16&&
                headquarters->child2_vertical_min_q12==-100&&
                headquarters->child2_vertical_max_q12==100,
                "Headquarters compatibility profile does not match root MDL 301");
        require(headquarters->has_model_subnode_animation,
                "Headquarters recursive animation census is false");

        const auto* pump=catalog.army_preset(332U);
        require(pump!=nullptr,"stock Tritium pump ARM 332 missing");
        const auto& ph=pump->resolved_model_hierarchy.empty()?pump->model_hierarchy:pump->resolved_model_hierarchy;
        require(!ph.empty()&&ph.front().model_definition_id==302U&&ph.front().runtime_class_id==14U,
                "Tritium pump root MDL 302 class 14 missing");
        require(ph.front().simulation.pitch_max_velocity_turn16==45&&
                ph.front().simulation.pitch_min_turn16==-800&&
                ph.front().simulation.pitch_max_turn16==200,
                "Tritium pump MDL bounded child[2] profile changed");
        require(thandor::model_runtime_child_uses_bounded_vertical_channel(ph.front(),2U)&&
                thandor::model_runtime_parent_has_live_child(ph,0U,2U)&&
                pump->has_model_subnode_animation,
                "Tritium pump child[2] animation route is incomplete");
    }

    std::cout<<"G398 game-runtime recursive MDL building animation pipeline PASS\n";
    return 0;
} catch(const std::exception& error){
    std::cerr<<"G398_GAME_RUNTIME_BUILDING_ANIMATION_PIPELINE FAIL: "<<error.what()<<'\n';return 1;
}
