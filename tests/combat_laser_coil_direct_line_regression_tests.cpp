#include "thandor/catalog.hpp"
#include "thandor/effect.hpp"
#include "thandor/model.hpp"
#include "thandor/simulation.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
std::string read_text(const std::filesystem::path& path){
    std::ifstream in(path,std::ios::binary);std::ostringstream out;out<<in.rdbuf();return out.str();
}
bool near(float a,float b,float eps=1.0e-6F){return std::abs(a-b)<=eps;}
}

int main(int argc,char** argv) try {
    // Direct-line shots freeze the world-space launch ray. A later entity movement may cause the
    // retail maintenance ray to hit a different surface, but it never retargets the existing beam
    // toward the entity's new coordinate. Mode 2 is the only recovered live-target follower.
    thandor::ShotDefinitionRecord direct;
    direct.trajectory_mode=0;
    direct.launch_speed_q12=thandor::float_to_q12(2.0F);
    direct.projectile_lifetime_ticks=20U;
    thandor::SimulationProjectileState beam;
    beam.launch_position={1.0F,2.0F,3.0F};
    beam.target_position={11.0F,2.0F,3.0F};
    beam.impact_position=beam.target_position;
    beam.impact_kind=thandor::SimulationImpactKind::TargetModelImpactClass;
    const auto frozen_target=beam.target_position;
    const auto frozen_impact=beam.impact_position;
    const float frozen_length=thandor::simulation_mode0_beam_length_world(beam,direct);
    thandor::simulation_update_projectile_live_target(beam,direct,{21.0F,7.0F,4.0F});
    require(near(beam.target_position.x,frozen_target.x)&&near(beam.target_position.y,frozen_target.y)&&
            near(beam.target_position.z,frozen_target.z),"mode-0 beam target moved after launch");
    require(near(beam.impact_position.x,frozen_impact.x)&&near(beam.impact_position.y,frozen_impact.y)&&
            near(beam.impact_position.z,frozen_impact.z),"mode-0 impact moved after launch");
    require(near(thandor::simulation_mode0_beam_length_world(beam,direct),frozen_length),
            "mode-0 beam length tracked a moved entity");

    thandor::ShotDefinitionRecord fixed=direct;
    fixed.trajectory_mode=2;
    fixed.fixed_range_transition_age_threshold_ticks=2U;
    beam.target_position=frozen_target;
    beam.impact_position=frozen_impact;
    beam.lifetime_ticks=20U;
    beam.travel_ticks=20U;
    thandor::simulation_update_projectile_live_target(beam,fixed,{21.0F,7.0F,4.0F});
    require(near(beam.target_position.x,21.0F)&&near(beam.target_position.y,7.0F),
            "mode-2 live target stopped following entity");

    const auto source=std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto editor=read_text(source/"src/editor/editor_app.cpp");
    const auto simulation=read_text(source/"src/core/simulation.cpp");
    require(editor.find("if(!projectile.live_target_placement_index||shot->trajectory_mode!=2)continue;")!=std::string::npos,
            "editor still refreshes non-mode-2 projectile command targets");
    require(simulation.find("if (shot.trajectory_mode != 2) return;")!=std::string::npos,
            "core live-target helper is not trajectory-gated");
    require(editor.find("ModelLookupKeyClass{4U}")!=std::string::npos&&
            editor.find("launch_shading_position")!=std::string::npos&&
            editor.find("sub(position,projectile.launch_position)")!=std::string::npos,
            "shot class-4 shading attachment/translation correspondence missing");
    require(editor.find("launch_effect_position")!=std::string::npos&&
            editor.find("ModelLookupKeyClass{3U}")!=std::string::npos&&
            editor.find("play_effect_positioned_sound(*launch_effect,launch_effect_position)")!=std::string::npos,
            "shot class-3 launch EFF audio producer point missing");
    require(editor.find("simulation_refresh_mode0_projectile_raycast(projectile,*shot,tick)")!=std::string::npos&&
            editor.find("simulation_model_raycast_nearest")!=std::string::npos&&
            editor.find("ShotModelRuntimeMaintenance_UpdateProjectileMotionCollisionAndEffects re-runs")!=std::string::npos,
            "mode-0 direct-line maintenance no longer re-rays the shared runtime owner list");

    if(argc>=2){
        const auto catalog=thandor::AssetCatalog::scan_game_root(argv[1]);
        const auto* coil=catalog.army_preset(345U);require(coil,"missing Laser coil ARM345");
        require(coil->runtime_model_class_id==10U,"Laser coil root runtime class drift");
        bool owns19=false;
        for(const auto& profile:coil->simulation_weapon_profiles){
            if(profile.shot_definition_id!=19U)continue;
            owns19=true;
            require(profile.runtime_class_id==7U,"Laser coil SHT19 not owned by nested class-7 weapon");
        }
        require(owns19,"Laser coil lost SHT19");
        std::array<bool,3> laser_coil_variants{};
        for(const auto& group:coil->model_variant_groups){
            for(const auto& variant:group.variants){
                for(const auto& pipeline:variant.weapon_pipelines){
                    if(pipeline.shot_definition_id==19U)laser_coil_variants[0]=true;
                    if(pipeline.shot_definition_id==1019U)laser_coil_variants[1]=true;
                    if(pipeline.shot_definition_id==2019U)laser_coil_variants[2]=true;
                }
            }
        }
        require(laser_coil_variants[0]&&laser_coil_variants[1]&&laser_coil_variants[2],
                "Laser coil base/technology SHT19/1019/2019 chain drift");
        const auto* shot=catalog.shot_definition(19U);require(shot,"missing Laser coil SHT19");
        require(shot->trajectory_kind()==thandor::ShotTrajectoryMode::DirectLine,
                "Laser coil SHT19 is no longer direct-line");
        require(shot->launch_effect_id==81U&&shot->primary_effect_id==9U,
                "Laser coil launch/primary EFF chain drift");
        for(uint32_t id:shot->target_impact_effect_ids)require(id==31U,"Laser coil target EFF31 drift");
        require(shot->sprite_path=="spr/schuesse/sltrm0.spr","Laser coil shot SPR drift");
        const auto sprite=thandor::SpriteAsset::parse(catalog.load(shot->sprite_path));
        const auto* launch=sprite.find_lookup_record(thandor::ModelLookupKeyIndex{0U},
                                                     thandor::ModelLookupKeyClass{3U});
        const auto* shading=sprite.find_lookup_record(thandor::ModelLookupKeyIndex{0U},
                                                      thandor::ModelLookupKeyClass{4U});
        require(launch!=nullptr,"Laser coil shot lost class-3 launch attachment");
        require(shading!=nullptr,"Laser coil shot lost class-4 shading attachment");
        require(std::abs(shading->local_translation.z)>1.0e-6F,
                "Laser coil class-4 shading attachment unexpectedly collapsed to origin");
        const auto* e81=catalog.effect_definition(81U);const auto* e9=catalog.effect_definition(9U);
        const auto* e31=catalog.effect_definition(31U);
        require(e81&&e81->sprite_path=="spr/effekte/emtrm0.spr","Laser coil launch EFF81 drift");
        require(e9&&e9->sprite_path=="spr/effekte/epexa0.spr","Laser coil primary EFF9 drift");
        require(e31&&e31->linked_effect_present!=0U&&e31->linked_effect_id==90U,
                "Laser coil target EFF31 -> EFF90 chain drift");
        std::cout<<" laser_coil_stock=PASS";
    }
    std::cout<<" V451_LASER_COIL_DIRECT_LINE PASS\n";
    return 0;
} catch(const std::exception& error){
    std::cerr<<"V451 Laser coil direct-line checks failed: "<<error.what()<<'\n';
    return 1;
}
