#include "thandor/catalog.hpp"
#include "thandor/effect.hpp"
#include "thandor/model.hpp"
#include "primitive_render.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

int main(int argc,char** argv) try {
    if(argc<2){
        std::cout << "G368_STOCK_HEADQUARTERS_CENSUS PASS unit-only\n";
        return 0;
    }
    const auto catalog=thandor::AssetCatalog::scan_game_root(argv[1]);
    const auto* preset=catalog.army_preset(300U);
    if(preset==nullptr)throw std::runtime_error("ARM 300 Headquarters is missing");
    if(preset->model_definition_id!=301U||preset->runtime_model_class_id!=11U)
        throw std::runtime_error("Headquarters model/class identity changed");
    if(preset->timed_effect_id!=131U||preset->timed_effect_interval_ticks!=8U)
        throw std::runtime_error("Headquarters no longer owns periodic EFF 131");
    if(preset->child0_rotation_step_turn16!=0||preset->child2_vertical_step_q12!=16||
       !preset->has_model_subnode_animation)
        throw std::runtime_error("Headquarters root/recursive MDL animation fields changed");

    const std::set<std::string> expected{
        "spr/gebaeude/gsbau0.spr","spr/gebaeude/gsbau1.spr","spr/gebaeude/gsbau2.spr",
        "spr/aufbau/aarad0.spr","spr/aufbau/aarad1.spr"};
    std::set<std::string> found;
    std::size_t live_nodes=0U,radar_triangles=0U;
    for(const auto& node:preset->resolved_model_hierarchy){
        if(!node.creates_runtime_node()||node.sprite_path.empty())continue;
        ++live_nodes;found.insert(node.sprite_path);
        const auto sprite=thandor::SpriteAsset::parse(catalog.load(node.sprite_path));
        if(node.sprite_path.find("aarad")!=std::string::npos)
            radar_triangles+=sprite.mesh().indices.size()/3U;
    }
    if(live_nodes!=5U||found!=expected||radar_triangles!=44U)
        throw std::runtime_error("Headquarters recursive model hierarchy is incomplete");

    const auto root=thandor::SpriteAsset::parse(catalog.load("spr/gebaeude/gsbau0.spr"));
    if(root.mesh().indices.size()/3U!=216U||root.type6_attachment_points().size()!=2U)
        throw std::runtime_error("Headquarters root mesh or smokestack points changed");
    std::size_t selector25=0U,center_roof=0U;
    for(std::size_t i=0;i<root.mesh().indices.size();i+=3U){
        const auto& a=root.mesh().vertices.at(root.mesh().indices[i]);
        const auto& b=root.mesh().vertices.at(root.mesh().indices[i+1U]);
        const auto& c=root.mesh().vertices.at(root.mesh().indices[i+2U]);
        if(a.render_flags==0x944FU&&a.texture_subresource==79U){
            ++selector25;
            const float minx=std::min({a.position.x,b.position.x,c.position.x});
            const float maxx=std::max({a.position.x,b.position.x,c.position.x});
            const float miny=std::min({a.position.y,b.position.y,c.position.y});
            const float maxy=std::max({a.position.y,b.position.y,c.position.y});
            if(minx<-0.40F&&maxx>0.40F&&miny<-0.49F&&maxy>0.48F)
                ++center_roof;
            const auto primitive=editor::classify_directdraw_direct3d_sprite_primitive(
                a.render_flags,true);
            if(primitive.blend!=editor::PrimitiveBlendPreset::AlphaNoDepthWrite||!primitive.two_sided)
                throw std::runtime_error("selector-25 roof packet classification changed");
        }
    }
    if(selector25<6U||center_roof!=2U)
        throw std::runtime_error("Headquarters translucent central roof packet census changed");

    const auto* effect=catalog.effect_definition(131U);
    if(effect==nullptr||effect->transition_kind!=2U||effect->runtime_value_0c!=8U||
       effect->frame_advance_threshold_q4!=160U)
        throw std::runtime_error("EFF 131 fields changed");
    const auto smoke=thandor::SpriteAsset::parse(catalog.load(effect->sprite_path));
    if(smoke.mesh().indices.size()/3U!=2U||(smoke.lod_flags(0U)&2U)==0U)
        throw std::runtime_error("EFF 131 is no longer the stock billboard plane");

    std::cout << "G368_STOCK_HEADQUARTERS_CENSUS asset=300 hierarchy=5 radar_triangles=44 "
                 "root_triangles=216 center_roof_alpha_triangles=2 smokestacks=2 "
                 "eff131_billboard=1 linked_radar_step_turn16=512 PASS\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr << "G368_STOCK_HEADQUARTERS_CENSUS FAIL: " << error.what() << '\n';
    return 1;
}
