#include "thandor/catalog.hpp"
#include "thandor/effect.hpp"
#include "thandor/model.hpp"
#include "thandor/simulation.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string read_text(const std::filesystem::path& path){
    std::ifstream stream(path,std::ios::binary);std::ostringstream text;text<<stream.rdbuf();return text.str();
}
}

int main(int argc,char** argv) try {
    thandor::SimulationActorState tank;
    require(!thandor::simulation_tick_movement_ground_effect_countdown(tank,false,7U),
            "stationary ground-effect owner advanced its countdown");
    require(tank.movement_ground_effect_countdown_ticks==0,
            "stationary ground-effect owner changed initial countdown");
    require(thandor::simulation_tick_movement_ground_effect_countdown(tank,true,7U),
            "zero-initialized tank ground-effect timer did not emit on first moved tick");
    require(tank.movement_ground_effect_countdown_ticks==7,
            "tank ground-effect timer did not reload the MDL interval");
    for(unsigned step=0;step<6U;++step)
        require(!thandor::simulation_tick_movement_ground_effect_countdown(tank,true,7U),
                "tank ground-effect timer emitted before its 7-tick cadence");
    require(thandor::simulation_tick_movement_ground_effect_countdown(tank,true,7U),
            "tank ground-effect timer did not emit on its 7th movement-gated tick");

    thandor::SimulationActorState walker;
    require(thandor::simulation_tick_movement_ground_effect_countdown(walker,true,0U),
            "class-3 zero-interval footprint did not emit on moved tick");
    require(thandor::simulation_tick_movement_ground_effect_countdown(walker,true,0U),
            "class-3 zero-interval footprint did not emit on the next moved tick");
    require(!thandor::simulation_tick_movement_ground_effect_countdown(walker,false,0U),
            "class-3 footprint emitted while root was stationary");

    const auto source=std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto editor=read_text(source/"src/editor/editor_app.cpp");
    const auto header=read_text(source/"src/editor/editor_app.hpp");
    require(editor.find("node.runtime_class_id==2U||node.runtime_class_id==3U")!=std::string::npos,
            "movement-ground owners are still admitted to generic timed-effect scheduling");
    require(editor.find("MovementGroundEffectInstance{index,effect_id,tick,origin,orientation}")!=std::string::npos,
            "editor does not snapshot movement-ground effect world poses");
    require(editor.find("ground_effect_root_changed")!=std::string::npos,
            "editor does not gate movement-ground emitters from solved root X/Y/yaw changes");
    require(editor.find("return !effect.ground_mark||show_movement_ground_marks_;")!=std::string::npos,
            "ground-mark history does not keep the dynamic presentation loop alive");
    require(header.find("bool show_movement_ground_marks_{true}")!=std::string::npos,
            "moving-unit ground marks are not enabled by default");

    if(argc>=2){
        const auto catalog=thandor::AssetCatalog::scan_game_root(argv[1]);
        std::size_t tanks=0,walkers=0;
        std::map<uint32_t,std::size_t> tank_effects,walker_effects;
        std::map<uint32_t,std::size_t> tank_intervals;
        for(const auto& preset:catalog.army_presets()){
            if(preset.browser_kind!=thandor::PlacementBrowserKind::Unit)continue;
            if(preset.runtime_model_class_id!=2U&&preset.runtime_model_class_id!=3U)continue;
            const auto hierarchy=preset.resolved_model_hierarchy.empty()
                ?std::span<const thandor::ModelVisualNode>(preset.model_hierarchy)
                :std::span<const thandor::ModelVisualNode>(preset.resolved_model_hierarchy);
            const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& node){
                return node.parent_index==std::numeric_limits<uint32_t>::max();
            });
            require(root!=hierarchy.end(),"movement-ground stock preset lost its root MDL");
            const auto sprite=thandor::SpriteAsset::parse(catalog.load(root->sprite_path));
            if(preset.runtime_model_class_id==2U){
                ++tanks;++tank_effects[root->simulation.timed_effect_id];
                ++tank_intervals[root->simulation.timed_effect_interval_ticks];
                require(root->simulation.timed_water_effect_id==0U,
                        "stock tracked tank unexpectedly gained a water tread effect");
                require(root->simulation.timed_effect_random_ticks==0U,
                        "stock tracked tank ground-mark cadence gained a random interval");
                require(sprite.type6_attachment_points().size()==1U,
                        "stock tracked tank no longer has exactly one class-6 ground emitter");
            }else{
                ++walkers;++walker_effects[root->simulation.timed_effect_id];
                require(root->simulation.timed_water_effect_id==147U,
                        "stock walker lost the water-contact ground effect");
                require(root->simulation.timed_effect_interval_ticks==0U&&
                        root->simulation.timed_effect_random_ticks==0U,
                        "stock walker footprint cadence is no longer movement-call immediate");
                require(sprite.type6_attachment_points().empty(),
                        "stock walker unexpectedly gained a class-6 footprint emitter");
            }
        }
        require(tanks==24U,"stock tracked-unit count changed");
        require(tank_effects[135U]==8U&&tank_effects[136U]==10U&&tank_effects[137U]==6U,
                "Badger/Panther/Goliath tread-effect ownership changed");
        require(tank_intervals[7U]==8U&&tank_intervals[9U]==10U&&tank_intervals[11U]==6U,
                "Badger/Panther/Goliath tread cadence changed");
        require(walkers==28U,"stock articulated walker count changed");
        require(walker_effects[144U]==8U&&walker_effects[145U]==12U&&walker_effects[146U]==8U,
                "Emu/Orang/Gorilla footprint-effect ownership changed");

        const auto* tread_a=catalog.effect_definition(135U);
        const auto* tread_b=catalog.effect_definition(136U);
        const auto* tread_c=catalog.effect_definition(137U);
        const auto* foot=catalog.effect_definition(144U);
        require(tread_a&&tread_b&&tread_c&&foot,"stock ground-mark EFF definition missing");
        require(thandor::effect_lifetime_ticks(*tread_a)==125U&&
                thandor::effect_lifetime_ticks(*tread_b)==157U&&
                thandor::effect_lifetime_ticks(*tread_c)==157U,
                "tracked-unit ground-mark lifetimes changed");
        require(tread_a->alpha_fade_out_ticks==30U&&tread_b->alpha_fade_out_ticks==30U&&
                tread_c->alpha_fade_out_ticks==30U&&foot->alpha_fade_out_ticks==30U,
                "ground-mark fade-out duration changed");
        require(thandor::effect_lifetime_ticks(*foot)==157U,
                "walker footprint lifetime changed");
        require(thandor::effect_alpha_at_age(*tread_a,124U)>0U&&
                thandor::effect_alpha_at_age(*tread_a,125U)==0U,
                "tracked-unit tread effect no longer expires at the recovered boundary");
        std::cout<<" tanks="<<tanks<<" walkers="<<walkers;
    }

    std::cout<<" V451_MOVEMENT_GROUND_MARK_HISTORY PASS\n";
    return 0;
} catch(const std::exception& error){
    std::cerr<<"V451 movement ground-mark history checks failed: "<<error.what()<<'\n';
    return 1;
}
