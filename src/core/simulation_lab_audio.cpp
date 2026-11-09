#include "thandor/simulation_lab_audio.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include "thandor/model_animation.hpp"
namespace thandor {
SimulationLabAudibleLoops simulation_lab_audible_loops(
    std::span<const SimulationLabSoundRequest> requests,
    const SpatialAudioListener& listener,uint32_t effects_gain_q15,
    const std::function<bool(Vec3f)>& visible) {
    SimulationLabAudibleLoops result;
    for(const auto& request:requests){
        if(!request.sound_table_index)continue;
        if(visible&&!visible(request.world_position)){++result.fog_filtered;continue;}
        const auto p=request.world_position;
        const auto gains=spatial_sound_gains_q15(listener,{p.x,p.y,p.z},
            request.gain_q15,request.maximum_distance_q12,effects_gain_q15);
        if(!gains.audible()){++result.inaudible;continue;}
        result.requests[request.sound_table_index]=request;
    }
    return result;
}
SimulationLabAudioFrame simulation_lab_audio_frame(const SimulationLabRun& run,
    const SimulationLabRuntimeCatalog& catalog,std::span<const SimulationLabOneShotEvent> events,
    const FieldGrid* field) {
    SimulationLabAudioFrame frame;
    const auto loop=[&](SimulationLabSoundRequest request){
        frame.ordered_loops.push_back(request);
        frame.loops[request.sound_table_index]=request;
    };
    for(const auto& entity:run.entities){
        ++frame.entity_owners_examined;
        if(entity.runtime_removed||entity.root_model_terminal||!entity.construction_complete)continue;
        auto profile=entity.runtime_sound_profile;
        const auto flags=entity.runtime_flags|(entity.root_model_component_index<entity.model_components.size()
            ?entity.model_components[entity.root_model_component_index].runtime_flags_ec:0U);
        const bool researching=entity.research_active||(flags&0x40U);
        const auto cls=entity.runtime_model_class_id;
        const bool factory=cls==11U||cls==13U||cls==22U;
        const bool building=cls==22U?entity.production_building:entity.production_behavior_state==1U;
        if((factory&&((flags&1U)||(!researching&&!building)))||
           (cls==4U&&((flags&1U)||!researching))||
           ((cls==10U||cls==14U||cls==16U)&&(flags&1U)))profile.projected_sound_asset_id_primary=0;
        // The class13 door channel is independent of switched-off/research state.
        const bool door=(cls==13U&&entity.production_behavior_state>=2U&&entity.production_behavior_state<=4U)||
            (cls==22U&&entity.airport_initialized&&entity.airport.dispatch_phase!=SimulationClass22DispatchPhase::Idle&&
             entity.airport.dispatch_phase!=SimulationClass22DispatchPhase::Reset);
        for(const auto index:simulation_runtime_looping_sound_indices(profile,
                {true,entity.last_translated,entity.last_turned,door}))
            if(index)loop({index,entity.actor.position,entity.positioned_sound_gain_q15,
                          entity.positioned_sound_maximum_distance_q12});
        // Native method-D recurses into attached turret runtimes. Root movement
        // is not turret rotation; consume the actual yaw/pitch axis velocities.
        // Use the same current attachment transforms as Lab collision/weapon
        // geometry, and only when a resolved field/hierarchy is available.
        if(field&&entity.runtime_model_hierarchy){
            const auto& nodes=*entity.runtime_model_hierarchy;
            const auto poses=simulation_lab_current_model_poses(entity,*field,catalog);
            const auto visit=[&](auto&& self,size_t index)->void{
                if(index>=nodes.size()||index>=poses.size())return;
                const auto& node=nodes[index];
                if(!node.creates_runtime_node()||node.sprite_path.empty())return;
                for(const auto& component:entity.model_components)
                    if(component.source_model_node_index==index&&
                       (component.runtime_removed||component.terminal_runtime_flag8))return;
                if(node.parent_index!=UINT32_MAX){
                    const auto parent=catalog.sprites.find(nodes[node.parent_index].sprite_path);
                    if(parent==catalog.sprites.end()||node.child_slot>=6U||
                       !parent->second.child_translations_q12[node.child_slot]||
                       !model_runtime_world_child_enabled(nodes[node.parent_index].runtime_class_id,node.child_slot))return;
                    if(node.runtime_class_id>=5U&&node.runtime_class_id<=8U){
                        bool turning=false;
                        for(size_t w=0;w<entity.combat_weapons.size()&&w<entity.actor.weapons.size();++w){
                            const auto& weapon=entity.combat_weapons[w].profile;
                            const auto& state=entity.actor.weapons[w];
                            turning|=(weapon.yaw_model_node_index==index&&state.yaw.velocity_turn16!=0)||
                                     (weapon.pitch_model_node_index==index&&state.pitch.velocity_turn16!=0);
                        }
                        const auto& p=node.simulation;
                        if(turning&&p.projected_sound_asset_id_d8)
                            loop({p.projected_sound_asset_id_d8,poses[index].position,
                                  p.positioned_sound_gain_q15,p.positioned_sound_maximum_distance_q12});
                    }
                }
                // Attachment slots are visited in native ascending order,
                // independently of the flattened hierarchy's storage order.
                for(uint32_t slot=0;slot<6U;++slot)
                    for(size_t child=index+1U;child<nodes.size();++child)
                        if(nodes[child].parent_index==index&&nodes[child].child_slot==slot)self(self,child);
            };
            for(size_t i=0;i<nodes.size();++i)if(nodes[i].parent_index==UINT32_MAX)visit(visit,i);
        }
    }
    for(const auto& projectile:run.projectiles){
        ++frame.projectile_owners_examined;
        if(projectile.impact_damage_applied||projectile.runtime_flight_expired)continue;
        const auto found=catalog.shots.find(projectile.shot_definition_id);
        if(found==catalog.shots.end())continue;
        const auto& shot=found->second;const auto index=shot.positioned_sound_table_index();
        if(index)loop({index,simulation_projectile_position(projectile,shot,run.tick),
            shot.positioned_sound_gain_q15,shot.positioned_sound_maximum_distance_q12});
    }
    for(const auto& event:events){
        ++frame.events_examined;
        if(event.kind==SimulationLabOneShotEventKind::GlobalMappedTerrainSound){
            if(event.sound_asset_id)frame.global_one_shots.push_back(
                {event.sound_asset_id,event.sound_listener_faction_index});
            continue;
        }
        if(event.sound_asset_id){
            if(event.sound_gain_and_distance){
                frame.one_shots.push_back({event.sound_asset_id,event.root_world_position,
                    (*event.sound_gain_and_distance)[0],(*event.sound_gain_and_distance)[1]});
            }else if(const auto found=catalog.armies.find(event.army_asset_id);found!=catalog.armies.end())frame.one_shots.push_back({event.sound_asset_id,event.root_world_position,
                found->second.positioned_sound_gain_q15,found->second.positioned_sound_maximum_distance_q12});
        }
        if(!event.effect_id)continue;
        const auto found=catalog.effects.find(event.effect_id);if(found==catalog.effects.end())continue;
        const auto& effect=found->second.definition;const auto index=effect.positioned_sound_table_index();
        if(index)frame.one_shots.push_back({index,event.root_world_position,
            effect.positioned_sound_gain_q15(),effect.positioned_sound_maximum_distance_q12()});
    }
    return frame;
}
}
