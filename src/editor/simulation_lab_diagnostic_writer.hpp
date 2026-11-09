#pragma once
#include <bit>
#include <cmath>
#include "thandor/async_diagnostic_log.hpp"
#include "thandor/simulation_lab_worker.hpp"
#include "thandor/simulation_lab_diagnostics.hpp"
#include "thandor/simulation_command_markers.hpp"
#include "thandor/retail_selection_metrics.hpp"
#include "thandor/simulation_lab_targeting.hpp"
#include "thandor/fog_preview.hpp"
#include "thandor/pioneer_resource_grid.hpp"
#include <limits>
#include <unordered_set>
namespace editor {
inline const char* simulation_lab_purchase_decision_name(thandor::SimulationLabRun::PurchaseDecision value) noexcept {
    using Decision=thandor::SimulationLabRun::PurchaseDecision;
    switch(value) {
        case Decision::NotReached:return "not_reached";
        case Decision::Unaffordable:return "unaffordable";
        case Decision::ResearchQueued:return "research_queued";
        case Decision::ResearchNoOp:return "research_noop";
        case Decision::ResearchUnavailable:return "research_unavailable_or_busy";
        case Decision::ProducerClassUsed:return "producer_class_used";
        case Decision::NoProducer:return "no_eligible_producer";
        case Decision::QueueFull:return "secondary_queue_full";
        case Decision::ArmyQueued:return "army_queued";
        case Decision::MissingAsset:return "missing_asset";
    }
    return "unknown";
}
struct SimulationLabRenderReceipt {
    std::size_t entity{};
    uint32_t run{},tick{},army{},faction{},triangles{};
    const char* result{"not_attempted"};
    // Vertices retain their order when the final queue reorders index packets.
    uint32_t first_vertex{},vertex_count{},root_model_definition{},active_model_nodes{};
    thandor::Vec3f bounds_min{},bounds_max{};
    bool bounds_valid{};
};
inline void write_walker_contact_diagnostics(std::ostream& out,std::size_t run,uint32_t tick,
    std::size_t index,const thandor::SimulationLabEntity& entity){
    if(entity.runtime_model_class_id!=3U)return;
    const auto& a=entity.actor;
    auto feet=a.articulated_foot_anchor_world;
    if(a.articulated_contact_transition_active&&(a.articulated_was_moving||a.articulated_contact_settling)){
        const auto side=a.articulated_swing_side&1U;
        const float t=static_cast<float>(std::min(a.articulated_contact_progress_q12,4096U))/4096.F;
        const auto target=a.articulated_foot_target_world[side];auto& p=feet[side];
        p={p.x+(target.x-p.x)*t,p.y+(target.y-p.y)*t,
            p.z+(target.z-p.z)*t+4.F*thandor::q12_to_float(entity.articulated_contact_lift_q12)*t*(1.F-t)};
    }
    const bool support_valid=entity.articulated_contact_geometry_enabled&&a.articulated_contacts_initialized;
    const thandor::Vec3f support=support_valid?thandor::Vec3f{
        (feet[0].x+feet[1].x)*.5F,(feet[0].y+feet[1].y)*.5F,
        (feet[0].z+feet[1].z)*.5F+entity.articulated_shared_foot_placement_offset+
            thandor::q12_to_float(entity.articulated_root_height_q12)}:a.position;
    out<<"WALKER_CONTACT run="<<run<<" tick="<<tick<<" entity="<<index
        <<" body_yaw="<<a.body_yaw_turn16<<" support_yaw="<<thandor::simulation_articulated_support_heading(a)
        <<" progress="<<a.articulated_contact_progress_q12<<" swing="<<a.articulated_swing_side
        <<" support_valid="<<support_valid<<" body="<<a.position.x<<','<<a.position.y<<','<<a.position.z
        <<" support="<<support.x<<','<<support.y<<','<<support.z
        <<" support_xy_error="<<std::hypot(a.position.x-support.x,a.position.y-support.y)
        <<" support_z_error="<<std::abs(a.position.z-support.z);
    for(std::size_t side=0;side<2;++side){
        const auto& p=a.articulated_foot_anchor_world[side];const auto& q=a.articulated_foot_target_world[side];
        out<<" anchor"<<side<<"="<<p.x<<','<<p.y<<','<<p.z
            <<" target"<<side<<"="<<q.x<<','<<q.y<<','<<q.z
            <<" heading"<<side<<"="<<a.articulated_contact_heading_turn16[side]
            <<" target_heading"<<side<<"="<<a.articulated_contact_target_heading_turn16[side];
    }
    out<<'\n';
}
struct SimulationLabDiagnosticSample {
    std::shared_ptr<const thandor::SimulationLabExperiment> snapshot;
    thandor::SimulationLabWorkerPerformance performance{};
    thandor::AsyncDiagnosticLog::Stats writer_stats{};
    uint32_t pending_ticks{},duration_ticks{},active_factions{};
    uint64_t generation_us{},peak_generation_us{};
    double simulation_lab_last_sync_milliseconds_{},dynamic_scene_build_milliseconds_{};
    double dynamic_scene_pre_shadow_milliseconds_{},dynamic_scene_shadow_milliseconds_{};
    double dynamic_scene_fog_milliseconds_{},dynamic_scene_model_setup_milliseconds_{};
    double dynamic_scene_effects_milliseconds_{},dynamic_scene_sort_milliseconds_{};
    double generated_shadow_build_milliseconds_{};
    bool projected_shadows_enabled{};
    size_t dynamic_shadow_templates{},dynamic_shadow_vertices{};
    int command_marker_run_index_{},fog_preview_mode_{},fog_preview_faction_{};
    double command_marker_build_milliseconds_{};
    thandor::SimulationCommandMarkerDiagnostics command_marker_diagnostics_{};
    std::size_t command_marker_count_{};
    bool fog_preview_all_explored_{};
    uint64_t audio_one_shot_requests{},audio_submitted{},audio_fog_filtered{},audio_inaudible{},audio_missing_samples{},audio_backend_rejected{},audio_loop_requests{},audio_path_cache_hits{};
    uint64_t audio_host_queued{},audio_host_completed{},audio_host_failed{};
    size_t audio_active_loops{};
    bool audio_enabled{},audio_map_focus{};
    uint32_t audio_effects_gain_q15{};
    float audio_camera_distance{};
    thandor::Vec3f audio_listener_position{};
    std::vector<SimulationLabRenderReceipt> render_receipts;
};
class SimulationLabDiagnosticWriter {
public:
    SimulationLabDiagnosticWriter(const std::filesystem::path& path,std::string header,
        thandor::FieldGrid field,std::unordered_set<uint32_t> presets)
        :field_(std::move(field)),preset_ids_(std::move(presets)),log_(path,std::move(header)){}
    ~SimulationLabDiagnosticWriter(){close("writer_destroyed");}
    bool append(SimulationLabDiagnosticSample sample,bool force){
        sample.writer_stats=log_.stats();
        return log_.enqueue([this,sample=std::move(sample),force](std::ostream& stream){format(stream,sample,force);},force);
    }
    void error(std::string message){log_.enqueue([message=std::move(message)](std::ostream& out){out<<"WORKER_ERROR "<<message<<'\n';},true);}
    void close(const std::string& reason){log_.close(reason);}
    thandor::AsyncDiagnosticLog::Stats stats() const{return log_.stats();}
private:
    void format(std::ostream& stream,const SimulationLabDiagnosticSample& sample,bool force){
        if(simulation_lab_diagnostic_limit_reached_)return;
    const auto perf=sample.performance;
    for(const auto& r:sample.render_receipts)
        stream<<"CPU_RENDER run="<<r.run<<" tick="<<r.tick<<" entity="<<r.entity
            <<" arm="<<r.army<<" faction="<<r.faction<<" result="<<r.result
            <<" triangles="<<r.triangles<<" first_vertex="<<r.first_vertex<<" vertices="<<r.vertex_count
            <<" root_mdl="<<r.root_model_definition<<" active_nodes="<<r.active_model_nodes
            <<" bounds_valid="<<r.bounds_valid
            <<" bounds_min="<<r.bounds_min.x<<','<<r.bounds_min.y<<','<<r.bounds_min.z
            <<" bounds_max="<<r.bounds_max.x<<','<<r.bounds_max.y<<','<<r.bounds_max.z
            <<" gpu_verified=0\n";
    stream<<"PERF sample="<<++simulation_lab_log_samples_
        <<" step_wall_us="<<perf.step_wall_us<<" run_sum_us="<<perf.run_sum_us
        <<" slowest_run="<<perf.slowest_run_index<<" slowest_run_us="<<perf.slowest_run_us
        <<" snapshot_copy_us="<<perf.snapshot_copy_us<<" snapshot_peak_us="<<perf.peak_snapshot_copy_us
        <<" snapshot_copies="<<perf.snapshot_copies
        <<" reclaim_us="<<perf.retired_snapshot_reclaim_us
        <<" pending_ticks="<<sample.pending_ticks
        <<" peak_pending_ticks="<<perf.peak_pending_ticks
        <<" worker_accepted_ticks="<<perf.accepted_ticks
        <<" worker_deferred_ticks="<<perf.deferred_ticks
        <<" worker_backpressure_events="<<perf.backpressure_events
        <<" gui_sync_ms="<<sample.simulation_lab_last_sync_milliseconds_
        <<" gui_dynamic_build_ms="<<sample.dynamic_scene_build_milliseconds_
        <<" gui_pre_shadow_ms="<<sample.dynamic_scene_pre_shadow_milliseconds_
        <<" gui_fog_ms="<<sample.dynamic_scene_fog_milliseconds_
        <<" gui_model_setup_ms="<<sample.dynamic_scene_model_setup_milliseconds_
        <<" gui_shadow_ms="<<sample.dynamic_scene_shadow_milliseconds_
        <<" gui_effects_ms="<<sample.dynamic_scene_effects_milliseconds_
        <<" gui_sort_ms="<<sample.dynamic_scene_sort_milliseconds_
        <<" static_shadow_build_ms="<<sample.generated_shadow_build_milliseconds_
        <<" projected_shadows="<<sample.projected_shadows_enabled
        <<" shadow_templates="<<sample.dynamic_shadow_templates
        <<" shadow_vertices="<<sample.dynamic_shadow_vertices
        <<" command_marker_run="<<sample.command_marker_run_index_
        <<" command_marker_build_ms="<<sample.command_marker_build_milliseconds_
        <<" marker_owners_examined="<<sample.command_marker_diagnostics_.owners_examined
        <<" marker_owners_filtered="<<sample.command_marker_diagnostics_.owners_filtered
        <<" marker_candidates="<<sample.command_marker_diagnostics_.candidates
        <<" marker_dedup_comparisons="<<sample.command_marker_diagnostics_.duplicate_comparisons
        <<" marker_duplicates="<<sample.command_marker_diagnostics_.duplicates
        <<" marker_visited="<<sample.command_marker_diagnostics_.markers_visited
        <<" marker_rendered="<<sample.command_marker_count_
        <<" marker_limit_stop="<<sample.command_marker_diagnostics_.stopped_early
        <<" previous_log_ms="<<sample.writer_stats.last_write_ms
        <<" log_writer_peak_ms="<<sample.writer_stats.peak_write_ms
        <<" log_written="<<sample.writer_stats.written<<" log_coalesced="<<sample.writer_stats.coalesced
        <<" log_failures="<<sample.writer_stats.failures<<'\n';
    stream<<"AUDIO one_shot_requests="<<sample.audio_one_shot_requests<<" submitted="<<sample.audio_submitted
        <<" fog_filtered="<<sample.audio_fog_filtered<<" inaudible="<<sample.audio_inaudible
        <<" missing_samples="<<sample.audio_missing_samples<<" backend_rejected="<<sample.audio_backend_rejected
        <<" loop_requests="<<sample.audio_loop_requests<<" active_loops="<<sample.audio_active_loops
        <<" path_cache_hits="<<sample.audio_path_cache_hits<<" host_queued="<<sample.audio_host_queued
        <<" host_completed="<<sample.audio_host_completed<<" host_failed="<<sample.audio_host_failed
        <<" enabled="<<sample.audio_enabled<<" effects_gain_q15="<<sample.audio_effects_gain_q15
        <<" map_focus="<<sample.audio_map_focus<<" camera_distance="<<sample.audio_camera_distance
        <<" listener_x="<<sample.audio_listener_position.x<<" listener_y="<<sample.audio_listener_position.y
        <<" listener_z="<<sample.audio_listener_position.z<<'\n';
    const auto position=stream.tellp();
    if(position>=0&&position>=static_cast<std::streamoff>(64*1024*1024)){
        stream<<"LOG_LIMIT 64 MiB; further samples omitted\n";
        stream.flush();
        simulation_lab_diagnostic_limit_reached_=true;
        return;
    }
    const auto runs=sample.snapshot->runs();
    if(simulation_lab_diagnostic_states_.size()!=runs.size())
        simulation_lab_diagnostic_states_.resize(runs.size());
    for(std::size_t ri=0;ri<runs.size();++ri){
        const auto& run=runs[ri];
        auto& prev=simulation_lab_diagnostic_states_[ri];
        if(!force&&prev.last_sample_tick==run.tick)continue; // immutable, including paused/finished runs
        prev.last_sample_tick=run.tick;
        const auto plans=thandor::simulation_lab_plan_summary(run);
        std::array<uint32_t,8> living{},pioneers{},structures{},zero_vision{},terminal{},pending_convert{};
        std::size_t released_slots=0U,retained_terminal_slots=0U,live_move_targets=0U;
        uint64_t fingerprint=1469598103934665603ULL;
        const auto mix=[&](uint64_t value){fingerprint^=value;fingerprint*=1099511628211ULL;};
        for(const auto& e:run.entities){
            if(e.runtime_removed)++released_slots;
            else if(e.root_model_terminal)++retained_terminal_slots;
            else if(e.actor.move_target||e.actor.move_waypoint)++live_move_targets;
            if(e.faction_index==0U||e.faction_index>=living.size())continue;
            const auto f=e.faction_index;
            mix(f);mix(e.army_asset_id);mix(e.runtime_model_class_id);
            mix(e.source_placement_index);mix(e.synthetic?1U:0U);
            mix(e.root_model_terminal?1U:0U);mix(e.construction_complete?1U:0U);
            mix(e.class18_conversion_effect_complete_tick);
            if(e.root_model_terminal){++terminal[f];continue;}
            ++living[f];
            if(e.runtime_model_class_id==18U){
                ++pioneers[f];
                if(e.class18_conversion_effect_complete_tick!=0U)++pending_convert[f];
            }
            if(e.building)++structures[f];
            if(e.fog_uncover_radius_q12<=0)++zero_vision[f];
        }
        mix(run.metrics.pioneer_conversions_completed);
        mix(run.metrics.pioneer_conversion_effects_started);
        mix(run.metrics.completed_builds);
        // Retry totals remain in periodic records; they do not identify a world
        // transition and previously forced megabytes of repeated full snapshots.
        mix(run.finished ? 1U : 0U);
        mix(run.pending_asset_requests.size());
        for (const auto& request : run.pending_asset_requests) {
            mix(request.faction_index); mix(request.army_asset_id); mix(request.created_tick);
        }
        mix(run.secondary_purchase_requests.size());
        for (const auto& request : run.secondary_purchase_requests) {
            mix(request.faction_index); mix(request.army_asset_id);
        }
        mix(run.metrics.component_terminal_events);
        mix(run.next_military_order_trace_sequence);
        // Trigger processing can disable an entire faction without causing a
        // MODEL terminal event. Include those transitions in the log cadence.
        for(const auto state:run.faction_lifecycle_states)mix(state);
        for(const auto& trigger:run.end_condition_triggers)mix(trigger.state_flags);
        const bool changed=prev.fingerprint!=fingerprint;
        const bool long_run=sample.duration_ticks>3600U;
        const uint32_t detail_interval=long_run
            ?std::max(64U,sample.duration_ticks/32U):64U;
        const bool periodic=run.tick<prev.last_trace_tick||
            run.tick-prev.last_trace_tick>=detail_interval;
        const bool detailed=force||periodic||run.finished||
            prev.tick==std::numeric_limits<uint32_t>::max();
        if(!force&&!changed&&!periodic)continue;
        if(!force&&run.tick==prev.tick&&!changed)continue;
        stream
            <<"RUN index="<<ri<<" tick="<<run.tick<<" seed="<<run.seed
            <<" swept_value="<<run.swept_value
            <<" finished="<<run.finished<<" active="<<run.active_faction_count
            <<" ai_mask="<<run.ai_controlled_faction_mask
            <<" forensic_planner_filter="<<run.ai_planner_dispatch_mask.has_value()
            <<" planner_dispatch_mask="<<run.ai_planner_dispatch_mask.value_or(0xFEU)
            <<" game_speed_q8="<<run.game_speed_q8
            <<" runtime_callback_batch="<<sample.snapshot->config().runtime_callback_step_ticks
            <<" active_view_faction="<<run.active_view_faction_index
            <<" alert_relation_tick="<<run.faction_relation_transition_ticks[
                std::min(run.active_view_faction_index,7U)]
            <<" mapped_alert_sound_slots="<<run.mapped_terrain_sound_asset_ids.size()
            <<" ai_planning_enabled="<<run.ai_planning_enabled<<" reduced_update="<<run.reduced_update
            <<" entities="<<run.entities.size()
            <<" released_entity_slots="<<released_slots
            <<" retained_terminal_slots="<<retained_terminal_slots
            <<" live_move_targets="<<live_move_targets
            <<" one_shot_journal_nodes="<<(run.next_one_shot_event_sequence-1U)
            <<" planning_passes_total="<<run.metrics.planning_cycles
            <<" primary_pending="<<plans.primary<<" secondary_pending="<<plans.secondary
            <<" oldest_primary_age_ticks="<<plans.oldest_primary_age_ticks
            <<" oldest_secondary_age_ticks="<<plans.oldest_secondary_age_ticks
            <<" primary_placement_attempts="<<plans.primary_placement_attempts
            <<" planned="<<run.metrics.planned_builds
            <<" built="<<run.metrics.completed_builds
            <<" military_sites="<<run.metrics.military_ai_site_orders
            <<" military_anchors="<<run.metrics.military_ai_anchor_orders
            <<" military_groups="<<run.metrics.military_ai_group_orders
            <<" military_site_orders_with_live_target="<<run.metrics.military_ai_site_orders_with_live_target
            <<" military_anchor_orders_with_live_target="<<run.metrics.military_ai_anchor_orders_with_live_target
            <<" military_ai_busy_skips="<<run.metrics.military_ai_busy_skips
            <<" military_unseen_group_bounded="<<run.metrics.military_ai_unseen_group_targets_bounded
            <<" military_unseen_group_unavailable="<<run.metrics.military_ai_unseen_group_targets_unavailable
            <<" military_order_trace_count="<<run.military_order_trace_count
            <<" military_order_trace_overwrites="<<run.military_order_trace_overwrites
            <<" overlap_route_rebuilds="<<run.metrics.navigation_overlap_group_rebuilds
            <<" overlap_route_pairs="<<run.metrics.navigation_overlap_group_pairs
            <<" overlap_route_capacity="<<run.metrics.navigation_overlap_group_capacity_reached
            <<" overlap_route_replans="<<run.metrics.navigation_overlap_group_source_replans
            <<" alert_decay_steps="<<run.metrics.impact_anchor_decay_steps
            <<" alert_expirations="<<run.metrics.impact_anchor_expirations
            <<" military_movement="<<run.metrics.military_movement_distance_world
            <<" gui_sync_ms="<<sample.simulation_lab_last_sync_milliseconds_
            <<" gui_dynamic_build_ms="<<sample.dynamic_scene_build_milliseconds_
        <<" gui_pre_shadow_ms="<<sample.dynamic_scene_pre_shadow_milliseconds_
        <<" gui_fog_ms="<<sample.dynamic_scene_fog_milliseconds_
        <<" gui_model_setup_ms="<<sample.dynamic_scene_model_setup_milliseconds_
        <<" gui_shadow_ms="<<sample.dynamic_scene_shadow_milliseconds_
        <<" gui_effects_ms="<<sample.dynamic_scene_effects_milliseconds_
        <<" gui_sort_ms="<<sample.dynamic_scene_sort_milliseconds_
        <<" static_shadow_build_ms="<<sample.generated_shadow_build_milliseconds_
        <<" projected_shadows="<<sample.projected_shadows_enabled
        <<" shadow_templates="<<sample.dynamic_shadow_templates
        <<" shadow_vertices="<<sample.dynamic_shadow_vertices
            <<" worker_generation_us="<<sample.generation_us
            <<" worker_peak_generation_us="<<sample.peak_generation_us
            <<" physical_step_blocks="<<run.metrics.navigation_actual_step_blocks
            <<" factory_exit_retries="<<run.metrics.factory_exit_route_retries
            <<" runtime_replacements="<<run.metrics.runtime_technology_replacements
            <<" invalid_health_scales="<<run.metrics.runtime_technology_invalid_health_scales
            <<" production_smoke_pulses="<<run.metrics.production_timed_effects_spawned
            <<" units_spawned="<<run.metrics.class13_units_spawned
            <<" retries="<<run.metrics.pending_placement_retries
            <<" pending="<<run.pending_asset_requests.size()
            <<" conversions_started="<<run.metrics.pioneer_conversion_effects_started
            <<" conversions_completed="<<run.metrics.pioneer_conversions_completed
            <<" terminal_events="<<run.metrics.component_terminal_events
            <<" child_terminal_events="<<run.metrics.child_component_terminal_events
            <<" retired_v713_root_190_combat_events="<<run.metrics.terminal_root_destruction_effect_events
            <<" terminal_attachment_effect_events="<<run.metrics.terminal_attachment_effect_events
            <<" terminal_world_points="<<run.metrics.terminal_attachment_world_points_prepared
            <<" projectile_root_component_hits="<<run.metrics.projectile_root_component_hits
            <<" projectile_child_component_hits="<<run.metrics.projectile_child_component_hits
            <<" projectile_repair_contacts_healed="<<run.metrics.projectile_repair_contacts_healed
            <<" projectile_repair_contacts_rejected="<<run.metrics.projectile_repair_contacts_rejected
            <<" projectile_repair_integrity_restored="<<run.metrics.projectile_repair_integrity_restored
            <<" removed_hierarchies="<<run.metrics.model_hierarchies_removed
            <<" removed_entities="<<run.metrics.entities_removed
            <<" destroyed_replacement_armies_spawned="<<run.metrics.destroyed_replacement_armies_spawned
            <<" destroyed_replacement_armies_unresolved="<<run.metrics.destroyed_replacement_armies_unresolved
            <<" environmental_effects="<<run.metrics.environmental_effects_spawned
            <<" environmental_shots="<<run.metrics.environmental_shots_spawned
            <<" extractor_site_tests="<<run.metrics.live_extractor_site_tests
            <<" extractor_site_accepts="<<run.metrics.live_extractor_site_accepts
            <<" extended_placement_searches="<<run.metrics.extended_placement_searches
            <<" extended_placement_tests="<<run.metrics.extended_placement_tests
            <<" extended_placement_accepts="<<run.metrics.extended_placement_accepts
            <<" placement_connectivity_tests="<<run.metrics.placement_connectivity_tests
                        <<" placement_connectivity_rejections="<<run.metrics.placement_connectivity_rejections
                        <<" special_reservation_tests="<<run.metrics.special_site_reservation_tests
            <<" special_reservation_accepts="<<run.metrics.special_site_reservation_accepts
            <<" temporary_probe_owners="<<run.metrics.special_site_probe_owners
            <<" storage_site_bonus_events="<<run.metrics.live_storage_site_bonus_events
            <<" vision_phase_tick="<<run.current_visibility_occupancy_tick
            <<" fog_selected="<<sample.fog_preview_mode_
            <<" all_explored="<<sample.fog_preview_all_explored_<<'\n';
        if(run.next_military_order_trace_sequence<=prev.last_military_order_trace_sequence)
            prev.last_military_order_trace_sequence=0U;
        const auto trace_count=std::min<std::size_t>(
            run.military_order_trace_count,thandor::kSimulationLabMilitaryOrderTraceCapacity);
        const auto trace_oldest=trace_count==thandor::kSimulationLabMilitaryOrderTraceCapacity
            ?static_cast<std::size_t>(run.military_order_trace_write_index):0U;
        for(std::size_t offset=0;offset<trace_count;++offset){
            const auto& trace=run.military_order_trace[
                (trace_oldest+offset)%thandor::kSimulationLabMilitaryOrderTraceCapacity];
            if(trace.sequence==0U||trace.sequence<=prev.last_military_order_trace_sequence)continue;
            stream<<"MILITARY_ORDER run="<<ri
                <<" sequence="<<trace.sequence
                <<" tick="<<trace.tick
                <<" faction="<<trace.faction_index
                <<" source="<<trace.source_entity_index
                <<" workspace="<<static_cast<std::uint32_t>(trace.workspace)
                <<" workspace_entry="<<trace.workspace_entry_index
                <<" target="<<trace.target_entity_index
                <<" target_live="<<trace.target_owner_live
                <<" target_from_unseen="<<trace.target_from_unseen
                <<" collected="<<trace.collected_count
                <<" strength="<<trace.accumulated_strength
                <<" selected_score="<<trace.selected_score
                <<" site_score="<<trace.best_site_score
                <<" anchor_score="<<trace.best_anchor_score
                <<" hostile_score="<<trace.best_hostile_score
                <<" requested_x_q12="<<trace.requested.x
                <<" requested_y_q12="<<trace.requested.y
                <<" primary_x_q12="<<trace.primary.x
                <<" primary_y_q12="<<trace.primary.y
                <<" fallback_x_q12="<<trace.fallback.x
                <<" fallback_y_q12="<<trace.fallback.y
                <<" stored_x_q12="<<trace.stored.x
                <<" stored_y_q12="<<trace.stored.y
                <<" command_mode="<<trace.command_mode
                <<" command_generation="<<trace.command_generation
                <<" movement_flags="<<trace.movement_flags
                <<" cooldown="<<trace.cooldown
                <<" route_started="<<trace.route_started
                <<" direct_route="<<trace.direct_route<<'\n';
            prev.last_military_order_trace_sequence=trace.sequence;
        }
        for(const auto& request:run.pending_asset_requests){
            stream<<"PENDING run="<<ri<<" tick="<<run.tick
                    <<" faction="<<request.faction_index<<" arm="<<request.army_asset_id
                    <<" attempts="<<request.placement_attempts
                    <<" created="<<request.created_tick
                    <<" age_ticks="<<(run.tick>=request.created_tick?run.tick-request.created_tick:0U)<<'\n';
            }
        for(uint32_t f=1U;f<living.size();++f){
            const auto& ws=run.ai_workspace_diagnostics[f];
            stream<<"FACTION run="<<ri<<" f="<<f
                <<" live="<<living[f]<<" pioneers="<<pioneers[f]
                <<" pending_conversion="<<pending_convert[f]
                <<" buildings="<<structures[f]<<" terminal="<<terminal[f]
                <<" zero_vision="<<zero_vision[f]
                <<" ws05="<<ws.general05<<" ws08="<<ws.resource08
                <<" ws09="<<ws.base09<<" ws10="<<ws.large10
                <<" presence_owners="<<ws.effective_presence_owners
                <<" last_planning_tick="<<ws.last_planning_tick
                <<" ai_assigned="<<(((run.ai_controlled_faction_mask>>f)&1U)!=0U)
                <<" lifecycle="<<static_cast<unsigned>(run.faction_lifecycle_states[f])<<'\n';
            if(detailed){
                stream<<"ECONOMY run="<<ri<<" tick="<<run.tick
                    <<" faction="<<f<<" xenite="<<run.faction_economy[f].xenite_current_q4
                    <<" tritium="<<run.faction_economy[f].tritium_current_q4
                    <<" tritium_cap="<<run.faction_economy[f].tritium_storage_limit_q4.value_or(0U)
                    <<" tritium_cap_locked="<<run.faction_economy[f].tritium_storage_limit_locked
                    <<" purchase_block_ceiling="<<run.ai_purchase_block_diagnostics[f].ceiling_blocks
                    <<" purchase_block_balance="<<run.ai_purchase_block_diagnostics[f].balance_blocks
                    <<" purchase_last_id="<<run.ai_purchase_block_diagnostics[f].last_candidate_id
                    <<" purchase_last_tick="<<run.ai_purchase_block_diagnostics[f].last_tick
                    <<" purchase_last_reason="<<static_cast<uint32_t>(run.ai_purchase_block_diagnostics[f].last_reason)
                    <<" purchase_last_arm="<<run.ai_purchase_block_diagnostics[f].last_candidate_id
                    <<" purchase_last_cost="<<run.ai_purchase_block_diagnostics[f].last_cost_q4
                    <<" purchase_last_balance="<<run.ai_purchase_block_diagnostics[f].last_balance_q4
                    <<" purchase_last_cap="<<run.ai_purchase_block_diagnostics[f].last_storage_cap_q4
                    <<" purchase_last_cached="<<run.ai_purchase_block_diagnostics[f].last_cached
                    <<" army_ws11="<<run.ai_army_profile_trace[f].workspace11_count
                    <<" army_walkers="<<run.ai_army_profile_trace[f].eligible_walkers
                    <<" army_gliders="<<run.ai_army_profile_trace[f].eligible_gliders
                    <<" army_tracked="<<run.ai_army_profile_trace[f].eligible_tracked
                    <<" army_aircraft="<<run.ai_army_profile_trace[f].eligible_aircraft
                    <<" army_naval="<<run.ai_army_profile_trace[f].eligible_naval
                    <<" army_turrets="<<run.ai_army_profile_trace[f].eligible_turrets
                    <<" army_best_a="<<run.ai_army_profile_trace[f].best_asset_id[0]
                    <<" army_score_a="<<run.ai_army_profile_trace[f].best_score[0]
                    <<" army_alt_a="<<run.ai_army_profile_trace[f].best_advanced_asset_id[0]
                    <<" army_alt_score_a="<<run.ai_army_profile_trace[f].best_advanced_score[0]
                    <<" army_best_b="<<run.ai_army_profile_trace[f].best_asset_id[1]
                    <<" army_score_b="<<run.ai_army_profile_trace[f].best_score[1]
                    <<" army_alt_b="<<run.ai_army_profile_trace[f].best_advanced_asset_id[1]
                    <<" army_alt_score_b="<<run.ai_army_profile_trace[f].best_advanced_score[1]
                    <<" army_best_c="<<run.ai_army_profile_trace[f].best_asset_id[2]
                    <<" army_score_c="<<run.ai_army_profile_trace[f].best_score[2]
                    <<" army_alt_c="<<run.ai_army_profile_trace[f].best_advanced_asset_id[2]
                    <<" army_alt_score_c="<<run.ai_army_profile_trace[f].best_advanced_score[2]
                    <<" tec1="<<thandor::faction_technology_unlocked(run.faction_technology_masks[f],1U)
                    <<" primary_alert="<<run.ai_primary_anchor_cooldown[f]
                    <<" secondary_alert="<<run.ai_anchor_cooldown0[f]
                    <<" cache_state="<<run.ai_candidate_cache_reuse_state[f]
                    <<" cache_count="<<run.ai_candidate_cache_count[f]
                    <<" xenite_cap="<<run.faction_economy[f].xenite_storage_limit_q4.value_or(0U)
                    <<" cap_locked="<<run.faction_economy[f].xenite_storage_limit_locked
                    <<" storage_threshold="<<run.ai_parameters[20U]
                    <<" storage_gap_threshold="<<run.ai_parameters[22U]
                    <<" storage_weight="<<run.ai_parameters[3U]
                    <<" run_research_completed="<<run.metrics.research_jobs_completed
                    <<" research_completed="<<run.metrics.research_jobs_completed
                    <<" research_rebuild="<<run.ai_research_trace[f].last_rebuild_tick
                    <<" research_arm330="<<run.ai_research_trace[f].arms_factory_present
                    <<" research_newest_first="<<run.level_runtime_enabled
                    <<" research_sources="<<run.ai_research_trace[f].source_count
                    <<" research_admitted="<<run.ai_research_trace[f].admitted_candidates
                    <<" research_positive="<<run.ai_research_trace[f].positive_candidates
                    <<" research_cap="<<run.ai_research_trace[f].candidate_cap_reached
                    <<" research_best="<<run.ai_research_trace[f].best_technology_id
                    <<" research_score="<<run.ai_research_trace[f].best_score
                    <<" research_arm_visits="<<run.ai_research_trace[f].compatibility_index_entries_visited
                    <<" research_seed_resolutions="<<run.ai_research_trace[f].potential_seed_resolutions
                    <<" research_score_evaluations="<<run.ai_research_trace[f].potential_score_evaluations
                    <<" research_score_cache_hits="<<run.ai_research_trace[f].potential_score_cache_hits
                    <<" research_purchase_tick="<<run.ai_research_trace[f].last_purchase_tick
                    <<" research_purchase="<<run.ai_research_trace[f].last_purchase_technology_id
                    <<" research_queued="<<run.ai_research_trace[f].last_purchase_queued
                    <<" research_last_complete="<<run.ai_research_trace[f].last_completed_technology_id
                    <<" research_completion_tick="<<run.ai_research_trace[f].last_completion_tick
                    <<" run_units_spawned="<<run.metrics.class13_units_spawned
                    <<" units_spawned="<<run.metrics.class13_units_spawned;
                for(std::size_t word=0;word<8U;++word)
                    stream<<" tech"<<word<<"="<<run.faction_technology_masks[f][word];
                stream<<'\n';
                const auto& research=run.ai_research_trace[f];
                const auto& strategic=run.ai_strategic_construction_trace[f];
                const auto& special=run.ai_special_site_trace[f];
                stream<<"SPECIAL_SITE run="<<ri<<" tick="<<run.tick<<" f="<<f
                    <<" evaluated_tick="<<special.tick<<" ws08="<<special.workspace08_count
                    <<" cluster_tests="<<special.cluster_tests<<" cluster_accepts="<<special.cluster_accepts
                    <<" hostile_blocks="<<special.hostile_blocks<<" probes="<<special.probes_created
                    <<" selected="<<special.selected<<" id="<<special.candidate_id<<" kind="<<special.candidate_kind
                    <<" weight="<<special.weight<<" site="<<special.site_index
                    <<" clearance_q12="<<special.clearance_q12<<" buckets="<<special.bucket_count<<'\n';
                stream<<"STRATEGIC_CONSTRUCTION run="<<ri<<" tick="<<run.tick<<" f="<<f
                    <<" planning_tick="<<strategic.tick<<" has_resource330="<<strategic.has_resource330
                    <<" arms_factories_unlocked="<<strategic.arms_factories_unlocked
                    <<" factory_capacity_available="<<strategic.factory_capacity_available
                    <<" lab_capacity_available="<<strategic.lab_capacity_available
                    <<" selected_factory="<<strategic.selected_factory
                    <<" existing_factory_count="<<strategic.existing_factory_count
                    <<" terrain_free_percent="<<strategic.free_percent[0]<<','<<strategic.free_percent[1]<<','<<strategic.free_percent[2]
                    <<" selected_lab="<<strategic.selected_lab<<'\n';
                const auto& purchase=run.ai_purchase_trace[f];
                stream<<"PURCHASE_PASS run="<<ri<<" tick="<<run.tick<<" f="<<f
                    <<" planning_tick="<<purchase.tick<<" cached="<<purchase.cached
                    <<" initial_budget_q4="<<purchase.initial_budget_q4
                    <<" candidates="<<purchase.candidate_count<<" recorded="<<purchase.recorded_count
                    <<" truncated="<<purchase.truncated<<'\n';
                for (uint32_t rank=0;rank<purchase.recorded_count&&rank<purchase.candidates.size();++rank) {
                    const auto& row=purchase.candidates[rank];
                    stream<<"PURCHASE_CANDIDATE run="<<ri<<" tick="<<run.tick<<" f="<<f
                        <<" planning_tick="<<purchase.tick<<" rank="<<row.rank<<" id="<<row.id
                        <<" kind="<<row.kind<<" score_signed="<<row.packed_score
                        <<" cost_q4="<<row.cost_q4<<" budget_before_q4="<<row.budget_before_q4
                        <<" budget_after_q4="<<row.budget_after_q4
                        <<" decision="<<simulation_lab_purchase_decision_name(row.decision)<<'\n';
                }
                for(uint32_t ci=0;ci<research.candidate_count&&ci<research.candidates.size();++ci){
                    const auto& candidate=research.candidates[ci];
                    stream<<"RESEARCH_CANDIDATE run="<<ri<<" tick="<<run.tick<<" f="<<f
                        <<" rebuild_tick="<<research.last_rebuild_tick<<" tech="<<candidate.technology_id
                        <<" source_arm="<<candidate.source_army_id<<" source_class="<<candidate.source_class
                        <<" score="<<candidate.score<<" score_signed="<<std::bit_cast<int32_t>(candidate.score)
                        <<" score_kind="<<candidate.score_kind<<" category="<<candidate.category
                        <<" source_entity="<<candidate.source_entity_index<<" panel_slot="<<candidate.panel_slot
                        <<" base_score="<<candidate.base_score<<" xenite_cost="<<candidate.xenite_cost_q4
                        <<" energy_demand="<<candidate.energy_demand_q4<<" duration="<<candidate.duration_ticks
                        <<" compatible_mdl="<<candidate.compatible_model_id
                        <<" compatible_class="<<candidate.compatible_model_class
                        <<" compatible_resolved="<<candidate.compatible_model_resolved
                        <<" lookup_complete="<<candidate.compatible_model_lookup_complete<<" reason="<<candidate.reason
                        <<" arm_matches="<<candidate.compatible_arm_matches
                        <<" arm_positive="<<candidate.compatible_arm_positive
                        <<" arm_missing_metadata="<<candidate.compatible_arm_missing_metadata
                        <<" locked_root_matches="<<candidate.locked_root_child_matches
                        <<" best_potential_arm="<<candidate.best_potential_arm_id
                        <<" best_potential_score="<<candidate.best_potential_arm_score<<'\n';
                }

                for(size_t rank=0;rank<std::min<size_t>(run.ai_candidate_cache_count[f],3U);++rank) {
                    const auto& candidate=run.ai_candidate_cache[f][rank];
                    stream<<"CACHED_CANDIDATE run="<<ri<<" tick="<<run.tick
                        <<" faction="<<f<<" rank="<<rank<<" kind="<<candidate.kind
                        <<" id="<<candidate.id<<" score="<<candidate.packed_score<<'\n';
                }
                for(std::size_t si=0;si<ws.resource_site_sample_count;++si){
                    const auto& site=ws.resource_site_samples[si];
                    stream<<"RESOURCE_SITE run="<<ri
                        <<" tick="<<ws.last_planning_tick<<" faction="<<f
                        <<" index="<<si<<" arm="<<site.army_asset_id
                        <<" priority="<<site.priority
                        <<" x="<<site.point.x<<" y="<<site.point.y
                        <<" nearest_constructor_q12="<<site.nearest_constructor_distance_q12
                        <<" reach_only="<<site.in_build_reach
                        <<" candidate_extent_q12="<<site.placement_candidate_extent_q12
                        <<" mode0_radius_with_extent="<<site.mode0_radius_with_extent
                        <<" pioneer_weighted_score="<<site.pioneer_weighted_score
                        <<" secondary_q12="<<site.pioneer_secondary_distance_q12
                        <<" visible_hostile_q12="<<site.pioneer_visible_hostile_distance_q12
                        <<" pioneer_distance_q12="<<site.pioneer_distance_q12
                        <<" assigned_count="<<site.pioneer_assigned_count
                        <<" support_only_inputs_available="<<site.support_only_inputs_available
                        <<" support_only_owner_count="<<site.support_only_owner_count
                        <<" support_only_mode3_reached="<<site.support_only_mode3_reached
                        <<" support_only_mode3_clearance_q12="<<site.support_only_mode3_clearance_q12
                        <<" support_only_mode0_reached="<<site.support_only_mode0_reached
                        <<" support_only_retail_admission_evaluated=0"<<'\n';
                }
            }
        }
        if(detailed){
            for(std::size_t ti=0;ti<run.end_condition_triggers.size();++ti){
                const auto& trigger=run.end_condition_triggers[ti];
                if(!trigger.active()&&!trigger.processed())continue;
                const auto condition=static_cast<std::size_t>(trigger.condition_index);
                const auto kind=condition<run.scheduled_conditions.size()
                    ? static_cast<unsigned>(run.scheduled_conditions[condition].status_and_kind&0xFEU)
                    : 255U;
                stream<<"TRIGGER run="<<ri
                    <<" tick="<<run.tick<<" id="<<ti
                    <<" faction="<<static_cast<unsigned>(trigger.faction_runtime_index)
                    <<" condition="<<condition<<" kind="<<kind
                    <<" active="<<trigger.active()<<" processed="<<trigger.processed()
                    <<" skip_disable="<<static_cast<unsigned>(trigger.skip_army_disable_when_one)
                    <<'\n';
            }
            // This distinguishes true death/conversion from renderer-only disappearance.
            for(const auto& e:run.entities){
                if(e.faction_index==0U||e.faction_index>7U)continue;
                bool fog_visible=false;
                bool fog_explored=false;
                bool persistent_explored=false;
                bool owner_disabled=false;
                bool fog_origin_inside=false;
                bool fog_origin_barrier=false;
                std::uint32_t fog_origin_x=0U, fog_origin_y=0U;
                std::uint32_t self_relation=0U;
                std::uint8_t self_occupancy=0U;
                if(e.faction_index<8U)
                    self_relation=(run.faction_packed_relation_states[e.faction_index]>>
                        (e.faction_index*4U))&0xFU;
                if(e.root_model_component_index<e.model_components.size())
                    owner_disabled=(e.model_components[e.root_model_component_index].runtime_flags_ec&0x10U)!=0U;
                if(field_.width()!=0U){
                    const auto cell=thandor::retail_fog_world_to_cell(field_,
                        thandor::float_to_q12(e.actor.position.x),
                        thandor::float_to_q12(e.actor.position.y));
                    if(cell){
                        fog_origin_inside=true;
                        fog_origin_x=cell->x;fog_origin_y=cell->y;
                        fog_origin_barrier=(field_.at(cell->x,cell->y).flags_and_material&0x88006000U)!=0U;
                        const std::size_t i=static_cast<std::size_t>(cell->y)*field_.width()+cell->x;
                        if(e.faction_index<run.faction_explored_cell_bits.size()){
                            const auto& bits=run.faction_explored_cell_bits[e.faction_index];
                            persistent_explored=(i/64U<bits.size()) &&
                                ((bits[i/64U]>>(i%64U))&1U)!=0U;
                        }
                        if(i<run.current_visibility_occupancy.size()){
                            self_occupancy=thandor::fog_occupancy_byte(
                                run.current_visibility_occupancy[i],e.faction_index);
                            const auto mask=run.current_visibility_occupancy[i];
                            const auto inspect=[&](uint32_t faction){
                                const auto byte=thandor::fog_occupancy_byte(mask,faction);
                                fog_visible|=thandor::retail_fog_currently_visible(byte);
                                fog_explored|=thandor::retail_fog_explored(byte);
                            };
                            if(sample.fog_preview_mode_==1)inspect(static_cast<uint32_t>(sample.fog_preview_faction_));
                            else for(uint32_t faction=1U;faction<=sample.active_factions;++faction)
                                inspect(faction);
                        }
                    }
                }
                for(std::size_t wi=0;wi<e.combat_weapons.size()&&wi<e.actor.weapons.size();++wi){
                    const auto& profile=e.combat_weapons[wi].profile;
                    const auto& state=e.actor.weapons[wi];
                    stream<<"WEAPON run="<<ri<<" tick="<<run.tick<<" entity="<<(&e-run.entities.data())
                        <<" index="<<wi<<" mdl="<<profile.model_definition_id<<" shot="<<profile.shot_definition_id
                        <<" callback_active="<<thandor::simulation_lab_weapon_callback_active(e,profile)
                        <<" profile_valid="<<profile.valid()<<" node_present="<<thandor::simulation_lab_model_node_present(e,profile.model_node_index)
                        <<" aim_node="<<profile.aim_model_node_index<<" yaw_node="<<profile.yaw_model_node_index
                        <<" range="<<state.target_in_range<<" aligned="<<state.target_aligned
                        <<" los="<<state.line_of_fire_clear<<" cooldown="<<state.cooldown_ticks
                        <<" inter_shot="<<state.inter_shot_ticks<<" selector_mask="<<state.launch_mesh_group_mask<<'\n';
                }
                if(e.runtime_model_class_id!=18U){
                    stream<<"ENTITY run="<<ri<<" tick="<<run.tick
                        <<" entity="<<static_cast<std::size_t>(&e-run.entities.data())
                        <<" faction="<<e.faction_index<<" arm="<<e.army_asset_id
                        <<" class="<<e.runtime_model_class_id<<" source="<<e.source_placement_index
                        <<" synthetic="<<e.synthetic<<" building="<<e.building
                        <<" complete="<<e.construction_complete<<" terminal="<<e.root_model_terminal
                        <<" hp="<<e.root_model_integrity<<" x="<<e.actor.position.x
                        <<" y="<<e.actor.position.y<<" z="<<e.actor.position.z
                        <<" runtime_flags="<<e.runtime_flags<<" owner_disabled="<<owner_disabled
                        <<" fog_current="<<fog_visible<<" fog_explored="<<fog_explored
                        <<" military_action="<<e.military_ai_command_kind
                        <<" military_cooldown="<<e.military_ai_busy_cooldown
                        <<" military_path_x_q12="<<e.combat_movement.fallback.x
                        <<" military_path_y_q12="<<e.combat_movement.fallback.y
                        <<" military_path_initialized="<<e.combat_movement_initialized
                        <<" military_score="<<e.military_ai_last_choice.score
                        <<" military_site_score="<<e.military_ai_last_choice.best_site_score
                        <<" military_anchor_score="<<e.military_ai_last_choice.best_anchor_score
                        <<" military_hostile_score="<<e.military_ai_last_choice.best_hostile_score
                        <<" research_id="<<e.research_technology_id<<" research_active="<<e.research_active
                        <<" research_pending="<<e.research_pending_payment
                        <<" research_elapsed="<<e.research_elapsed_ticks<<" research_duration="<<e.research_duration_ticks
                        <<" research_xenite_due="<<e.research_xenite_due_q4<<" research_energy="<<e.research_energy_demand_q4
                        <<" production_asset="<<e.production_asset_id
                        <<" production_state="<<e.production_behavior_state
                        <<" linked_to_parent="<<e.production_linked_to_parent
                        <<" has_factory_exit="<<e.production_exit_world.has_value()
                        <<" root_mdl="<<(e.root_model_component_index<e.model_components.size()
                            ?e.model_components[e.root_model_component_index].model_definition_id:0U)
                        <<" max_hp="<<e.root_model_maximum_integrity
                        <<" speed_q12="<<e.motion.speed_q12_per_tick
                        <<" weapons="<<e.combat_weapons.size()
                        <<" volleys="<<e.weapon_volleys_fired<<" last_fire_tick="<<e.last_weapon_fire_tick
                        <<" translated="<<e.last_translated<<" turned="<<e.last_turned
                        <<" contact_geometry="<<e.articulated_contact_geometry_enabled
                        <<" contacts_initialized="<<e.actor.articulated_contacts_initialized
                        <<" contact_phase="<<e.actor.articulated_contact_progress_q12
                        <<" contact_serial="<<e.actor.articulated_contact_event_serial
                        <<" contact_swing="<<e.actor.articulated_swing_side
                        <<" yaw="<<e.actor.body_yaw_turn16
                        <<" factory_spawn_x="<<e.production_spawn_world.value_or(e.actor.position).x
                        <<" factory_spawn_y="<<e.production_spawn_world.value_or(e.actor.position).y
                        <<" factory_exit_x="<<e.production_exit_world.value_or(e.actor.position).x
                        <<" factory_exit_y="<<e.production_exit_world.value_or(e.actor.position).y
                        <<" factory_drive_x="<<e.production_drive_target_world.value_or(e.production_exit_world.value_or(e.actor.position)).x
                        <<" factory_drive_y="<<e.production_drive_target_world.value_or(e.production_exit_world.value_or(e.actor.position)).y
                        <<" has_move_target="<<e.actor.move_target.has_value()
                        <<" move_target_x="<<e.actor.move_target.value_or(e.actor.position).x
                        <<" move_target_y="<<e.actor.move_target.value_or(e.actor.position).y
                        <<" move_target_z="<<e.actor.move_target.value_or(e.actor.position).z
                        <<" has_waypoint="<<e.actor.move_waypoint.has_value()
                        <<" waypoint_x="<<e.actor.move_waypoint.value_or(e.actor.position).x
                        <<" waypoint_y="<<e.actor.move_waypoint.value_or(e.actor.position).y
                        <<" target_entity="<<e.target_entity_index.value_or(SIZE_MAX)
                        <<" has_target_position="<<e.combat_command_target_position_world.has_value()
                        <<" target_position_x="<<e.combat_command_target_position_world.value_or(e.actor.position).x
                        <<" target_position_y="<<e.combat_command_target_position_world.value_or(e.actor.position).y
                        <<" target_position_z="<<e.combat_command_target_position_world.value_or(e.actor.position).z
                        <<" combat_mode="<<e.combat_command_mode_flags<<" combat_generation="<<e.combat_command_generation
                        <<" combat_assigned_target="<<e.combat_assigned_target_entity_index.value_or(SIZE_MAX)
                        <<" combat_acquisitions="<<e.combat_command_acquisitions<<" combat_retains="<<e.combat_command_retains
                        <<" combat_fallbacks="<<e.combat_command_fallbacks
                        <<" follow_flags="<<e.combat_movement.flags<<" follow_retry="<<e.combat_movement.retry
                        <<" follow_queue="<<e.combat_movement.queue_count<<" follow_owned="<<e.combat_follow_owned
                        <<" follow_starts="<<e.combat_follow_starts<<" follow_stops="<<e.combat_follow_stops
                        <<" follow_blocked="<<e.combat_follow_blocked_callbacks<<" follow_solves="<<e.combat_follow_route_resolves
                        <<" follow_failures="<<e.combat_follow_route_failures
                        <<" contact_active="<<e.actor.articulated_contact_transition_active
                        <<" contact_rate="<<e.actor.articulated_contact_step_q12
                        <<" contact_target_dx="<<(e.actor.articulated_foot_target_world[e.actor.articulated_swing_side&1U].x-e.actor.position.x)
                        <<" contact_target_dy="<<(e.actor.articulated_foot_target_world[e.actor.articulated_swing_side&1U].y-e.actor.position.y)
                        <<" route_direct="<<e.navigation_direct_route_valid
                        <<" route_cell_valid="<<e.navigation_target_cell_valid
                        <<" route_recheck="<<e.navigation_recheck_tick
                        <<" aim_in_range="<<e.actor.target_in_range
                        <<" aim_aligned="<<e.actor.target_aligned
                        <<" weapon_los_blocked="<<std::count_if(e.actor.weapons.begin(),e.actor.weapons.end(),[](const auto& w){return w.target_in_range&&!w.line_of_fire_clear;})
                        <<" removed="<<e.runtime_removed
                        <<" energy_total_q4="<<thandor::retail_selection_metrics(e).energy_total_q4
                        <<" energy_active_q4="<<thandor::retail_selection_metrics(e).energy_active_q4<<'\n';
                write_walker_contact_diagnostics(stream,ri,run.tick,static_cast<std::size_t>(&e-run.entities.data()),e);
                    continue;
                }
                stream<<"ENTITY run="<<ri<<" tick="<<run.tick
                    <<" entity="<<static_cast<std::size_t>(&e-run.entities.data())
                    <<" faction="<<e.faction_index<<" arm="<<e.army_asset_id
                    <<" class="<<e.runtime_model_class_id
                    <<" source="<<e.source_placement_index
                    <<" synthetic="<<e.synthetic<<" building="<<e.building
                    <<" complete="<<e.construction_complete
                    <<" terminal="<<e.root_model_terminal
                    <<" hp="<<e.root_model_integrity
                    <<" radius="<<e.fog_uncover_radius_q12
                    <<" x="<<e.actor.position.x<<" y="<<e.actor.position.y
                    <<" z="<<e.actor.position.z
                    <<" conversion_due="<<e.class18_conversion_effect_complete_tick
                    <<" fog_current="<<fog_visible<<" fog_explored="<<fog_explored
                    <<" fog_persistent_explored="<<persistent_explored
                    <<" fog_origin_inside="<<fog_origin_inside
                    <<" fog_origin_x="<<fog_origin_x<<" fog_origin_y="<<fog_origin_y
                    <<" fog_origin_barrier="<<fog_origin_barrier
                    <<" owner_disabled="<<owner_disabled
                    <<" self_relation="<<self_relation
                    <<" self_occupancy="<<static_cast<unsigned>(self_occupancy)
                    <<" build_reach_q12="<<e.runtime_class_specific_19c_raw
                    <<" preset_present="<<(preset_ids_.contains(e.army_asset_id))
                    <<" runtime_flags="<<e.runtime_flags
                    <<" motion_speed_q12="<<e.motion.speed_q12_per_tick
                    <<" pioneer_hold_bit="<<((e.runtime_flags&0x08U)!=0U)
                    <<" pioneer_hold_without_due="<<((e.runtime_flags&0x08U)!=0U && e.class18_conversion_effect_complete_tick==0U)
                    <<" pioneer_runtime_steps="<<e.pioneer_runtime_step_count
                    <<" pioneer_last_step_tick="<<e.pioneer_runtime_last_step_tick
                    <<" pioneer_hold_skips="<<e.pioneer_hold_skipped_ticks
                    <<" pioneer_immobile_skips="<<e.pioneer_immobile_skipped_ticks
                    <<" pioneer_last_gate_action="<<e.pioneer_last_gate_action
                    <<" pioneer_gate_calls="<<e.pioneer_gate_trace.calls
                    <<" pioneer_gate_scheduled="<<e.pioneer_gate_trace.scheduled_calls
                    <<" pioneer_gate_special="<<e.pioneer_gate_trace.special_flag_calls
                    <<" pioneer_gate_updated="<<e.pioneer_gate_trace.movement_update_calls
                    <<" pioneer_gate_carry="<<e.pioneer_gate_trace.movement_carry_calls
                    <<" pioneer_gate_prevented_update="<<e.pioneer_gate_trace.special_flag_prevented_update
                    <<" pioneer_gate_reset_kept_special="<<e.pioneer_gate_trace.reset_preserved_special_flag
                    <<" pioneer_gate_ws_equal="<<e.pioneer_gate_trace.workspace_equal_calls
                    <<" pioneer_gate_ws_unequal="<<e.pioneer_gate_trace.workspace_unequal_calls
                    <<" pioneer_gate_ws_unequal_updated="<<e.pioneer_gate_trace.unequal_with_update_calls
                    <<" pioneer_gate_ws_unequal_carry="<<e.pioneer_gate_trace.unequal_carry_calls
                    <<" pioneer_gate_special_unequal_sites="<<e.pioneer_gate_trace.special_with_unequal_nonempty_sites
                    <<" pioneer_gate_reset_ws_equal="<<e.pioneer_gate_trace.reset_with_equal_workspaces
                    <<" pioneer_gate_no_command="<<e.pioneer_gate_trace.actions[0]
                    <<" pioneer_gate_bootstrap="<<e.pioneer_gate_trace.actions[1]
                    <<" pioneer_gate_score_sites="<<e.pioneer_gate_trace.actions[2]
                    <<" pioneer_gate_reset="<<e.pioneer_gate_trace.actions[3]
                    <<" pioneer_gate_await="<<e.pioneer_gate_trace.actions[4]
                    <<" pioneer_gate_last_tick="<<e.pioneer_gate_trace.last_tick
                    <<" pioneer_gate_last_entry_flags="<<e.pioneer_gate_trace.last_entry_flags
                    <<" pioneer_gate_last_update_flags="<<e.pioneer_gate_trace.last_flags_after_update
                    <<" pioneer_gate_last_gate_flags="<<e.pioneer_gate_trace.last_gate_flags
                    <<" pioneer_gate_last_ws00="<<e.pioneer_gate_trace.last_workspace00
                    <<" pioneer_gate_last_ws04="<<e.pioneer_gate_trace.last_workspace04
                    <<" pioneer_gate_last_ws08="<<e.pioneer_gate_trace.last_workspace08
                    <<" pioneer_gate_last_scheduled="<<e.pioneer_gate_trace.last_scheduled
                    <<" pioneer_gate_last_carry="<<e.pioneer_gate_trace.last_movement_carry
                    <<" pioneer_gate_last_action="<<e.pioneer_gate_trace.last_action
                    <<" pioneer_selector_calls="<<e.pioneer_site_selector.calls
                    <<" pioneer_selector_last_tick="<<e.pioneer_site_selector.last_tick
                    <<" pioneer_selector_candidates="<<e.pioneer_site_selector.last_candidates
                    <<" pioneer_selector_evaluated="<<e.pioneer_site_selector.last_scores_evaluated
                    <<" pioneer_selector_positive="<<e.pioneer_site_selector.last_positive_scores
                    <<" pioneer_selector_best_score="<<e.pioneer_site_selector.last_best_score
                    <<" pioneer_selector_best_x_q12="<<e.pioneer_site_selector.last_best_point.x
                    <<" pioneer_selector_best_y_q12="<<e.pioneer_site_selector.last_best_point.y
                    <<" pioneer_selector_best_exists="<<e.pioneer_site_selector.last_best_exists
                    <<" pioneer_selector_empty_calls="<<e.pioneer_site_selector.empty_workspace_calls
                    <<" pioneer_selector_zero_positive_calls="<<e.pioneer_site_selector.zero_positive_score_calls
                    <<" pioneer_selector_unseen_distance_rejects="<<e.pioneer_site_selector.last_unseen_distance_rejections
                    <<" pioneer_selector_visible_distance_rejects="<<e.pioneer_site_selector.last_visible_distance_rejections
                    <<" pioneer_selector_placement_skips="<<e.pioneer_site_selector.last_placement_skips
                    <<" pioneer_selector_stops="<<e.pioneer_site_selector.last_selector_stops
                    <<" pioneer_selector_admitted_scores="<<e.pioneer_site_selector.last_admitted_for_scoring
                    <<" pioneer_selector_arm333_probes="<<e.pioneer_site_selector.last_temporary_arm333_probes
                    <<" pioneer_selector_retail_admission_evaluated="<<e.pioneer_site_selector.retail_admission_evaluated
                    <<" pioneer_route_rejections="<<e.pioneer_route_rejection_count
                    <<" pioneer_last_rejected_x_q12="<<e.pioneer_last_rejected_target_x_q12
                    <<" pioneer_last_rejected_y_q12="<<e.pioneer_last_rejected_target_y_q12
                    <<" pioneer_ai_cooldown="<<e.pioneer_ai_command_cooldown_ticks
                    <<" pioneer_ai_busy_skipped="<<e.pioneer_ai_busy_skipped_calls
                    <<" pioneer_ai_commands="<<e.pioneer_ai_command_count
                    <<" pioneer_last_command_tick="<<e.pioneer_ai_last_command_tick
                    <<" pioneer_movement_flags="<<e.pioneer_movement.flags
                    <<" pioneer_retry="<<e.pioneer_movement.retry
                    <<" pioneer_primary_x_q12="<<e.pioneer_movement.primary.x
                    <<" pioneer_primary_y_q12="<<e.pioneer_movement.primary.y
                    <<" pioneer_fallback_x_q12="<<e.pioneer_movement.fallback.x
                    <<" pioneer_fallback_y_q12="<<e.pioneer_movement.fallback.y
                    <<" pioneer_stored_x_q12="<<e.pioneer_movement.stored.x
                    <<" pioneer_stored_y_q12="<<e.pioneer_movement.stored.y
                    <<" pioneer_last_checked_x_q12="<<e.pioneer_movement.last_checked.x
                    <<" pioneer_last_checked_y_q12="<<e.pioneer_movement.last_checked.y
                    <<" pioneer_has_traversal_profile="<<e.has_traversal_profile
                    <<" research_id="<<e.research_technology_id
                    <<" research_active="<<e.research_active
                    <<" research_pending="<<e.research_pending_payment
                    <<" research_elapsed="<<e.research_elapsed_ticks
                    <<" research_duration="<<e.research_duration_ticks
                    <<" research_xenite_due="<<e.research_xenite_due_q4
                    <<" research_energy="<<e.research_energy_demand_q4
                    <<" production_asset="<<e.production_asset_id
                    <<" production_state="<<e.production_behavior_state
                    <<" linked_to_parent="<<e.production_linked_to_parent
                    <<" has_factory_exit="<<e.production_exit_world.has_value()
                        <<" root_mdl="<<(e.root_model_component_index<e.model_components.size()
                            ?e.model_components[e.root_model_component_index].model_definition_id:0U)
                        <<" max_hp="<<e.root_model_maximum_integrity
                        <<" speed_q12="<<e.motion.speed_q12_per_tick
                        <<" weapons="<<e.combat_weapons.size()
                        <<" volleys="<<e.weapon_volleys_fired<<" last_fire_tick="<<e.last_weapon_fire_tick
                        <<" translated="<<e.last_translated<<" turned="<<e.last_turned
                        <<" contact_geometry="<<e.articulated_contact_geometry_enabled
                        <<" contacts_initialized="<<e.actor.articulated_contacts_initialized
                        <<" contact_phase="<<e.actor.articulated_contact_progress_q12
                        <<" contact_serial="<<e.actor.articulated_contact_event_serial
                        <<" contact_swing="<<e.actor.articulated_swing_side
                        <<" yaw="<<e.actor.body_yaw_turn16
                        <<" factory_spawn_x="<<e.production_spawn_world.value_or(e.actor.position).x
                        <<" factory_spawn_y="<<e.production_spawn_world.value_or(e.actor.position).y
                        <<" factory_exit_x="<<e.production_exit_world.value_or(e.actor.position).x
                        <<" factory_exit_y="<<e.production_exit_world.value_or(e.actor.position).y
                        <<" factory_drive_x="<<e.production_drive_target_world.value_or(e.production_exit_world.value_or(e.actor.position)).x
                        <<" factory_drive_y="<<e.production_drive_target_world.value_or(e.production_exit_world.value_or(e.actor.position)).y
                    <<" has_move_target="<<e.actor.move_target.has_value()
                        <<" move_target_x="<<e.actor.move_target.value_or(e.actor.position).x
                        <<" move_target_y="<<e.actor.move_target.value_or(e.actor.position).y
                        <<" move_target_z="<<e.actor.move_target.value_or(e.actor.position).z
                        <<" has_waypoint="<<e.actor.move_waypoint.has_value()
                        <<" waypoint_x="<<e.actor.move_waypoint.value_or(e.actor.position).x
                        <<" waypoint_y="<<e.actor.move_waypoint.value_or(e.actor.position).y
                        <<" target_entity="<<e.target_entity_index.value_or(SIZE_MAX)
                        <<" has_target_position="<<e.combat_command_target_position_world.has_value()
                        <<" target_position_x="<<e.combat_command_target_position_world.value_or(e.actor.position).x
                        <<" target_position_y="<<e.combat_command_target_position_world.value_or(e.actor.position).y
                        <<" target_position_z="<<e.combat_command_target_position_world.value_or(e.actor.position).z
                        <<" combat_mode="<<e.combat_command_mode_flags<<" combat_generation="<<e.combat_command_generation
                        <<" combat_assigned_target="<<e.combat_assigned_target_entity_index.value_or(SIZE_MAX)
                        <<" combat_acquisitions="<<e.combat_command_acquisitions<<" combat_retains="<<e.combat_command_retains
                        <<" combat_fallbacks="<<e.combat_command_fallbacks
                        <<" follow_flags="<<e.combat_movement.flags<<" follow_retry="<<e.combat_movement.retry
                        <<" follow_queue="<<e.combat_movement.queue_count<<" follow_owned="<<e.combat_follow_owned
                        <<" follow_starts="<<e.combat_follow_starts<<" follow_stops="<<e.combat_follow_stops
                        <<" follow_blocked="<<e.combat_follow_blocked_callbacks<<" follow_solves="<<e.combat_follow_route_resolves
                        <<" follow_failures="<<e.combat_follow_route_failures
                        <<" contact_active="<<e.actor.articulated_contact_transition_active
                        <<" contact_rate="<<e.actor.articulated_contact_step_q12
                        <<" contact_target_dx="<<(e.actor.articulated_foot_target_world[e.actor.articulated_swing_side&1U].x-e.actor.position.x)
                        <<" contact_target_dy="<<(e.actor.articulated_foot_target_world[e.actor.articulated_swing_side&1U].y-e.actor.position.y)
                        <<" route_direct="<<e.navigation_direct_route_valid
                        <<" route_cell_valid="<<e.navigation_target_cell_valid
                        <<" route_recheck="<<e.navigation_recheck_tick
                        <<" aim_in_range="<<e.actor.target_in_range
                        <<" aim_aligned="<<e.actor.target_aligned
                        <<" weapon_los_blocked="<<std::count_if(e.actor.weapons.begin(),e.actor.weapons.end(),[](const auto& w){return w.target_in_range&&!w.line_of_fire_clear;})
                        <<" removed="<<e.runtime_removed
                        <<" energy_total_q4="<<thandor::retail_selection_metrics(e).energy_total_q4
                        <<" energy_active_q4="<<thandor::retail_selection_metrics(e).energy_active_q4<<'\n';
                write_walker_contact_diagnostics(stream,ri,run.tick,static_cast<std::size_t>(&e-run.entities.data()),e);
            }
        }
        prev.tick=run.tick;
        prev.fingerprint=fingerprint;
        if(detailed)prev.last_trace_tick=run.tick;
    }

    }
    thandor::FieldGrid field_;
    std::unordered_set<uint32_t> preset_ids_;
    struct State {
        uint32_t tick{UINT32_MAX},last_sample_tick{UINT32_MAX};
        uint64_t fingerprint{};
        uint32_t last_trace_tick{};
        uint64_t last_military_order_trace_sequence{};
    };
    std::vector<State> simulation_lab_diagnostic_states_;
    bool simulation_lab_diagnostic_limit_reached_{};
    uint64_t simulation_lab_log_samples_{};
    // Last member: joined before formatter state or immutable field can be destroyed.
    thandor::AsyncDiagnosticLog log_;
};
} // namespace editor
