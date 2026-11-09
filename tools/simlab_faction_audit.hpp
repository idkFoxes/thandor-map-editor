#pragma once
// Headless observation only. This never writes to SimulationLabRun or consumes its RNG.
#include "thandor/simulation_lab.hpp"
#include "thandor/catalog.hpp"
#include <fstream>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <set>
#include <tuple>
#include <iomanip>
#include <thread>
#include <ctime>
#include <cmath>

namespace thandor::audit {
inline std::string csv(std::string value){std::string out="\"";for(char c:value){out+=c;if(c=='\"')out+=c;}return out+'\"';}
class FactionAudit {
    using Key=std::tuple<size_t,uint32_t,uint32_t>;
    struct Previous {
        uint64_t id{},volleys{};uint32_t faction{},arm{},research{},production{},prod_elapsed{},flags{};
        int32_t hp{};Vec3f position{};bool complete{},terminal{},removed{},active{},pending{},producing{},linked{};
        uint32_t stationary{};bool stalled{};uint64_t target{};std::optional<Vec3f> goal;
    };
    struct Totals {uint64_t spawned{},completed{},lost{},volleys{},hp_lost{},research_completed{},orders{},move_commands{},stalls{};double distance{};};
    std::filesystem::path dir_;
    std::ofstream events_,factions_,entities_,planning_,research_,purchases_,orders_,hashes_,metrics_;
    std::map<Key,std::vector<uint64_t>> identities_;
    std::map<uint64_t,Previous> previous_;
    std::array<Totals,8> totals_{};
    std::array<FactionTechnologyMask256,8> masks_{};
    std::array<uint32_t,8> plan_tick_{},research_tick_{},purchase_tick_{},completion_tick_{};
    std::array<uint8_t,8> lifecycle_{};
    uint64_t next_id_=1,last_order_=0,rolling_=14695981039346656037ULL;
    uint32_t last_tick_=0;bool initialized_=false;
    uint32_t faction_sample_interval_=20,entity_sample_interval_=20;
    std::chrono::steady_clock::time_point start_=std::chrono::steady_clock::now();
    std::clock_t cpu_start_=std::clock();double cpu_share_=0.5;
    static bool same(std::optional<Vec3f> a,std::optional<Vec3f> b){if(bool(a)!=bool(b))return false;if(!a)return true;return float_to_q12(a->x)==float_to_q12(b->x)&&float_to_q12(a->y)==float_to_q12(b->y);}
    void hash(uint64_t v){for(unsigned i=0;i<8;++i){rolling_^=(v>>(8*i))&255;rolling_*=1099511628211ULL;}}
    void event(uint32_t tick,uint32_t f,const char* kind,uint64_t id,uint32_t arm,int64_t value=0,uint64_t other=0){events_<<tick<<','<<f<<','<<kind<<','<<id<<','<<arm<<','<<value<<','<<other<<'\n';}
public:
    FactionAudit(const std::filesystem::path& dir,const AssetCatalog& catalog,const LevelAsset& level,double cpu_share):dir_(dir),cpu_share_(cpu_share){
        for(auto pair:{std::pair{"THANDOR_SIMLAB_AUDIT_FACTION_INTERVAL",&faction_sample_interval_},std::pair{"THANDOR_SIMLAB_AUDIT_ENTITY_INTERVAL",&entity_sample_interval_}}){if(const char* v=std::getenv(pair.first)){auto n=std::stoul(v);if(n<1||n>20)throw std::runtime_error("Audit interval must be 1..20 ticks");*pair.second=uint32_t(n);}}
        std::filesystem::create_directories(dir);
        events_.open(dir/"events.csv.partial");events_<<"tick,faction,event,entity_id,arm,value,other_entity_or_technology\n";
        factions_.open(dir/"factions.csv.partial");factions_<<"tick,seconds,faction,lifecycle,buildings,units,pioneers,under_construction,primary_queue,secondary_queue,xenite_q4,tritium_q4,xenite_capacity_q4,tritium_capacity_q4,energy_capacity_q4,energy_supplied_q4,energy_unpowered_q4,xenite_rate_q4,tritium_rate_q4,movement_world,volleys,root_hp_lost,spawned,build_completions,terminal_events,research_completions,orders,move_commands,stall_episodes,active_research,active_production,explored_percent,rng,technology0,technology1,technology2,technology3,technology4,technology5,technology6,technology7\n";
        entities_.open(dir/"entities.csv.partial");entities_<<"tick,entity_id,slot,faction,arm,class,building,complete,terminal,removed,x_q12,y_q12,z_q12,hp,max_hp,move_target_x_q12,move_target_y_q12,target_entity_id,volleys,production_arm,production_elapsed,production_duration,research_tec,research_active,research_pending,research_elapsed,research_duration,linked,command_kind,command_mode,nav_direct,follow_route_failures,follow_blocks,stationary_ticks\n";
        planning_.open(dir/"planning.csv.partial");planning_<<"tick,faction,own,pioneers,structures,presence,ws05,ws06,ws08,ws09,ws10,factory,existing_factories,lab,resource330,army_unlocked,factory_capacity,lab_capacity,profile0,score0,profile1,score1,profile2,score2,best_research,best_research_score,cache_count,purchase_balance_blocks,purchase_ceiling_blocks\n";
        research_.open(dir/"research_candidates.csv.partial");research_<<"tick,faction,technology,source_arm,source_class,score,reason,kind,base_score,cost_q4,model,model_class,positive_compatible_armies,best_potential_arm,best_potential_score\n";
        purchases_.open(dir/"purchases.csv.partial");purchases_<<"tick,faction,rank,id,kind,packed_score,cost_q4,budget_before_q4,budget_after_q4,decision,cached\n";
        orders_.open(dir/"military_orders.csv.partial");orders_<<"tick,sequence,faction,entity_id,workspace,target_entity_id,count,strength,score,site_score,anchor_score,hostile_score,requested_x_q12,requested_y_q12,stored_x_q12,stored_y_q12,mode,generation,route_started,direct,live_target,unseen_target\n";
        hashes_.open(dir/"state_hashes.csv.partial");hashes_<<"tick,rolling_logical_hash,rng,entities,projectiles,planning_cycles,shots,damage\n";
        metrics_.open(dir/"metrics.csv.partial");metrics_<<"tick,planning_cycles,builds,production_started,production_completed,research_started,research_completed,shots,projectile_impacts,projectile_damage,water_damage,water_loss,collisions,route_solves,step_blocks,factory_exit_retries,removed,conversion_complete\n";
        std::ofstream assets(dir/"assets.csv.partial");assets<<"arm,name,loadout,class,building\n";for(const auto& p:catalog.army_presets())assets<<p.registry_id<<','<<csv(p.display_name)<<','<<csv(p.loadout_name)<<','<<p.runtime_model_class_id<<','<<(p.browser_kind==PlacementBrowserKind::Building)<<'\n';
        std::ofstream tech(dir/"technologies.csv.partial");tech<<"technology,title,cost_q4,duration,category\n";for(const auto& t:catalog.technologies())tech<<t.technology_id<<','<<csv(t.title)<<','<<t.xenite_cost_q4<<','<<t.research_duration_q5<<','<<uint32_t(t.category)<<'\n';
        std::ofstream names(dir/"faction_names.csv.partial");names<<"faction,name,color,graphics_family\n";for(uint32_t f=1;f<=level.frontend_active_faction_count();++f){const auto g=resolve_faction_graphics_family(f,level.faction_graphics_family_offset_raw(f));names<<f<<','<<csv(graphics_family_single_player_faction_name(g))<<','<<csv(graphics_family_color_name(g))<<','<<g<<'\n';}
    }
    void observe(const SimulationLabRun& run){
        const uint32_t tick=run.tick;last_tick_=tick;
        std::map<Key,size_t> occurrence;std::vector<uint64_t> ids;ids.reserve(run.entities.size());std::set<uint64_t> present;
        for(const auto& e:run.entities){const Key key{e.source_placement_index,e.army_asset_id,e.construction_start_tick};auto& group=identities_[key];auto rank=occurrence[key]++;if(rank==group.size())group.push_back(next_id_++);ids.push_back(group[rank]);present.insert(group[rank]);}
        auto id_at=[&](std::optional<size_t> slot){return slot&&*slot<ids.size()?ids[*slot]:0ULL;};
        hash(tick);hash(run.rng_state);hash(run.ai_controlled_faction_mask);
        for(size_t i=0;i<run.entities.size();++i){const auto& e=run.entities[i];const auto id=ids[i];const auto f=e.faction_index;auto& total=totals_[std::min(f,7U)];auto it=previous_.find(id);const bool fresh=it==previous_.end();Previous old=fresh?Previous{}:it->second;
            if(fresh){event(tick,f,tick?"spawn":"initial",id,e.army_asset_id);if(tick){++total.spawned;if(e.construction_complete){++total.completed;event(tick,f,"construction_complete",id,e.army_asset_id);}}}
            else {
                const double dx=double(e.actor.position.x)-old.position.x,dy=double(e.actor.position.y)-old.position.y;const double distance=std::hypot(dx,dy);total.distance+=distance;
                if(!old.complete&&e.construction_complete){++total.completed;event(tick,f,"construction_complete",id,e.army_asset_id);}
                if(!old.terminal&&e.root_model_terminal){++total.lost;event(tick,f,"terminal",id,e.army_asset_id);}
                if(!old.removed&&e.runtime_removed)event(tick,f,"removed",id,e.army_asset_id);
                if(e.root_model_integrity<old.hp){const auto lost=int64_t(old.hp)-e.root_model_integrity;total.hp_lost+=lost;event(tick,f,"root_hp_loss",id,e.army_asset_id,lost);}
                if(e.root_model_integrity>old.hp)event(tick,f,"root_hp_gain",id,e.army_asset_id,int64_t(e.root_model_integrity)-old.hp);
                if(old.faction!=f)event(tick,f,"ownership_changed",id,e.army_asset_id,old.faction);
                if(old.linked&&!e.production_linked_to_parent)event(tick,f,"producer_release",id,e.army_asset_id);
            }
            const auto target=id_at(e.target_entity_index);
            if((fresh&&target)||(!fresh&&target!=old.target))event(tick,f,"attack_target",id,e.army_asset_id,0,target);
            const auto shots=e.weapon_volleys_fired-(fresh?0:old.volleys);if(shots){total.volleys+=shots;event(tick,f,"weapon_volley",id,e.army_asset_id,shots,target);}
            if(!same(e.actor.move_target,old.goal)){++total.move_commands;event(tick,f,e.actor.move_target?"move_target":"movement_stop",id,e.army_asset_id,e.actor.move_target?float_to_q12(e.actor.move_target->x):0,e.actor.move_target?uint64_t(int64_t(float_to_q12(e.actor.move_target->y))):0);}
            if(e.production_building&&(fresh||!old.producing||old.production!=e.production_asset_id||e.production_elapsed_ticks<old.prod_elapsed))event(tick,f,"production_start",id,e.army_asset_id,e.production_asset_id);
            if(e.research_pending_payment&&(fresh||!old.pending||old.research!=e.research_technology_id))event(tick,f,"research_queued",id,e.army_asset_id,e.research_technology_id);
            if(e.research_active&&(fresh||!old.active||old.research!=e.research_technology_id))event(tick,f,"research_start",id,e.army_asset_id,e.research_technology_id);
            const bool wants_move=e.actor.move_target&&e.motion.movable()&&!e.root_model_terminal&&!e.production_linked_to_parent;
            const bool stationary=!fresh&&float_to_q12(old.position.x)==float_to_q12(e.actor.position.x)&&float_to_q12(old.position.y)==float_to_q12(e.actor.position.y);
            uint32_t stopped=wants_move&&stationary?old.stationary+1U:0U;const bool stalled=stopped>=200;
            if(stalled&&!old.stalled){++total.stalls;event(tick,f,"stationary_10s",id,e.army_asset_id,target?1:0,target);}
            if(old.stalled&&!stalled)event(tick,f,"stationary_end",id,e.army_asset_id,old.stationary);
            Previous now;now.id=id;now.faction=f;now.arm=e.army_asset_id;now.volleys=e.weapon_volleys_fired;now.hp=e.root_model_integrity;now.position=e.actor.position;now.complete=e.construction_complete;now.terminal=e.root_model_terminal;now.removed=e.runtime_removed;now.active=e.research_active;now.pending=e.research_pending_payment;now.research=e.research_technology_id;now.production=e.production_asset_id;now.prod_elapsed=e.production_elapsed_ticks;now.producing=e.production_building;now.linked=e.production_linked_to_parent;now.stationary=stopped;now.stalled=stalled;now.target=target;now.goal=e.actor.move_target;previous_[id]=now;
            hash(id);hash(f);hash(e.army_asset_id);hash(e.runtime_model_class_id);hash(uint32_t(float_to_q12(e.actor.position.x)));hash(uint32_t(float_to_q12(e.actor.position.y)));hash(uint32_t(e.root_model_integrity));hash(e.root_model_terminal);hash(e.runtime_removed);hash(e.research_technology_id);hash(e.research_elapsed_ticks);hash(e.research_active);hash(e.production_asset_id);hash(e.production_elapsed_ticks);hash(e.weapon_volleys_fired);hash(target);
            if(tick%entity_sample_interval_==0)entities_<<tick<<','<<id<<','<<i<<','<<f<<','<<e.army_asset_id<<','<<e.runtime_model_class_id<<','<<e.building<<','<<e.construction_complete<<','<<e.root_model_terminal<<','<<e.runtime_removed<<','<<float_to_q12(e.actor.position.x)<<','<<float_to_q12(e.actor.position.y)<<','<<float_to_q12(e.actor.position.z)<<','<<e.root_model_integrity<<','<<e.root_model_maximum_integrity<<','<<(e.actor.move_target?float_to_q12(e.actor.move_target->x):0)<<','<<(e.actor.move_target?float_to_q12(e.actor.move_target->y):0)<<','<<target<<','<<e.weapon_volleys_fired<<','<<e.production_asset_id<<','<<e.production_elapsed_ticks<<','<<e.production_duration_ticks<<','<<e.research_technology_id<<','<<e.research_active<<','<<e.research_pending_payment<<','<<e.research_elapsed_ticks<<','<<e.research_duration_ticks<<','<<e.production_linked_to_parent<<','<<e.military_ai_command_kind<<','<<e.combat_command_mode_flags<<','<<e.navigation_direct_route_valid<<','<<e.combat_follow_route_failures<<','<<e.combat_follow_blocked_callbacks<<','<<stopped<<'\n';
        }
        for(auto it=previous_.begin();it!=previous_.end();)if(!present.contains(it->first)){event(tick,it->second.faction,"unlinked_or_converted",it->first,it->second.arm);it=previous_.erase(it);}else ++it;
        if(run.next_military_order_trace_sequence>last_order_+1){std::vector<SimulationLabMilitaryOrderTrace> pending;for(const auto& o:run.military_order_trace)if(o.sequence>last_order_)pending.push_back(o);std::sort(pending.begin(),pending.end(),[](auto&a,auto&b){return a.sequence<b.sequence;});for(const auto& o:pending){orders_<<o.tick<<','<<o.sequence<<','<<o.faction_index<<','<<id_at(o.source_entity_index)<<','<<uint32_t(o.workspace)<<','<<id_at(o.target_entity_index)<<','<<o.collected_count<<','<<o.accumulated_strength<<','<<o.selected_score<<','<<o.best_site_score<<','<<o.best_anchor_score<<','<<o.best_hostile_score<<','<<o.requested.x<<','<<o.requested.y<<','<<o.stored.x<<','<<o.stored.y<<','<<o.command_mode<<','<<o.command_generation<<','<<o.route_started<<','<<o.direct_route<<','<<o.target_owner_live<<','<<o.target_from_unseen<<'\n';if(o.faction_index<8)++totals_[o.faction_index].orders;last_order_=o.sequence;}}
        for(uint32_t f=1;f<=run.active_faction_count;++f){
            hash(run.faction_economy[f].xenite_current_q4);hash(run.faction_economy[f].tritium_current_q4);hash(run.faction_lifecycle_states[f]);hash(run.faction_packed_relation_states[f]);
            if(initialized_&&run.faction_lifecycle_states[f]!=lifecycle_[f])event(tick,f,"lifecycle",0,0,run.faction_lifecycle_states[f]);lifecycle_[f]=run.faction_lifecycle_states[f];
            for(uint32_t w=0;w<8;++w){hash(run.faction_technology_masks[f][w]);if(initialized_){const auto gained=run.faction_technology_masks[f][w]&~masks_[f][w];for(uint32_t bit=0;bit<32;++bit)if(gained&(1U<<bit))event(tick,f,"technology_unlocked",0,0,w*32+bit);}masks_[f][w]=run.faction_technology_masks[f][w];}
            const auto& research=run.ai_research_trace[f];if(research.last_completion_tick&&completion_tick_[f]!=research.last_completion_tick){completion_tick_[f]=research.last_completion_tick;++totals_[f].research_completed;event(research.last_completion_tick,f,"research_complete",0,0,research.last_completed_technology_id);}
            const auto& d=run.ai_workspace_diagnostics[f];const auto& s=run.ai_strategic_construction_trace[f];const auto& a=run.ai_army_profile_trace[f];const auto& b=run.ai_purchase_block_diagnostics[f];
            if(d.last_planning_tick&&plan_tick_[f]!=d.last_planning_tick){plan_tick_[f]=d.last_planning_tick;planning_<<d.last_planning_tick<<','<<f<<','<<d.own_entities<<','<<d.live_pioneers<<','<<d.live_structures<<','<<d.effective_presence_owners<<','<<d.general05<<','<<d.flagged06<<','<<d.resource08<<','<<d.base09<<','<<d.large10<<','<<s.selected_factory<<','<<s.existing_factory_count<<','<<s.selected_lab<<','<<s.has_resource330<<','<<s.arms_factories_unlocked<<','<<s.factory_capacity_available<<','<<s.lab_capacity_available;for(size_t j=0;j<3;++j)planning_<<','<<a.best_asset_id[j]<<','<<a.best_score[j];planning_<<','<<research.best_technology_id<<','<<research.best_score<<','<<run.ai_candidate_cache_count[f]<<','<<b.balance_blocks<<','<<b.ceiling_blocks<<'\n';}
            if(research.last_rebuild_tick&&research_tick_[f]!=research.last_rebuild_tick){research_tick_[f]=research.last_rebuild_tick;for(uint32_t i=0;i<research.candidate_count;++i){const auto& c=research.candidates[i];research_<<research.last_rebuild_tick<<','<<f<<','<<c.technology_id<<','<<c.source_army_id<<','<<c.source_class<<','<<c.score<<','<<c.reason<<','<<c.score_kind<<','<<c.base_score<<','<<c.xenite_cost_q4<<','<<c.compatible_model_id<<','<<c.compatible_model_class<<','<<c.compatible_arm_positive<<','<<c.best_potential_arm_id<<','<<c.best_potential_arm_score<<'\n';}}
            const auto& purchase=run.ai_purchase_trace[f];if(purchase.tick&&purchase_tick_[f]!=purchase.tick){purchase_tick_[f]=purchase.tick;for(uint32_t i=0;i<purchase.recorded_count;++i){const auto& c=purchase.candidates[i];purchases_<<purchase.tick<<','<<f<<','<<c.rank<<','<<c.id<<','<<c.kind<<','<<c.packed_score<<','<<c.cost_q4<<','<<c.budget_before_q4<<','<<c.budget_after_q4<<','<<uint32_t(c.decision)<<','<<purchase.cached<<'\n';}}
            if(tick%faction_sample_interval_==0){uint32_t buildings=0,units=0,pioneers=0,constructing=0,active_research=0,active_production=0;for(const auto& e:run.entities)if(e.faction_index==f&&!e.runtime_removed){if(e.building)++buildings;else ++units;pioneers+=e.runtime_model_class_id==18;constructing+=!e.construction_complete;active_research+=e.research_active;active_production+=e.production_building;}
                const auto count_queue=[&](const auto& q){return std::count_if(q.begin(),q.end(),[&](auto& r){return r.faction_index==f;});};const auto& t=totals_[f];const auto& e=run.faction_economy[f];
                factions_<<std::setprecision(12)<<tick<<','<<tick/20<<','<<f<<','<<uint32_t(run.faction_lifecycle_states[f])<<','<<buildings<<','<<units<<','<<pioneers<<','<<constructing<<','<<count_queue(run.pending_asset_requests)<<','<<count_queue(run.secondary_purchase_requests)<<','<<e.xenite_current_q4<<','<<e.tritium_current_q4<<','<<e.xenite_storage_limit_q4.value_or(0)<<','<<e.tritium_storage_limit_q4.value_or(0)<<','<<run.faction_energy_generation_capacity_q4[f]<<','<<run.faction_supplied_energy_demand_q4[f]<<','<<run.faction_unpowered_energy_demand_q4[f]<<','<<run.faction_xenite_extraction_rate_q4_per_tick[f]<<','<<run.faction_tritium_extraction_rate_q4_per_tick[f]<<','<<t.distance<<','<<t.volleys<<','<<t.hp_lost<<','<<t.spawned<<','<<t.completed<<','<<t.lost<<','<<t.research_completed<<','<<t.orders<<','<<t.move_commands<<','<<t.stalls<<','<<active_research<<','<<active_production<<','<<run.faction_score[f].explored_terrain_percent<<','<<run.rng_state;for(auto mask:run.faction_technology_masks[f])factions_<<','<<mask;factions_<<'\n';}
        }
        initialized_=true;
        if(tick%20==0){const auto& m=run.metrics;hashes_<<tick<<','<<std::hex<<rolling_<<std::dec<<','<<run.rng_state<<','<<run.entities.size()<<','<<run.projectiles.size()<<','<<m.planning_cycles<<','<<m.weapon_shots_fired<<','<<m.projectile_damage_applied<<'\n';metrics_<<tick<<','<<m.planning_cycles<<','<<m.completed_builds<<','<<m.production_jobs_started<<','<<m.production_jobs_completed<<','<<m.research_jobs_started<<','<<m.research_jobs_completed<<','<<m.weapon_shots_fired<<','<<m.projectile_impacts<<','<<m.projectile_damage_applied<<','<<m.water_damage_applications<<','<<m.water_damage_integrity_lost<<','<<m.collision_contacts<<','<<m.navigation_routed_solves<<','<<m.navigation_actual_step_blocks<<','<<m.factory_exit_route_retries<<','<<m.entities_removed<<','<<m.pioneer_conversions_completed<<'\n';}
        if(tick%1200==0){std::cerr<<"AUDIT_PROGRESS tick="<<tick<<" minutes="<<tick/1200<<" entities="<<run.entities.size()<<" shots="<<run.metrics.weapon_shots_fired<<" cpu_seconds="<<double(std::clock()-cpu_start_)/CLOCKS_PER_SEC<<'\n';events_.flush();factions_.flush();entities_.flush();hashes_.flush();}
    }
    void finish(){
        // Publish complete files only after checked flush/close. Readers must not
        // mistake a live or interrupted writer for a complete simulation trace.
        for(auto* stream:{&events_,&factions_,&entities_,&planning_,&research_,&purchases_,&orders_,&hashes_,&metrics_}){
            stream->flush();if(!*stream)throw std::runtime_error("Audit write failed");
            stream->close();if(stream->fail())throw std::runtime_error("Audit close failed");
        }
        for(const char* name:{"events.csv","factions.csv","entities.csv","planning.csv","research_candidates.csv","purchases.csv","military_orders.csv","state_hashes.csv","metrics.csv","assets.csv","technologies.csv","faction_names.csv"})
            std::filesystem::rename(dir_/(std::string(name)+".partial"),dir_/name);
    }
    void pace(){if(cpu_share_<=0||cpu_share_>=1)return;const double used=double(std::clock()-cpu_start_)/CLOCKS_PER_SEC;const auto target=start_+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double>(used/cpu_share_));if(target>std::chrono::steady_clock::now())std::this_thread::sleep_until(target);}
};
}
