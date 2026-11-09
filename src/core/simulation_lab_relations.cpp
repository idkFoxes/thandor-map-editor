#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/retail_relation_prediction.hpp"
#include "thandor/retail_random.hpp"
#include <bit>

namespace thandor {
namespace {
uint32_t state(const SimulationLabRun& r,uint32_t row,uint32_t column) {
    return (r.faction_packed_relation_states[row]>>(column*4U))&15U;
}
uint32_t draw(SimulationLabRun& r) {++r.relation_trace.draws;return retail_random_next(r.rng_state);}
void merge(SimulationLabRun& r,uint32_t survivor,uint32_t absorbed) {
    ++r.relation_trace.merges;
    if(r.active_view_faction_index==absorbed)r.active_view_faction_index=survivor;
    for(auto& redirect:r.faction_merge_redirect)if(redirect==absorbed)redirect=survivor;
    for(auto& e:r.entities)if(!e.runtime_removed && e.faction_index==absorbed &&
       (e.root_model_component_index>=e.model_components.size() || !e.model_components[e.root_model_component_index].runtime_removed))
        e.faction_index=survivor;
    r.faction_frontend_player_counts[survivor]+=r.faction_frontend_player_counts[absorbed];
    r.faction_frontend_player_counts[absorbed]=0;
    if(r.retail_scratch) {
        auto scratch=std::make_shared<SimulationLabRetailScratchImage>(*r.retail_scratch);
        for(auto& mask:scratch->field_occupancy_masks)mask|=((mask>>(8U*absorbed))&255U)<<(8U*survivor);
        r.retail_scratch=std::move(scratch);
    }
    for(auto& mask:r.current_visibility_occupancy)mask|=((mask>>(8U*absorbed))&255U)<<(8U*survivor);
    r.faction_lifecycle_states[absorbed]=0;
    auto& a=r.faction_economy[survivor];const auto& b=r.faction_economy[absorbed];
    a.xenite_current_q4+=b.xenite_current_q4;a.tritium_current_q4+=b.tritium_current_q4;
    a.xenite_storage_limit_q4=a.xenite_storage_limit_q4.value_or(0)+b.xenite_storage_limit_q4.value_or(0);
    a.tritium_storage_limit_q4=a.tritium_storage_limit_q4.value_or(0)+b.tritium_storage_limit_q4.value_or(0);
    a.baseline_xenite_storage_limit_q4+=b.baseline_xenite_storage_limit_q4;
    a.baseline_tritium_storage_limit_q4+=b.baseline_tritium_storage_limit_q4;
    a.baseline_energy_supply_q4+=b.baseline_energy_supply_q4;
    a.baseline_energy_generation_capacity_q4+=b.baseline_energy_generation_capacity_q4;
    r.faction_energy_generation_capacity_q4[survivor]+=r.faction_energy_generation_capacity_q4[absorbed];
    r.faction_active_research_energy_q4[survivor]+=r.faction_active_research_energy_q4[absorbed];
    r.faction_active_production_energy_q4[survivor]+=r.faction_active_production_energy_q4[absorbed];
    r.faction_active_research_energy_q4[absorbed]=r.faction_active_production_energy_q4[absorbed]=0;
    // Native merge ORs masks directly; it does not invoke technology-unlock/variant callbacks.
    for(size_t w=0;w<8;++w)r.faction_technology_masks[survivor][w]|=r.faction_technology_masks[absorbed][w];
    auto& score=r.faction_score[survivor];const auto& other=r.faction_score[absorbed];
    score.xenite_extracted_total_q4+=other.xenite_extracted_total_q4;
    score.tritium_extracted_total_q4+=other.tritium_extracted_total_q4;
    const auto add=[](int32_t& x,int32_t y){x=std::bit_cast<int32_t>(uint32_t(x)+uint32_t(y));};
    add(score.relation_counter_a,other.relation_counter_a);add(score.relation_counter_b,other.relation_counter_b);
    add(score.relation_counter_c,other.relation_counter_c);add(score.relation_counter_d,other.relation_counter_d);
    add(score.relation_counter_e,other.relation_counter_e);add(score.relation_counter_f,other.relation_counter_f);
    r.faction_relation_counter_c[survivor]+=r.faction_relation_counter_c[absorbed];
    r.faction_relation_counter_d[survivor]+=r.faction_relation_counter_d[absorbed];
    const auto append=[&](auto& queue) {
        size_t count=0;for(const auto& q:queue)count+=q.faction_index==survivor;
        const auto before=queue.size();
        for(size_t i=0;i<before && count<64U;++i)if(queue[i].faction_index==absorbed) {
            auto copy=queue[i];copy.faction_index=survivor;queue.push_back(copy);++count;
        }
    };
    append(r.pending_asset_requests);append(r.secondary_purchase_requests);
}
void advance(SimulationLabRun& r,uint32_t first,uint32_t second) {
    const auto old=state(r,second,first);
    if(old==2 && std::bit_cast<int32_t>(r.tick-r.faction_pair_change_ticks[second][first])<601)return;
    switch(old) {
    case 0:case 1:case 2:simulation_lab_apply_relation_transition(r,first,second,3,2);break;
    case 3:simulation_lab_apply_relation_transition(r,first,second,4,4);break;
    case 4:case 5:simulation_lab_apply_relation_transition(r,first,second,6,5);break;
    case 6:simulation_lab_apply_relation_transition(r,first,second,8,8);break;
    case 8:case 9:simulation_lab_apply_relation_transition(r,first,second,10,9);break;
    case 10:simulation_lab_apply_relation_transition(r,first,second,11,11);break;
    default:break;
    }
}
void reset(SimulationLabRun& r,uint32_t first,uint32_t second) {
    const auto old=state(r,second,first);
    switch(old) {
    case 1:case 2:case 3:case 4:case 6:simulation_lab_apply_relation_transition(r,first,second,0,0);break;
    case 5:case 8:case 10:simulation_lab_apply_relation_transition(r,first,second,4,4);break;
    case 9:simulation_lab_apply_relation_transition(r,first,second,8,8);break;
    default:break;
    }
}
}
void simulation_lab_apply_relation_transition(SimulationLabRun& r,uint32_t first,uint32_t second,uint32_t a,uint32_t b) {
    if(first>=8 || second>=8)return;
    ++r.relation_trace.transitions;
    auto& x=r.faction_packed_relation_states[first];auto& y=r.faction_packed_relation_states[second];
    x=(x&~(15U<<(4U*second)))|(a<<(4U*second));y=(y&~(15U<<(4U*first)))|(b<<(4U*first));
    if(b<4){r.faction_capability_flags[first]&=~(1U<<second);r.faction_capability_flags[second]&=~(1U<<first);}
    else {r.faction_capability_flags[first]|=1U<<second;r.faction_capability_flags[second]|=1U<<first;}
    r.faction_pair_change_ticks[first][second]=r.faction_pair_change_ticks[second][first]=r.tick;
    if(b==11) {
        const auto count_first=r.faction_frontend_player_counts[first],count_second=r.faction_frontend_player_counts[second];
        const bool swap=(count_first&count_second)==0 && (count_first!=0 || count_second!=0)
            ?count_second!=0:(draw(r)&0x2000U)==0;
        merge(r,swap?second:first,swap?first:second);
    }
}
void simulation_lab_update_faction_relations(SimulationLabRun& r,uint32_t faction) {
    r.relation_trace={r.tick,faction,0,0,0};
    for(uint32_t other=7;other>0;--other) {
        if(r.faction_lifecycle_states[other]!=1 || other==faction)continue;
        const bool blocked=retail_relation_pair_blocked(other,faction,r.relation_ui_flags,r.faction_lifecycle_states,
            r.faction_packed_relation_states,r.scheduled_conditions,r.end_condition_triggers);
        const auto old=state(r,faction,other);const bool top=old==3 || old==6 || old==10;
        if(blocked) {if(top && (draw(r)&0x180U)==0x80U)reset(r,other,faction);continue;}
        const auto random=draw(r);const auto pressure=r.faction_pair_pressure[faction][other];
        if(pressure!=0 && (pressure>31 || (random>>28U)<=pressure))continue;
        const uint32_t mask=top?(pressure?0x1FFU:0x7FU):(pressure?0x3FFU:0x17FU);
        if((random&mask)==85U)advance(r,other,faction);
    }
}
}
