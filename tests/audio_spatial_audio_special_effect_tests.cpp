#include "thandor/audio.hpp"
#include "thandor/catalog.hpp"
#include "thandor/context_order.hpp"
#include "thandor/effect.hpp"
#include "thandor/technology.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef THANDOR_SOURCE_DIR
#define THANDOR_SOURCE_DIR "."
#endif

namespace {
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
std::string read_text(const std::filesystem::path& path){
    std::ifstream in(path,std::ios::binary);std::ostringstream out;out<<in.rdbuf();return out.str();
}
bool near_u32(uint32_t value,uint32_t expected,uint32_t tolerance){
    return value>=expected-tolerance&&value<=expected+tolerance;
}
void require_effect(const thandor::AssetCatalog& catalog,uint32_t id,const std::string& role){
    if(id!=0U)require(catalog.effect_definition(id)!=nullptr,"missing "+role+" EFF "+std::to_string(id));
}
void require_shot_pipeline(const thandor::AssetCatalog& catalog,uint32_t shot_id,const std::string& role){
    const auto* shot=catalog.shot_definition(shot_id);
    require(shot!=nullptr,"missing "+role+" SHT "+std::to_string(shot_id));
    require_effect(catalog,shot->launch_effect_id,role+" launch");
    require_effect(catalog,shot->primary_effect_id,role+" primary");
    require_effect(catalog,shot->secondary_effect_id,role+" secondary");
    for(uint32_t id:shot->target_impact_effect_ids)require_effect(catalog,id,role+" target impact");
    for(uint32_t id:shot->terrain_impact_effect_ids)require_effect(catalog,id,role+" terrain impact");
}
}

int main(int argc,char** argv) try {
    // Retail listener-space mixer: front/back stay centered, +/-90 degrees hard-pan, radial gain
    // follows the first cosine quarter, and the persistent effects gain is multiplied in Q15.
    const thandor::SpatialAudioListener listener{{0.0F,0.0F,0.0F},{1.0F,0.0F,0.0F},{0.0F,1.0F,0.0F}};
    const auto centered=thandor::spatial_sound_gains_q15(listener,{0.0F,0.0F,0.0F},0x8000U,409600U,0x8000U,false);
    require(centered.left_q15==0x8000U&&centered.right_q15==0x8000U,"zero-distance source not centered/full");
    const auto right=thandor::spatial_sound_gains_q15(listener,{1.0F,0.0F,0.0F},0x8000U,409600U,0x8000U,false);
    require(right.left_q15<=2U&&right.right_q15>0x7F00U,"right-side source did not hard-pan");
    const auto left=thandor::spatial_sound_gains_q15(listener,{-1.0F,0.0F,0.0F},0x8000U,409600U,0x8000U,false);
    require(left.right_q15<=2U&&left.left_q15>0x7F00U,"left-side source did not hard-pan");
    const auto half=thandor::spatial_sound_gains_q15(listener,{0.0F,50.0F,0.0F},0x8000U,409600U,0x8000U,false);
    require(near_u32(half.left_q15,23170U,16U)&&near_u32(half.right_q15,23170U,16U),"half-distance cosine attenuation drift");
    const auto half_volume=thandor::spatial_sound_gains_q15(listener,{0.0F,0.0F,0.0F},0x8000U,409600U,0x4000U,false);
    require(half_volume.left_q15==0x4000U&&half_volume.right_q15==0x4000U,"master effects volume is not Q15-multiplied");
    require(!thandor::spatial_sound_gains_q15(listener,{0.0F,100.0F,0.0F},0x8000U,409600U,0x8000U,false).audible(),
            "maximum-distance source remained audible");
    const auto reversed=thandor::spatial_sound_gains_q15(listener,{1.0F,0.0F,0.0F},0x8000U,409600U,0x8000U,true);
    require(reversed.right_q15<=2U&&reversed.left_q15>0x7F00U,"reverse-stereo swap drift");

    // Support orders are relation-sensitive: recovery can select a friendly runtime, while a
    // normal weapon keeps the existing hostile-only entity rule and recovery-only actors do not
    // invent terrain attack orders.
    const thandor::ContextOrderActorCapabilities recovery_only{true,false,false,false,1U,true};
    require(thandor::resolve_context_order(recovery_only,1U,true,true)==thandor::ContextOrderKind::AttackPlacement,
            "recovery weapon cannot select friendly placement");
    require(thandor::resolve_context_order(recovery_only,2U,true,true)==thandor::ContextOrderKind::None,
            "recovery-only weapon incorrectly attacks hostile placement");
    require(thandor::resolve_context_order(recovery_only,std::nullopt,false,true)==thandor::ContextOrderKind::MoveToTerrain,
            "movable recovery unit lost normal drive order");

    const auto source=std::filesystem::path(THANDOR_SOURCE_DIR);
    const auto editor=read_text(source/"src/editor/editor_app.cpp");
    const auto editor_header=read_text(source/"src/editor/editor_app.hpp");
    const auto model=read_text(source/"src/core/model.cpp");
    const auto effect=read_text(source/"src/core/effect.cpp");
    require(editor.find("SliderInt(\"Sound volume\"")!=std::string::npos&&
            editor_header.find("sound_effects_gain_q15_{0x8000U}")!=std::string::npos,
            "master Sound volume UI/default missing");
    require(editor.find("spatial_audio_listener()")!=std::string::npos&&
            editor.find("spatial_sound_gains_q15")!=std::string::npos&&
            editor.find("voice->set_gains")!=std::string::npos,
            "live listener-relative loop gain update missing");
    require(editor.find("ModelLookupKeyClass{3U}")!=std::string::npos&&
            editor.find("launch_effect_position=transform_point(")!=std::string::npos&&
            editor.find("play_effect_positioned_sound(*launch_effect,launch_effect_position)")!=std::string::npos&&
            editor.find("tick==impact_tick")!=std::string::npos&&
            editor.find("play_effect_positioned_sound(*impact,projectile.impact_position)")!=std::string::npos,
            "attachment-derived launch / impact spatial effect audio missing");
    require(editor.find("Shot audioRefresh owns a projected spatial slot")!=std::string::npos&&
            editor.find("simulation_projectile_position(projectile,*shot,tick)")!=std::string::npos,
            "in-flight SHT projected-sound refresh missing");
    require(editor.find("placement_weapon_profiles(*preset,index)")!=std::string::npos&&
            editor.find("simulation_weapon_profiles_for_hierarchy")!=std::string::npos,
            "technology-resolved runtime weapon profile path missing");
    require(model.find("positioned_sound_gain_q15 = read_u32(data, offset + 0x78U)")!=std::string::npos&&
            model.find("positioned_sound_maximum_distance_q12 = read_u32(data, offset + 0x7CU)")!=std::string::npos,
            "MDL +78/+7C positioned sound fields missing");
    require(effect.find("offset + 0x288")!=std::string::npos&&effect.find("offset + 0x28C")!=std::string::npos&&
            effect.find("offset + 0x6C")!=std::string::npos&&effect.find("offset + 0x70")!=std::string::npos,
            "SHT/EFF positioned sound fields missing");
    require(editor.find("TerrainGrid_TestProjectedCellMaskBits01Cf gates the retail SHT audioRefresh")!=std::string::npos&&
            editor.find("if(fog_preview_mode_!=0&&!fog_preview_point_visible(world_position))return;")!=std::string::npos,
            "projected SHT/EFF spatial-audio visibility gate missing");

    if(argc>=2){
        const auto catalog=thandor::AssetCatalog::scan_game_root(argv[1]);

        // All stock tower/turret weapon roots must retain a closed SHT -> EFF -> SPR pipeline.
        const std::unordered_map<uint32_t,uint32_t> tower_base_shots{
            {345U,19U},{350U,9U},{351U,18U},{352U,20U},{360U,2U},{361U,17U},
            {362U,27U},{363U,11U},{364U,10U},{365U,30U},{366U,31U},{370U,6U},
            {371U,3U},{372U,12U},{373U,25U},{374U,14U},{376U,32U},{377U,4U}};
        for(const auto& [arm_id,shot_id]:tower_base_shots){
            const auto* preset=catalog.army_preset(arm_id);
            require(preset!=nullptr,"missing tower ARM "+std::to_string(arm_id));
            require(preset->effect_pipeline_validation.ok(),"tower effect pipeline validation failed ARM "+std::to_string(arm_id));
            require(!preset->simulation_weapon_profiles.empty(),"tower base weapon profile missing ARM "+std::to_string(arm_id));
            bool found=false;
            for(const auto& profile:preset->simulation_weapon_profiles)if(profile.shot_definition_id==shot_id)found=true;
            require(found,"tower base SHT mismatch ARM "+std::to_string(arm_id));
            require_shot_pipeline(catalog,shot_id,"tower base ARM "+std::to_string(arm_id));
        }
        uint32_t tower_variant_pipeline_count=0U;
        for(uint32_t arm_id:{345U,350U,351U,352U,360U,361U,362U,363U,364U,365U,366U,
                            370U,371U,372U,373U,374U,375U,376U,377U}){
            const auto* preset=catalog.army_preset(arm_id);require(preset,"missing stock tower ARM");
            uint32_t arm_pipeline_count=0U;
            for(const auto& group:preset->model_variant_groups){
                for(const auto& variant:group.variants){
                    for(const auto& pipeline:variant.weapon_pipelines){
                        ++arm_pipeline_count;++tower_variant_pipeline_count;
                        require_shot_pipeline(catalog,pipeline.shot_definition_id,
                            "tower variant ARM "+std::to_string(arm_id)+" MDL "+
                            std::to_string(pipeline.model_definition_id));
                    }
                }
            }
            require(arm_pipeline_count==preset->effect_pipeline_validation.weapon_pipeline_count,
                    "tower variant pipeline census disagrees with catalog validation ARM "+std::to_string(arm_id));
            const uint32_t expected=arm_id==375U?2U:3U;
            require(arm_pipeline_count==expected,
                    "tower technology pipeline count drift ARM "+std::to_string(arm_id));
        }
        require(tower_variant_pipeline_count==56U,"stock tower technology pipeline census drift");
        const auto* radar=catalog.army_preset(340U);
        require(radar&&radar->simulation_weapon_profiles.empty(),"radar turret invented a projectile weapon");

        // ARM 375 is intentionally SHT-0 in the base hierarchy. Technology 91 replaces that MDL
        // with 1224/SHT1000; the runtime weapon profile must follow the resolved hierarchy too.
        const auto* laser_turret=catalog.army_preset(375U);require(laser_turret,"missing ARM 375");
        require(laser_turret->simulation_weapon_profiles.empty(),"ARM375 base hierarchy unexpectedly armed");
        thandor::FactionTechnologyMask256 laser_mask{};
        thandor::set_faction_technology_unlocked(laser_mask,91U,true);
        const auto laser_hierarchy=thandor::resolve_faction_technology_hierarchy(*laser_turret,laser_mask);
        const auto laser_profiles=catalog.simulation_weapon_profiles_for_hierarchy(laser_hierarchy);
        require(laser_profiles.size()==1U&&laser_profiles[0].shot_definition_id==1000U,
                "ARM375 tech-resolved SHT1000 weapon profile missing");
        for(uint32_t arm_id:{2U,112U,170U,183U,251U,375U}){
            const auto* preset=catalog.army_preset(arm_id);require(preset,"missing technology-only laser ARM");
            const auto hierarchy=thandor::resolve_faction_technology_hierarchy(*preset,laser_mask);
            const auto profiles=catalog.simulation_weapon_profiles_for_hierarchy(hierarchy);
            require(!profiles.empty()&&profiles.front().shot_definition_id==1000U,
                    "technology-only laser failed dynamic profile ARM "+std::to_string(arm_id));
        }

        // Recovery beam: exact stock owners, direct-line SHT33, negative target-class values and
        // its launch/target visual effects. Negative damage is a support relation, not hostility.
        const auto* repair=catalog.shot_definition(33U);require(repair,"missing recovery SHT33");
        require(repair->trajectory_kind()==thandor::ShotTrajectoryMode::DirectLine&&repair->is_repair_or_recovery_shot(),
                "SHT33 recovery semantics drift");
        require(repair->launch_effect_id==85U,"SHT33 launch EFF drift");
        for(uint32_t id:repair->target_impact_effect_ids)require(id==37U,"SHT33 target recovery EFF drift");
        require(repair->sprite_path=="spr/schuesse/siiob0.spr","SHT33 beam sprite drift");
        for(uint32_t arm_id:{38U,88U,139U,207U,274U}){
            const auto* preset=catalog.army_preset(arm_id);require(preset,"missing repair-module ARM");
            bool owns=false;for(const auto& profile:preset->simulation_weapon_profiles)owns|=profile.shot_definition_id==33U;
            require(owns,"repair module lost SHT33 ARM "+std::to_string(arm_id));
        }
        const auto* repair_launch=catalog.effect_definition(85U);const auto* repair_hit=catalog.effect_definition(37U);
        require(repair_launch&&repair_launch->sprite_path=="spr/effekte/emtrm0.spr","repair launch EFF85 drift");
        require(repair_hit&&repair_hit->sprite_path=="spr/effekte/ehund0.spr","repair target EFF37 drift");

        // The stock fixed-range silo is another special chain: launch EFF82 emits linked SHT22;
        // target EFF33 in turn emits linked EFF90. Preserve the recursive visual graph and audio.
        const auto* silo=catalog.army_preset(395U);require(silo,"missing AR-R Silo ARM395");
        bool silo21=false;for(const auto& profile:silo->simulation_weapon_profiles)silo21|=profile.shot_definition_id==21U;
        require(silo21,"AR-R Silo lost SHT21");
        const auto* special=catalog.shot_definition(21U);require(special,"missing SHT21");
        require(special->trajectory_kind()==thandor::ShotTrajectoryMode::FixedRange&&special->launch_effect_id==82U&&special->secondary_effect_id==105U,
                "AR-R Silo fixed-range chain drift");
        const auto* e82=catalog.effect_definition(82U);const auto* e33=catalog.effect_definition(33U);
        require(e82&&e82->linked_shot_present!=0U&&e82->linked_shot_id==22U&&
                e82->positioned_sound_table_index()==21U&&e82->positioned_sound_gain_q15()==0x8000U&&
                e82->positioned_sound_maximum_distance_q12()==50000U,"EFF82 special launch/audio chain drift");
        require(e33&&e33->linked_effect_present!=0U&&e33->linked_effect_id==90U&&
                e33->positioned_sound_table_index()==19U&&e33->positioned_sound_maximum_distance_q12()==50000U,
                "EFF33 special target/audio chain drift");

        std::cout<<" stock_tower_repair_special_census=PASS";
    }
    std::cout<<" V451_SPATIAL_AUDIO_SPECIAL_EFFECT PASS\n";
    return 0;
} catch(const std::exception& error){
    std::cerr<<"V451 spatial audio/special effect checks failed: "<<error.what()<<'\n';
    return 1;
}
