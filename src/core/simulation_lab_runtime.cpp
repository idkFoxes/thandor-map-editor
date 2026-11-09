#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_derived_selection_metrics.hpp"
#include "thandor/audio.hpp"
#include <algorithm>
#include <bit>
#include <cctype>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace thandor {
namespace {
uint32_t fixed_length3_floor_q12(const std::array<int32_t,3>& value) noexcept {
    const auto square=[](int32_t component)->uint64_t {
        const int64_t wide=component;return static_cast<uint64_t>(wide*wide);
    };
    const uint64_t squared=square(value[0])+square(value[1])+square(value[2]);
    uint64_t result=static_cast<uint64_t>(std::sqrt(static_cast<long double>(squared)));
    while(result+1U<=std::numeric_limits<uint32_t>::max()&&result+1U<=squared/(result+1U))++result;
    while(result!=0U&&result>squared/result)--result;
    return static_cast<uint32_t>(std::min<uint64_t>(result,std::numeric_limits<uint32_t>::max()));
}
const ModelVisualRecord* definition(const SimulationLabRuntimeCatalog& catalog,uint32_t id) {
    const auto found=catalog.definitions.find(id);
    return found==catalog.definitions.end()?nullptr:&found->second;
}
void copy_component_definition(SimulationLabModelComponentState& component,
                               const ModelVisualRecord& model,bool creation) {
    const auto& p=model.simulation;
    component.model_definition_id=model.definition_id;
    component.definition_flags68=p.runtime_render_flags;
    component.runtime_class_id=model.runtime_class_id;
    component.placement_contact_kind=p.placement_contact_kind;
    component.maximum_integrity=p.maximum_integrity;
    component.health_regeneration_per_step=p.health_regeneration_per_step;
    component.resource_support_class=model.runtime_class_id==14U&&
        p.class_specific_c0_raw>=0&&p.class_specific_c0_raw<=1
        ?static_cast<uint32_t>(p.class_specific_c0_raw):32U;
    component.resource_extraction_weight_u8=p.resource_extraction_weight_u8&0xFFU;
    component.terminal_attachment_effect_ids=p.terminal_attachment_effect_ids;
    if(creation) {
        // These faction contributions are registered by model creation and
        // removed by model release. A technology swap only changes the selected
        // definition, health and derived army metrics; it does not re-register
        // storage or power from the replacement record.
        component.resource_storage_selector=p.resource_storage_selector;
        component.resource_storage_capacity_contribution_q4=p.resource_storage_capacity_contribution_q4;
        component.has_resource_storage_capacity_contribution=p.has_resource_storage_capacity_contribution;
        component.energy_generation_capacity_q4=p.energy_generation_capacity_q4;
        component.dismantle_countdown=0;
        component.animated_subnode_ticks=0;
        component.last_model_callback_tick=component.terminal_effect_not_before_tick=0;
        if(component.runtime_class_id==3U)component.timed_effect_countdown=0x7FFFFFFFU;
        component.integrity=component.maximum_integrity;
        component.retail_energy_demand_q4=p.retail_energy_demand_q4;
        component.terminal_attachment_countdowns=p.terminal_attachment_delay_ticks;
    }
    // Runtime flags, regeneration delay, current Energy load and emitter countdowns survive a swap.
}
void rebuild_runtime_fields(SimulationLabEntity& entity,const SimulationLabRuntimeCatalog& catalog,
                            bool creation) {
    if(!entity.runtime_model_hierarchy||entity.runtime_model_hierarchy->empty())return;
    const auto& hierarchy=*entity.runtime_model_hierarchy;
    const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& n){
        return n.parent_index==std::numeric_limits<uint32_t>::max();});
    if(root==hierarchy.end())return;
    const auto* model=definition(catalog,root->model_definition_id);
    if(!model)return;
    const auto& p=model->simulation;
    entity.runtime_model_class_id=model->runtime_class_id;
    entity.destroyed_replacement_army_asset_id=p.destroyed_replacement_army_asset_id;
    if(model->runtime_class_id==12U) {
        entity.class12_vertical_step_q12_per_tick=p.class_step_q12;
        const auto sprite=catalog.sprites.find(root->sprite_path);
        if(sprite!=catalog.sprites.end()&&sprite->second.has_bounds) {
            const auto height=sprite->second.bounds_max.z-sprite->second.bounds_min.z;
            entity.class12_model_height_q12=std::max<std::int32_t>(0,float_to_q12(height));
        }
    }
    entity.shot_impact_class_index=model->shot_impact_class_index;
    entity.has_traversal_profile=model->has_traversal_profile;
    entity.traversal_low_band_index=model->traversal_low_band_index;
    entity.traversal_high_band_index=model->traversal_high_band_index;
    entity.motion.placement_contact_kind=p.placement_contact_kind;
    entity.motion.placement_height_offset_q12=p.placement_height_offset_q12;
    entity.motion.collision_radius_q12=p.movement_collision_radius_q12;
    const auto cls=model->runtime_class_id;
    const bool mobile=(cls>=1U&&cls<=3U)||cls==17U||cls==18U||cls==19U||cls==21U;
    entity.motion.ground_speed_control=cls==1U||cls==2U||cls==17U||cls==18U||cls==19U;
    entity.motion.ground_acceleration_q12=static_cast<uint32_t>(p.runtime_field_18_raw);
    entity.motion.ground_slope_threshold_turn16=p.ground_slope_threshold_turn16;
    entity.motion.ground_secondary_slope_threshold_turn16=p.ground_secondary_slope_threshold_turn16;
    if(mobile) {
        entity.motion.speed_q12_per_tick=p.class_step_q12;
        entity.motion.yaw_max_velocity_turn16=p.yaw_max_velocity_turn16;
        entity.motion.yaw_acceleration_turn16=p.yaw_acceleration_turn16;
        entity.motion.heading_interpolation_distance_q12=p.ground_heading_interpolation_distance_q12;
        entity.motion.far_heading_error_limit_turn16=p.ground_far_heading_error_limit_turn16;
        entity.motion.near_heading_error_limit_turn16=p.ground_near_heading_error_limit_turn16;
    }
    entity.movement_ground_effect_interval_ticks=p.timed_effect_interval_ticks;
    entity.movement_ground_land_effect_id=p.timed_effect_id;
    entity.movement_ground_water_effect_id=p.timed_water_effect_id;
    if(model->runtime_class_id==18U) {
        const auto effect=catalog.conversion_effect_lifetimes.find(p.class_specific_c4_raw);
        entity.class18_conversion_effect_lifetime_ticks=effect==catalog.conversion_effect_lifetimes.end()?0U:effect->second;
    }
    if(!mobile)entity.motion.speed_q12_per_tick=0;
    entity.water_damage_multiplier=p.water_damage_multiplier;
    entity.water_damage_threshold_q12=p.water_damage_threshold_q12;
    entity.vertical_deployment_step_q12_per_tick=p.vertical_deployment_step_q12_per_tick;
    entity.vertical_deployment_travel_limit_q12=p.vertical_deployment_travel_limit_q12;
    entity.class23_deployment_sound_asset_id=p.runtime_class_effect_sound_asset_id_26c;
    entity.articulated_root_height_q12=p.articulated_root_height_q12;
    entity.articulated_contact_interpolation_rate_q12=p.articulated_contact_interpolation_rate_q12;
    entity.articulated_contact_lift_q12=p.articulated_contact_lift_q12;
    entity.articulated_contact_steering_limit_turn16=p.articulated_contact_steering_limit_turn16;
    entity.articulated_contact_sound_asset_id=p.articulated_contact_sound_asset_id;
    entity.runtime_sound_profile={model->runtime_class_id,p.projected_sound_asset_id_d0,
        p.projected_sound_asset_id_d8,p.projected_sound_asset_id_primary,p.projected_sound_asset_id_secondary};
    entity.positioned_sound_gain_q15=p.positioned_sound_gain_q15;
    entity.positioned_sound_maximum_distance_q12=p.positioned_sound_maximum_distance_q12;
    entity.runtime_class_specific_c0_raw=p.class_specific_c0_raw;
    entity.runtime_class_specific_c4_raw=p.class_specific_c4_raw;
    entity.runtime_class_specific_19c_raw=p.class_specific_19c_raw;
    entity.runtime_class_specific_1a8_raw=p.class_specific_1a8_raw;
    entity.production_compatibility_mask=p.class_specific_c4_raw;
    entity.production_storage_capacity=std::min(p.class_specific_c4_raw,
        static_cast<uint32_t>(kSimulationClass22StoredSlotCount));
    entity.production_door_step_q12=p.class_step_q12;
    entity.aircraft_definition={p.class_step_q12,p.pitch_max_velocity_turn16,p.class_specific_c0_raw,
        int32_t(p.class_specific_c4_raw),p.class_specific_c8_raw,p.class_specific_cc_raw,int32_t(p.reload_ticks)};
    entity.airport_platform_step_q12=p.class_specific_c8_raw;
    entity.airport_platform_limit_q12=p.class_specific_cc_raw;
    if(const auto sprite=catalog.sprites.find(root->sprite_path);sprite!=catalog.sprites.end())
        entity.airport_platform_baseline_q12=sprite->second.pad_baseline_q12;
    entity.research_technology_ids.assign(model->technology_panel_ids.begin(),model->technology_panel_ids.end());
    const auto derived_metrics=simulation_lab_derived_selection_metrics(
        hierarchy,entity.runtime_model_node_height_offsets_q12);
    entity.fog_uncover_radius_q12=derived_metrics.fog_uncover_radius_q12;
    entity.occupancy_mark_radius_units=derived_metrics.occupancy_mark_radius_units;
    entity.fog_reference_height_offset_q12=derived_metrics.fog_reference_height_offset_q12;
    const auto profiles=simulation_weapon_profiles_from_hierarchy(hierarchy,
        [&catalog](uint32_t id)->const ShotDefinitionRecord* {
            const auto it=catalog.shots.find(id);return it==catalog.shots.end()?nullptr:&it->second;
        },[&catalog](std::string_view path) {
            const auto it=catalog.sprites.find(std::string(path));
            return it==catalog.sprites.end()?std::pair<uint32_t,uint32_t>{}:it->second.launch_geometry;
        });
    const auto old_profiles=std::move(entity.combat_weapons);
    const auto old_states=std::move(entity.actor.weapons);
    entity.combat_weapons.clear();entity.actor.weapons.clear();
    for(const auto& profile:profiles) {
        entity.combat_weapons.push_back({profile,catalog.shots.at(profile.shot_definition_id)});
        SimulationWeaponState state{};
        const auto old=std::find_if(old_profiles.begin(),old_profiles.end(),[&](const auto& w){
            return w.profile.yaw_model_node_index==profile.yaw_model_node_index&&
                   w.profile.pitch_model_node_index==profile.pitch_model_node_index;
        });
        const auto index=static_cast<size_t>(old-old_profiles.begin());
        // Creation builds fresh SPR nodes from their serialized local angles.
        // The seed's default weapon state (pitch 0x4000) is not a live pose.
        if(!creation&&old!=old_profiles.end()&&index<old_states.size())state=old_states[index];
        else {
            state.launch_mesh_group_mask=0xFFFFFFFFU;
            if(profile.yaw_model_node_index<hierarchy.size())
                state.yaw.angle_turn16=hierarchy[profile.yaw_model_node_index].local_rotation_angle2&0xFFFFU;
            if(profile.pitch_model_node_index<hierarchy.size())
                state.pitch.angle_turn16=hierarchy[profile.pitch_model_node_index].local_rotation_angle1&0xFFFFU;
        }
        entity.actor.weapons.push_back(std::move(state));
    }
    if(creation) {
        std::vector<int32_t> bounds(hierarchy.size(),-1);
        const auto radius=[&](auto&& self,size_t index)->int32_t {
            if(bounds[index]>=0)return bounds[index];
            const auto sprite=catalog.sprites.find(hierarchy[index].sprite_path);
            if(sprite==catalog.sprites.end())return bounds[index]=0;
            int64_t value=std::max(0,sprite->second.bounding_radius_q12);
            for(size_t child=0;child<hierarchy.size();++child) {
                const auto slot=hierarchy[child].child_slot;
                if(hierarchy[child].parent_index!=index||slot>=6U)continue;
                const auto& offset=sprite->second.child_translations_q12[slot];if(!offset)continue;
                value=std::max(value,static_cast<int64_t>(fixed_length3_floor_q12(*offset))+self(self,child));
            }
            return bounds[index]=static_cast<int32_t>(std::clamp<int64_t>(value,0,std::numeric_limits<int32_t>::max()));
        };
        const auto root_index=static_cast<size_t>(root-hierarchy.begin());
        entity.motion.depth_bin_broad_phase_radius_q12=std::max(entity.motion.collision_radius_q12,radius(radius,root_index));
        const auto sprite=catalog.sprites.find(root->sprite_path);
        if(sprite!=catalog.sprites.end()&&model->runtime_class_id==13U) {
            entity.production_spawn_local=sprite->second.factory_spawn;
            entity.production_exit_local=sprite->second.factory_exit;
            entity.production_root_sprite_height_offset=sprite->second.height_offset;
        }
        // Produced factories compute world points from their new pose on the next production service.
        if(model->runtime_class_id==13U) {
            entity.production_spawn_world.reset();entity.production_exit_world.reset();
        }
    }
    if(entity.root_model_component_index<entity.model_components.size()) {
        const auto& component=entity.model_components[entity.root_model_component_index];
        entity.root_model_maximum_integrity=component.maximum_integrity;
        entity.root_model_integrity=component.integrity;
    }
    for (auto& component : entity.model_components) {
        if (component.runtime_removed) continue;
        component.terminal_attachment_metadata_prepared = true;
        component.terminal_attachment_point_mask = 0U;
        // Native channels scan the model root and its authored slot-0 node.
        for (size_t i = 0; i < hierarchy.size(); ++i) {
            const auto& node = hierarchy[i];
            if (i != component.source_model_node_index &&
                !(node.parent_index == component.source_model_node_index && node.child_slot == 0U &&
                  node.model_definition_id == component.model_definition_id && node.creates_runtime_node())) continue;
            const auto sprite = catalog.sprites.find(node.sprite_path);
            if (sprite != catalog.sprites.end())
                component.terminal_attachment_point_mask |= sprite->second.terminal_attachment_point_mask;
        }
        for (size_t channel = 0; channel < 8U; ++channel) {
            const auto effect = catalog.terminal_effect_completion_ticks.find(component.terminal_attachment_effect_ids[channel]);
            component.terminal_attachment_completion_ticks[channel] =
                effect == catalog.terminal_effect_completion_ticks.end() ? 0U : effect->second;
        }
    }
    entity.navigation_direct_route_valid=false;
    entity.navigation_target_cell_valid=false;
}
}
std::vector<uint32_t> simulation_lab_prepare_sound_asset_indices(
    const AssetCatalog& catalog,const LevelAsset& level) {
    auto pattern=level.paths().sounds;
    if(pattern.empty())return {};
    std::replace(pattern.begin(),pattern.end(),'\\','/');
    std::transform(pattern.begin(),pattern.end(),pattern.begin(),[](unsigned char c){
        return static_cast<char>(std::tolower(c));
    });
    const auto slash=pattern.find_last_of('/'),dot=pattern.find_last_of('.');
    if(dot==std::string::npos||(slash!=std::string::npos&&dot<slash))pattern+=".sam";
    else pattern.replace(dot,std::string::npos,".sam");
    std::unordered_map<uint32_t,std::string> paths;
    for(const auto& asset:catalog.assets()) {
        if(asset.kind!=AssetKind::Sound||asset.normalized_path.size()!=pattern.size())continue;
        bool matches=true;
        for(size_t i=0;i<pattern.size();++i) {
            auto c=static_cast<unsigned char>(asset.normalized_path[i]);
            if(c=='\\')c='/';
            if(pattern[i]!='?'&&pattern[i]!=static_cast<char>(std::tolower(c))){matches=false;break;}
        }
        if(!matches)continue;
        const auto id=sam_trailing_decimal_index(asset.normalized_path);
        if(!id||!*id)continue;
        const auto [it,inserted]=paths.try_emplace(*id,asset.normalized_path);
        if(!inserted&&it->second!=asset.normalized_path)it->second.clear();
    }
    std::vector<uint32_t> indices;
    for(const auto& [id,path]:paths)if(!path.empty())indices.push_back(id);
    std::sort(indices.begin(),indices.end());
    return indices;
}

std::shared_ptr<const SimulationLabRuntimeCatalog> simulation_lab_prepare_runtime_catalog(const AssetCatalog& catalog) {
    auto result=std::make_shared<SimulationLabRuntimeCatalog>();
    std::unordered_set<uint32_t> terminal_effect_ids;
    std::vector<const AssetRecord*> model_assets;
    std::unordered_set<std::string> loaded_paths;
    for(const auto& path:catalog.model_load_order()){
        const auto* asset=catalog.find(path);
        if(!asset)throw std::runtime_error("Missing level model resource: "+path);
        model_assets.push_back(asset);loaded_paths.insert(asset->normalized_path);
    }
    for(const auto& asset:catalog.assets())if(asset.kind==AssetKind::Model&&loaded_paths.insert(asset.normalized_path).second)
        model_assets.push_back(&asset);
    for(const auto* asset_pointer:model_assets) {
        const auto& asset=*asset_pointer;
        try {
            for(auto model:parse_model_visual_records(catalog.load(asset),asset.normalized_path)) {
                if(model.runtime_class_id==18U)
                    if(const auto* effect=catalog.effect_definition(model.simulation.class_specific_c4_raw))
                        if(const auto ticks=effect_model_completion_tick(*effect))
                            result->conversion_effect_lifetimes.try_emplace(effect->definition_id,*ticks);
                if(model.simulation.shot_definition_id!=0U||model.simulation.has_weapon_definition())
                    if(const auto* shot=catalog.shot_definition(model.simulation.shot_definition_id))
                        result->shots.try_emplace(shot->definition_id,*shot);
                for (const auto id : model.simulation.terminal_attachment_effect_ids)
                    if (const auto* effect = catalog.effect_definition(id)){
                        terminal_effect_ids.insert(id);
                        if (const auto ticks = effect_model_completion_tick(*effect))
                            result->terminal_effect_completion_ticks.try_emplace(id, *ticks);
                        if(effect->transition_kind==4U)
                            result->effects.try_emplace(id,SimulationLabRuntimeEffect{*effect,{}});
                    }
                for(const auto& node:model.hierarchy_nodes) {
                    if(node.sprite_path.empty()||result->sprites.contains(node.sprite_path))continue;
                    try {
                    const auto sprite=SpriteAsset::parse(catalog.load(node.sprite_path));
                    SimulationLabRuntimeSprite geometry;
                    geometry.height_offset=sprite.placement_height_offset();
                    if(const auto* p=sprite.find_lookup_record(ModelLookupKeyIndex{0},ModelLookupKeyClass{1}))
                        geometry.pad_baseline_q12=float_to_q12(p->local_translation.z);
                    // Preserve the native raycast LOD triangles for the narrow-phase
                    // shot contact solver. Immutable geometry is shared across every
                    // actor instance and never requires worker-thread asset reads.
                    if(const auto& mesh=sprite.raycast_mesh();mesh.indices.size()>=3U){
                        auto surface=std::make_shared<SimulationLabTriangleSurface>();
                        surface->native_triangles=sprite.raycast_group().raycast_triangles;
                        surface->positions.reserve(mesh.vertices.size());
                        for(const auto& vertex:mesh.vertices)surface->positions.push_back(vertex.position);
                        surface->indices.reserve(mesh.indices.size());
                        for(std::size_t tri=0;tri+2U<mesh.indices.size();tri+=3U){
                            const auto a=mesh.indices[tri],b=mesh.indices[tri+1U],c=mesh.indices[tri+2U];
                            if(a>=surface->positions.size()||b>=surface->positions.size()||c>=surface->positions.size())continue;
                            const auto pa=surface->positions[a],pb=surface->positions[b],pc=surface->positions[c];
                            const auto finite=[](Vec3f v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);};
                            if(!finite(pa)||!finite(pb)||!finite(pc))continue;
                            surface->indices.insert(surface->indices.end(),{a,b,c});
                        }
                        if(!surface->indices.empty())geometry.collision_surface=std::move(surface);
                    }
                    geometry.bounds_min=sprite.local_bounds_min();geometry.bounds_max=sprite.local_bounds_max();
                    geometry.has_bounds=sprite.has_valid_header_bounds();
                    // Broad-phase bounds must contain every visible triangle, even
                    // when a stock header's claimed extents are undersized. Otherwise
                    // the narrow phase would never see some legitimate model hits.
                    if(geometry.collision_surface){
                        Vec3f lo{INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY};
                        for(const auto& vertex:sprite.raycast_mesh().vertices){const auto p=vertex.position;
                            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))continue;
                            lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};
                            hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
                        if(lo.x<=hi.x&&lo.y<=hi.y&&lo.z<=hi.z){
                            if(geometry.has_bounds){
                                geometry.bounds_min={std::min(geometry.bounds_min.x,lo.x),std::min(geometry.bounds_min.y,lo.y),std::min(geometry.bounds_min.z,lo.z)};
                                geometry.bounds_max={std::max(geometry.bounds_max.x,hi.x),std::max(geometry.bounds_max.y,hi.y),std::max(geometry.bounds_max.z,hi.z)};
                            }else{geometry.bounds_min=lo;geometry.bounds_max=hi;geometry.has_bounds=true;}
                        }
                    }
                    geometry.effect_points.assign(sprite.type6_attachment_points().begin(),sprite.type6_attachment_points().end());
                    for(const auto& point:sprite.lookup_records())
                        if(point.key_class().value()==6U)
                            geometry.effect_point_records.emplace_back(point.key_index().value(),point.local_translation);
                        else if(point.key_class().value()==3U)
                            geometry.damage_effect_points.emplace_back(point.key_index().value(),point.local_translation);
                    geometry.bounding_radius_q12=sprite.bounding_radius_q12();
                    geometry.has_native_bounding_radius=true;
                    for(uint32_t slot=0;slot<6U;++slot)
                        if(const auto* child=sprite.child_attachment(slot))geometry.child_translations_q12[slot]=child->local_translation_q12;
                    if(const auto* point=sprite.find_lookup_record(ModelLookupKeyIndex{0},ModelLookupKeyClass{5}))
                        geometry.factory_spawn=point->local_translation;
                    if(const auto* point=sprite.find_lookup_record(ModelLookupKeyIndex{1},ModelLookupKeyClass{5}))
                        geometry.factory_exit=point->local_translation;
                    for(const auto& point:sprite.lookup_records())if(point.key_class().value()==2U) {
                        geometry.shot_points.emplace_back(point.key_index().value(),point.local_translation);
                        ++geometry.launch_geometry.first;
                        if(point.key_index().value()<32U)geometry.launch_geometry.second|=1U<<point.key_index().value();
                    }
                    for (const auto& point : sprite.lookup_records())
                        if (point.key_class().value() == 3U && point.key_index().value() < 8U)
                            geometry.terminal_attachment_point_mask |= 1U << point.key_index().value();
                    result->sprites.emplace(node.sprite_path,geometry);
                    } catch(const std::exception&) {}
                }
                result->definitions.try_emplace(model.definition_id,std::move(model));
            }
        } catch(const std::exception&) {
            // Unsupported optional imports retain the baseline worker path; no invented definition.
        }
    }
    for(const auto& [id,model]:result->definitions) {
        (void)id;
        for(const auto effect_id:{model.simulation.timed_effect_id,model.simulation.timed_water_effect_id,model.simulation.damage_effect_id,model.simulation.construction_destruction_effect_id}) {
            if(effect_id==0U||result->effects.contains(effect_id))continue;
            const auto* effect=catalog.effect_definition(effect_id);if(!effect)continue;
            result->effects.emplace(effect_id,SimulationLabRuntimeEffect{*effect,build_linked_shot_emissions(*effect)});
            if(const auto* shot=catalog.shot_definition(effect->linked_shot_id))result->shots.try_emplace(shot->definition_id,*shot);
            if(!result->sprites.contains(effect->sprite_path))try {
                const auto sprite=SpriteAsset::parse(catalog.load(effect->sprite_path));SimulationLabRuntimeSprite geometry;
                for(const auto& point:sprite.lookup_records())if(point.key_class().value()==2U)
                    geometry.shot_points.emplace_back(point.key_index().value(),point.local_translation);
                result->sprites.emplace(effect->sprite_path,std::move(geometry));
            }catch(const std::exception&) {}
        }
    }
    // Terminal channels without a completion action can still spawn linked
    // effects and shots. Prepare those chains independently of model disposal.
    for(const auto id:terminal_effect_ids){
        auto effect_id=id;
        std::unordered_set<uint32_t> visited;
        while(effect_id&&visited.insert(effect_id).second){
            const auto* effect=catalog.effect_definition(effect_id);
            if(!effect)break;
            result->effects.try_emplace(effect_id,SimulationLabRuntimeEffect{*effect,build_linked_shot_emissions(*effect)});
            if(effect->linked_shot_present)
                if(const auto* shot=catalog.shot_definition(effect->linked_shot_id))
                    result->shots.try_emplace(shot->definition_id,*shot);
            if((effect->linked_effect_present||effect->linked_shot_present)&&!result->sprites.contains(effect->sprite_path)){
                try {
                    const auto sprite=SpriteAsset::parse(catalog.load(effect->sprite_path));
                    SimulationLabRuntimeSprite geometry;
                    for(const auto& point:sprite.lookup_records())
                        if(point.key_class().value()==3U)
                            geometry.damage_effect_points.emplace_back(point.key_index().value(),point.local_translation);
                        else if(point.key_class().value()==2U)
                            geometry.shot_points.emplace_back(point.key_index().value(),point.local_translation);
                    result->sprites.emplace(effect->sprite_path,std::move(geometry));
                }catch(const std::exception&) {}
            }
            effect_id=effect->linked_effect_present?effect->linked_effect_id:0U;
        }
    }
    std::vector<uint32_t> pending_shots;
    for(const auto& [id,shot]:result->shots)pending_shots.push_back(id);
    std::unordered_set<uint32_t> retained_effects;
    const auto retain=[&](auto&& self,uint32_t eid)->void {
        if(!eid||!retained_effects.insert(eid).second)return;
        const auto* effect=catalog.effect_definition(eid);
        if(!effect)return;
        result->effect_lifetimes.try_emplace(eid,effect_lifetime_ticks(*effect));
        result->effects.try_emplace(eid,SimulationLabRuntimeEffect{*effect,build_linked_shot_emissions(*effect)});
        if(effect->linked_shot_present)
            if(const auto* shot=catalog.shot_definition(effect->linked_shot_id))
                if(result->shots.try_emplace(shot->definition_id,*shot).second)
                    pending_shots.push_back(shot->definition_id);
        if((effect->linked_effect_present||effect->linked_shot_present||effect->periodic_effect_interval_ticks)&&
           !result->sprites.contains(effect->sprite_path))try {
            const auto sprite=SpriteAsset::parse(catalog.load(effect->sprite_path));
            SimulationLabRuntimeSprite geometry;
            for(const auto& point:sprite.lookup_records()){
                if(point.key_class().value()==3U)
                    geometry.damage_effect_points.emplace_back(point.key_index().value(),point.local_translation);
                else if(point.key_class().value()==2U)
                    geometry.shot_points.emplace_back(point.key_index().value(),point.local_translation);
            }
            result->sprites.emplace(effect->sprite_path,std::move(geometry));
        }catch(const std::exception&) {}
        if(effect->linked_effect_present)self(self,effect->linked_effect_id);
        if(effect->periodic_effect_interval_ticks)self(self,effect->periodic_effect_id);
    };
    for(size_t i=0;i<pending_shots.size();++i){
        const auto id=pending_shots[i];
        // Effect chains can introduce additional shot definitions.
        const auto shot=result->shots.at(id);
        const auto retain_shot_effect=[&](uint32_t eid){retain(retain,eid);};
        retain_shot_effect(shot.launch_effect_id);
        retain_shot_effect(shot.secondary_effect_id);
        if((shot.launch_effect_id||shot.secondary_effect_id)&&!shot.sprite_path.empty()){
            try {
                const auto sprite=SpriteAsset::parse(catalog.load(shot.sprite_path));
                for(const auto& point:sprite.lookup_records()){
                    if(point.key_class().value()!=3U)continue;
                    if(point.key_index().value()==0U&&shot.launch_effect_id)
                        result->shot_launch_effect_points.try_emplace(id,point.local_translation);
                    if(point.key_index().value()==1U&&shot.secondary_effect_id)
                        result->shot_trail_effect_points.try_emplace(id,point.local_translation);
                }
            }catch(const std::exception&) {}
        }
        retain_shot_effect(shot.primary_effect_id);
        for(auto eid:shot.target_impact_effect_ids)retain_shot_effect(eid);
        for(auto eid:shot.terrain_impact_effect_ids)retain_shot_effect(eid);
    }
    for(const auto& preset:catalog.army_presets()) {
        ArmyAssetPreset compact;
        compact.registry_id=preset.registry_id;
        compact.model_variant_groups=preset.model_variant_groups;
        result->armies.emplace(compact.registry_id,std::move(compact));
    }
    return result;
}

bool simulation_lab_initialize_runtime_entity(SimulationLabEntity& entity,
    const SimulationLabSeedEntity& seed,const FactionTechnologyMask256& mask,
    const SimulationLabRuntimeCatalog& catalog,bool creation) {
    const auto asset=catalog.armies.find(seed.army_asset_id);
    if(asset==catalog.armies.end())return false;
    auto selection=asset->second;
    if(creation)for(auto& group:selection.model_variant_groups)
        for(auto& variant:group.variants)variant.faction_technology_replacements.clear();
    auto hierarchy=resolve_faction_technology_hierarchy(selection,mask);
    if(hierarchy.empty())return false;
    for(const auto& n:hierarchy)if(!definition(catalog,n.model_definition_id))return false;
    const auto components=simulation_lab_model_components_from_hierarchy(hierarchy);
    if(components.empty())return false;
    entity.runtime_model_node_height_offsets_q12=creation?std::vector<int32_t>{}:seed.runtime_model_node_height_offsets_q12;
    const auto previous=std::move(entity.model_components);
    entity.model_components.clear();entity.root_model_component_index=0;
    for(const auto& source:components) {
        SimulationLabModelComponentState component;
        const auto old=std::find_if(previous.begin(),previous.end(),[&](const auto& c){
            return c.source_model_node_index==source.source_model_node_index&&c.model_definition_id==source.model_definition_id;
        });
        if(!creation&&old!=previous.end())component=*old;
        copy_component_definition(component,*definition(catalog,source.model_definition_id),creation||old==previous.end());
        component.source_model_node_index=source.source_model_node_index;
        component.model_rotation_angle0=source.model_rotation_angle0;
        component.parent_component_index=source.parent_component_index;
        entity.model_components.push_back(component);
        if(!component.parent_component_index)entity.root_model_component_index=entity.model_components.size()-1U;
    }
    entity.collision_model_component_index=seed.collision_model_component_index<entity.model_components.size()
        ?seed.collision_model_component_index:entity.root_model_component_index;
    entity.runtime_model_hierarchy=std::make_shared<const std::vector<ModelVisualNode>>(std::move(hierarchy));
    rebuild_runtime_fields(entity,catalog,creation);
    entity.building = simulation_lab_structure_depth_class(entity.runtime_model_class_id);
    return true;
}

void simulation_lab_apply_runtime_technology(SimulationLabEntity& entity,
    const FactionTechnologyMask256& mask,const SimulationLabRuntimeCatalog& catalog,
    SimulationLabRunMetrics& metrics) {
    if(entity.runtime_removed || !entity.runtime_model_hierarchy)return;
    const auto& original=*entity.runtime_model_hierarchy;
    std::shared_ptr<std::vector<ModelVisualNode>> changed;
    for(auto& component:entity.model_components) {
        if (component.runtime_removed) continue;
        const auto* current=definition(catalog,component.model_definition_id);
        if(!current)continue;
        const ModelVisualRecord* replacement=nullptr;
        for(const auto id:current->faction_technology_variant_model_ids) {
            const auto* candidate=id==0U?nullptr:definition(catalog,id);
            if(candidate&&faction_technology_unlocked(mask,candidate->technology_unlock_id)) {
                replacement=candidate;break;
            }
        }
        if(!replacement)continue;
        if(current->simulation.maximum_integrity<=0) {
            ++metrics.runtime_technology_invalid_health_scales;continue;
        }
        const int64_t scaled=static_cast<int64_t>(component.integrity)*replacement->simulation.maximum_integrity/
                             current->simulation.maximum_integrity;
        if(scaled<std::numeric_limits<int32_t>::min()||scaled>std::numeric_limits<int32_t>::max()) {
            ++metrics.runtime_technology_invalid_health_scales;continue;
        }
        if(!changed)changed=std::make_shared<std::vector<ModelVisualNode>>(*entity.runtime_model_hierarchy);
        const auto old_id=component.model_definition_id;
        // Only nodes owned by this ModelRuntime change definition; children retain their own runtimes.
        for(size_t i=component.source_model_node_index;i<changed->size();++i) {
            auto cursor=i;bool owned=cursor==component.source_model_node_index;
            for(size_t hops=0;!owned&&cursor<changed->size()&&hops<changed->size();++hops) {
                if(original[cursor].model_definition_id!=old_id)break;
                cursor=original[cursor].parent_index;owned=cursor==component.source_model_node_index;
            }
            if(!owned||original[i].model_definition_id!=old_id)continue;
            auto& node=(*changed)[i];node.model_definition_id=replacement->definition_id;
            node.runtime_class_id=replacement->runtime_class_id;node.simulation=replacement->simulation;
            node.shot_impact_class_index=replacement->shot_impact_class_index;
            node.research_technology_ids=replacement->technology_panel_ids;
        }
        copy_component_definition(component,*replacement,false);
        component.integrity=static_cast<int32_t>(scaled);
        ++metrics.runtime_technology_replacements;
    }
    if(changed) {
        entity.runtime_model_hierarchy=std::move(changed);
        rebuild_runtime_fields(entity,catalog,false);
    }
}
}
