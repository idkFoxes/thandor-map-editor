#include "thandor/simulation_lab_catalog.hpp"
#include <algorithm>
#include <unordered_set>

namespace thandor {
std::vector<V528PathRuntimeBlocker> simulation_lab_prepare_static_navigation_blockers(
    const AssetCatalog& catalog,const LevelAsset& level) {
    std::vector<V528PathRuntimeBlocker> result;
    for(size_t placement_index=0;placement_index<level.placements().size();++placement_index) {
        const auto& placement=level.placements()[placement_index];
        const auto* preset=catalog.army_preset(placement.army_asset_id);
        if(!preset||simulation_lab_scenery_has_runtime_owner(*preset,placement.faction_index)) continue;
        if(simulation_lab_faction_placement_has_runtime_owner(*preset,placement.faction_index))continue;
        const auto radius=preset->simulation_motion_profile.collision_radius_q12;
        if(radius>0) result.push_back({float_to_q12(placement.render_x()),float_to_q12(placement.render_y()),radius,preset->runtime_model_class_id,placement_index});
    }
    return result;
}
std::vector<SimulationLabTechnologyAsset> simulation_lab_prepare_technologies(
    const AssetCatalog& catalog, const LevelAsset* level) {
    std::vector<SimulationLabTechnologyAsset> assets;
    for(const auto& t:catalog.technologies()) {
        if(t.technology_id>=256U) continue;
        assets.push_back({t.technology_id,t.prerequisite_masks,t.xenite_cost_q4,
            t.energy_demand_q4,t.research_duration_q5,t.dependency_technology_id,
            t.ai_base_candidate_score,static_cast<uint32_t>(t.category)});
    }
    if (level) {
        // NewLevel_LoadAssetList visits the LEV MDL table in file order;
        // ModelAsset_PrepareRecords claims each variable-size record in file order.
        // Parse the complete table before returning any authoritative lookup result.
        // Missing/malformed resources throw instead of silently using another map.
        std::unordered_set<std::uint32_t> registered_ids;
        for (const auto& base_path : level->model_resource_paths_in_load_order()) {
            const auto path = base_path.ends_with(".mdl") ? base_path : base_path + ".mdl";
            for (const auto& model : parse_model_visual_records(catalog.load(path), path)) {
                if (!registered_ids.insert(model.definition_id).second || registered_ids.size()>768U)
                    throw FormatError("LEV model registry has duplicate IDs or exceeds 768 slots");
                for (auto& technology : assets) {
                    if (technology.compatible_model_resolved ||
                        model.technology_unlock_id != technology.technology_id) continue;
                    technology.compatible_model_resolved = true;
                    technology.compatible_model_definition_id = model.definition_id;
                    technology.compatible_model_runtime_class_id = model.runtime_class_id;
                    technology.compatible_model_registry_position = static_cast<uint32_t>(registered_ids.size());
                }
            }
        }
        for (auto& technology : assets) technology.compatible_model_lookup_complete = true;
        return assets;
    }
    // Legacy API fixtures without a LEV retain the documented lowest-MDL-ID
    // approximation. Actual editor/replay sessions pass their current LEV.
    for(const auto& preset:catalog.army_presets())
        for(const auto& group:preset.model_variant_groups)
            for(const auto& variant:group.variants)
                for(auto& technology:assets) {
                    if(variant.technology_unlock_id!=technology.technology_id) continue;
                    if(technology.compatible_model_resolved &&
                       technology.compatible_model_definition_id<=variant.model_definition_id) continue;
                    const auto root=std::find_if(variant.model_hierarchy.begin(),variant.model_hierarchy.end(),
                        [](const auto& n){return n.parent_index==std::numeric_limits<uint32_t>::max();});
                    if(root==variant.model_hierarchy.end()) continue;
                    technology.compatible_model_resolved=true;
                    technology.compatible_model_definition_id=variant.model_definition_id;
                    technology.compatible_model_runtime_class_id=root->runtime_class_id;
                }
    return assets;
}
namespace {
const SimulationLabLinkedModelTechnology* selected(
    const SimulationLabLinkedModelGroup& group, const FactionTechnologyMask256& mask) {
    const SimulationLabLinkedModelTechnology* result=nullptr;
    for(const auto& variant:group.variants) {
        if(variant.variant_index==0U) result=&variant;
        if(faction_technology_unlocked(mask,variant.required_technology_id)) result=&variant;
    }
    return result;
}
}
void simulation_lab_prepare_model_technologies(const ArmyAssetPreset& preset,
    const AssetCatalog& catalog, SimulationLabSeedEntity& seed) {
    // Preserve existing world points for authored map placements. New factories
    // need body-local lookup 5/0 and 5/1 so their live pose can locate the exit.
    if(preset.runtime_model_class_id==13U) {
        const auto& hierarchy=preset.resolved_model_hierarchy.empty()
            ?preset.model_hierarchy:preset.resolved_model_hierarchy;
        const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& n){
            return n.parent_index==std::numeric_limits<uint32_t>::max();});
        if(root!=hierarchy.end()&&!root->sprite_path.empty()) {
            seed.production_door_step_q12=root->simulation.class_step_q12;
            const auto sprite=SpriteAsset::parse(catalog.load(root->sprite_path));
            const auto* spawn=sprite.find_lookup_record(ModelLookupKeyIndex{0U},ModelLookupKeyClass{5U});
            const auto* exit=sprite.find_lookup_record(ModelLookupKeyIndex{1U},ModelLookupKeyClass{5U});
            if(spawn&&exit) {
                seed.production_spawn_local=spawn->local_translation;
                seed.production_exit_local=exit->local_translation;
                seed.production_root_sprite_height_offset=sprite.placement_height_offset();
            }
        }
    }
    seed.linked_model_technologies.clear();
    seed.research_model_variants.clear();
    for(const auto& group:preset.model_variant_groups) {
        SimulationLabLinkedModelGroup out;
        out.parent_index=group.parent_group_index;
        out.child_slot=group.child_slot;
        for(const auto& variant:group.variants) {
            if(group.parent_group_index==std::numeric_limits<uint32_t>::max()) {
                const auto panel_from=[](const auto& hierarchy) {
                    SimulationLabResearchPanel panel;
                    const auto root=std::find_if(hierarchy.begin(),hierarchy.end(),[](const auto& n){
                        return n.parent_index==std::numeric_limits<uint32_t>::max();});
                    if(root!=hierarchy.end()) {
                        panel.model_definition_id=root->model_definition_id;
                        panel.runtime_class_id=root->runtime_class_id;
                        panel.ids=root->research_technology_ids;
                    }
                    return panel;
                };
                SimulationLabResearchVariant research;
                research.variant_index=variant.variant_index;
                research.required_technology_id=variant.technology_unlock_id;
                research.panel=panel_from(variant.model_hierarchy);
                for(const auto& replacement:variant.faction_technology_replacements)
                    research.replacements.push_back({replacement.technology_unlock_id,
                        panel_from(replacement.model_hierarchy)});
                if(research.panel.model_definition_id!=0U)
                    seed.research_model_variants.push_back(std::move(research));
            }
            SimulationLabLinkedModelTechnology v;
            v.variant_index=variant.variant_index;
            v.required_technology_id=variant.technology_unlock_id;
            v.granted_technology_id=variant.linked_technology_id;
            const auto root=std::find_if(variant.model_hierarchy.begin(),variant.model_hierarchy.end(),
                [](const auto& n){return n.parent_index==std::numeric_limits<uint32_t>::max();});
            if(root!=variant.model_hierarchy.end()) {
                v.score.resolved=true;
                v.score.root_definition_value0c=root->simulation.class_step_q12;
                v.score.root_definition_value18=root->simulation.runtime_field_18_raw;
                v.score.root_definition_value60=root->simulation.maximum_integrity;
                v.score.root_category_maximum_index5c=root->shot_impact_class_index;
                if(root->simulation.reload_ticks!=0U)
                    if(const auto* shot=catalog.shot_definition(root->simulation.shot_definition_id)) {
                        v.weapon.valid=true;
                        v.weapon.reload_divisor30=static_cast<int32_t>(root->simulation.reload_ticks);
                        v.weapon.target_class_impact_damage_q12=shot->target_impact_damage_q12;
                    }
            }
            out.variants.push_back(v);
        }
        seed.linked_model_technologies.push_back(std::move(out));
    }
}
void simulation_lab_prepare_ai_product(const ArmyAssetPreset& preset,
    const AssetCatalog& catalog, SimulationLabProductionAsset& product) {
    product.ai_linked_model_definition_ids = {};
    product.ai_faction_score_seeds = {};
    product.ai_faction_score_seeds_prepared = false;
    product.ai_baseline_root_definition_value0c = 0;
    product.ai_baseline_root_definition_value18 = 0;
    product.ai_baseline_root_definition_value60 = 0;
    product.ai_baseline_root_category_maximum_index5c = 0;
    product.ai_root_first_variant_required_technology_bit = std::numeric_limits<uint32_t>::max();
    simulation_lab_prepare_model_technologies(preset,catalog,product.entity_seed);
    const auto& groups=product.entity_seed.linked_model_technologies;
    const auto root=std::find_if(groups.begin(),groups.end(),[](const auto& g){
        return g.parent_index==std::numeric_limits<uint32_t>::max();});
    if(root==groups.end()) return;
    // Research kind 5 compares raw ARM linked definitions, before faction MDL
    // replacement. Keep this in the shared builder so GUI and replay agree.
    const auto root_index = static_cast<uint32_t>(root-groups.begin());
    const auto copy_raw_group = [&](uint32_t index, std::size_t list) {
        for (const auto& variant : preset.model_variant_groups[index].variants)
            if (variant.variant_index < 8U && variant.model_definition_id != 0U)
                product.ai_linked_model_definition_ids[list].push_back(variant.model_definition_id);
    };
    copy_raw_group(root_index,0U);
    std::vector<uint32_t> children;
    for (uint32_t i=0;i<groups.size();++i)
        if (groups[i].parent_index==root_index) children.push_back(i);
    std::sort(children.begin(),children.end(),[&](uint32_t a,uint32_t b) {
        return groups[a].child_slot<groups[b].child_slot;
    });
    for (std::size_t i=0;i<std::min<std::size_t>(2U,children.size());++i)
        copy_raw_group(children[i],i+1U);
    product.ai_faction_score_seeds_prepared=true;
    const auto baseline=std::find_if(root->variants.begin(),root->variants.end(),
        [](const auto& v){return v.variant_index==0U;});
    if(baseline!=root->variants.end()) {
        product.ai_baseline_root_definition_value0c=baseline->score.root_definition_value0c;
        product.ai_baseline_root_definition_value18=baseline->score.root_definition_value18;
        product.ai_baseline_root_definition_value60=baseline->score.root_definition_value60;
        product.ai_baseline_root_category_maximum_index5c=baseline->score.root_category_maximum_index5c;
        product.ai_root_first_variant_required_technology_bit=baseline->required_technology_id;
    }
    for(uint32_t f=0;f<8U;++f)
        product.ai_faction_score_seeds[f]=simulation_lab_current_ai_score_seed(product,{},f);
}
SimulationLabAiArmyScoreSeed simulation_lab_current_ai_score_seed(
    const SimulationLabProductionAsset& product,const FactionTechnologyMask256& mask,uint32_t faction) {
    const auto& groups=product.entity_seed.linked_model_technologies;
    if(groups.empty()) return faction<product.ai_faction_score_seeds.size()
        ?product.ai_faction_score_seeds[faction]:SimulationLabAiArmyScoreSeed{};
    uint32_t root_index=std::numeric_limits<uint32_t>::max();
    bool unlocked=true;
    for(uint32_t i=0;i<groups.size();++i) {
        if(groups[i].parent_index==std::numeric_limits<uint32_t>::max()) root_index=i;
        const auto first=std::find_if(groups[i].variants.begin(),groups[i].variants.end(),
            [](const auto& v){return v.variant_index==0U;});
        // Retail hierarchy availability checks linkedDefinitionIds[0], not the
        // last selectable variant. AI scoring separately selects current variants.
        if(first==groups[i].variants.end()||
           !faction_technology_unlocked(mask,first->required_technology_id)) unlocked=false;
    }
    if(root_index==std::numeric_limits<uint32_t>::max()) return {};
    const auto* root=selected(groups[root_index],mask);
    if(!root) return {};
    auto score=root->score;
    score.all_selected_model_technologies_unlocked=unlocked;
    std::vector<const SimulationLabLinkedModelGroup*> children;
    for(const auto& g:groups) if(g.parent_index==root_index) children.push_back(&g);
    std::sort(children.begin(),children.end(),[](auto a,auto b){return a->child_slot<b->child_slot;});
    score.child_list_count=static_cast<uint32_t>(children.size());
    for(size_t i=0;i<std::min(children.size(),score.child_weapons.size());++i)
        if(const auto* child=selected(*children[i],mask)) score.child_weapons[i]=child->weapon;
    return score;
}
}
