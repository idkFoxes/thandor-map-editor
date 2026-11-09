#pragma once
#include "thandor/effect.hpp"
#include "thandor/simulation.hpp"
#include <functional>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace thandor {
// EFF 0 is the absent-effect sentinel. SHT 0 is a valid laser definition;
// linked_shot_present, not the numeric SHT identity, controls an EFF's link.
class RenderTextureDependencies {
public:
    using EffectLookup=std::function<const EffectDefinitionRecord*(uint32_t)>;
    using ShotLookup=std::function<const ShotDefinitionRecord*(uint32_t)>;
    using EffectVisit=std::function<void(const EffectDefinitionRecord&)>;
    using ShotVisit=std::function<void(const ShotDefinitionRecord&)>;
    RenderTextureDependencies(EffectLookup effects,ShotLookup shots,
                              EffectVisit visit_effect,ShotVisit visit_shot)
        :effects_(std::move(effects)),shots_(std::move(shots)),
         visit_effect_(std::move(visit_effect)),visit_shot_(std::move(visit_shot)){}
    void effect(uint32_t id) {
        if(id==0U||!effect_ids_.insert(id).second)return;
        const auto* definition=effects_(id);
        if(!definition)throw std::runtime_error("timed effect is missing from the EFF registry");
        visit_effect_(*definition);
        if(definition->linked_effect_present)effect(definition->linked_effect_id);
        if(definition->periodic_effect_id)effect(definition->periodic_effect_id);
        if(definition->linked_shot_present)shot(definition->linked_shot_id);
    }
    void shot(uint32_t id) {
        if(!shot_ids_.insert(id).second)return;
        const auto* definition=shots_(id);
        if(!definition)throw std::runtime_error("weapon references a missing SHT definition");
        visit_shot_(*definition);
        effect(definition->primary_effect_id);
        effect(definition->launch_effect_id);
        effect(definition->secondary_effect_id);
        for(const auto impact:definition->terrain_impact_effect_ids)effect(impact);
        for(const auto impact:definition->target_impact_effect_ids)effect(impact);
    }
private:
    EffectLookup effects_;
    ShotLookup shots_;
    EffectVisit visit_effect_;
    ShotVisit visit_shot_;
    std::unordered_set<uint32_t> effect_ids_,shot_ids_;
};
}
