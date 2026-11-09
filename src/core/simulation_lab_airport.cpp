#include "thandor/simulation_lab_army_pool.hpp"
#include "thandor/simulation_lab.hpp"
#include "thandor/simulation_lab_environment.hpp"
#include "thandor/simulation_lab_geometry.hpp"
#include "thandor/simulation_lab_runtime.hpp"
#include "thandor/simulation_lab_shot_rays.hpp"
#include "thandor/simulation_lab_targeting.hpp"
#include "thandor/fog_preview.hpp"
#include <algorithm>
#include <bit>
namespace thandor {
namespace {
int32_t wrap(uint32_t v) { return std::bit_cast<int32_t>(v); }
int32_t add(int32_t a, int32_t b) { return wrap(uint32_t(a) + uint32_t(b)); }
int32_t mult(int32_t a, int32_t b) {
  return wrap(uint32_t(uint64_t(uint32_t(a)) * uint32_t(b)));
}
int32_t qmul(int32_t a, int32_t b) {
  return wrap(uint32_t((int64_t(a) * b) >> 12));
}
void initialize_pad(SimulationLabEntity &e) {
  if (e.airport_initialized) {
    e.airport.storage_capacity = std::min<uint32_t>(
        e.production_storage_capacity, kSimulationClass22StoredSlotCount);
    return;
  }
  e.airport.stored_army_asset_ids = e.production_linked_child_asset_ids;
  e.airport.occupied_slot_mask = uint16_t(e.production_linked_child_slot_mask);
  simulation_class22_configure_storage(e.airport, e.production_storage_capacity,
                                       e.airport_platform_baseline_q12);
  e.airport_initialized = true;
}
void sync_pad(SimulationLabEntity &e) {
  e.production_linked_child_asset_ids = e.airport.stored_army_asset_ids;
  e.production_linked_child_slot_mask = e.airport.occupied_slot_mask;
  e.production_linked_child_count = e.airport.stored_slot_count;
  e.production_door_texture_offset_v =
      uint32_t(e.airport.primary_texture_offset_v);
}
void journal(SimulationLabRun &run, const SimulationLabEntity &e,
             uint32_t sound, uint32_t effect = 0) {
  if (!sound && !effect)
    return;
  SimulationLabOneShotEvent ev;
  ev.sequence = run.next_one_shot_event_sequence++;
  ev.tick = run.tick;
  ev.kind = effect ? SimulationLabOneShotEventKind::ModelTimedEffect
                   : SimulationLabOneShotEventKind::PositionedEffectSound;
  ev.source_placement_index = e.source_placement_index;
  ev.army_asset_id = e.army_asset_id;
  ev.faction_index = e.faction_index;
  ev.root_world_position = e.actor.position;
  ev.actor = e.actor;
  ev.sound_asset_id = sound;
  ev.effect_id = effect;
  ev.sound_gain_and_distance = std::array{
      e.positioned_sound_gain_q15, e.positioned_sound_maximum_distance_q12};
  run.one_shot_event_tail =
      std::make_shared<const SimulationLabOneShotEventNode>(
          std::move(ev),
                                        run.one_shot_event_tail);
}
void mapped_terrain_sound(SimulationLabRun& run,const SimulationLabEntity& e,
                          const FieldGrid& field) {
  const auto active=run.active_view_faction_index;
  if(active>=8U || e.faction_index>=8U || !e.runtime_model_hierarchy)return;
  const auto& nodes=*e.runtime_model_hierarchy;
  const auto root=std::find_if(nodes.begin(),nodes.end(),[](const auto& n){
    return n.parent_index==UINT32_MAX;
  });
  if(root==nodes.end())return;
  const auto sound=root->simulation.runtime_class_effect_sound_asset_id_26c;
  if(!sound || std::find(run.mapped_terrain_sound_asset_ids.begin(),
       run.mapped_terrain_sound_asset_ids.end(),sound)==run.mapped_terrain_sound_asset_ids.end())return;
  const auto cell=retail_nearest_world_to_cell(field,float_to_q12(e.actor.position.x),
                                               float_to_q12(e.actor.position.y));
  if(!cell)return;
  const auto index=size_t(cell->y)*field.width()+cell->x;
  if(index>=run.current_visibility_occupancy.size())return;
  // Native TestCapabilityBitClear(active, source) reads the SOURCE relation row.
  const bool hostile=run.level_runtime_enabled
    ? ((run.faction_packed_relation_states[e.faction_index]>>(active*4U))&15U)<4U
    : e.faction_index!=0U && active!=0U && e.faction_index!=active;
  if(!simulation_class21_mapped_terrain_sound_gate(e.faction_index,active,hostile,
       fog_occupancy_byte(run.current_visibility_occupancy[index],active),
       run.faction_relation_transition_ticks[active]))return;
  SimulationLabOneShotEvent ev;
  ev.sequence=run.next_one_shot_event_sequence++;
  ev.tick=run.tick;
  ev.kind=SimulationLabOneShotEventKind::GlobalMappedTerrainSound;
  ev.source_placement_index=e.source_placement_index;
  ev.army_asset_id=e.army_asset_id;
  ev.faction_index=e.faction_index;
  ev.actor=e.actor;
  ev.root_world_position=e.actor.position;
  ev.sound_asset_id=sound;
  ev.sound_listener_faction_index=active;
  run.one_shot_event_tail=std::make_shared<const SimulationLabOneShotEventNode>(
      std::move(ev),run.one_shot_event_tail);
  run.faction_relation_transition_ticks[active]=0U;
}
} // namespace
bool SimulationLabExperiment::queue_airport_launch(size_t run_index,
                                                   size_t pad_index,
                                                   uint32_t lanes,
                                                   uint32_t heading, int32_t x,
                                                   int32_t y) {
  if (run_index >= runs_.size() ||
      pad_index >= runs_[run_index].entities.size())
    return false;
  auto &e = runs_[run_index].entities[pad_index];
  if (e.runtime_removed || e.runtime_model_class_id != 22)
    return false;
  initialize_pad(e);
  const auto cmd = simulation_class22_world_launch_command(heading, x, y);
  simulation_class22_queue_launch_mask(e.airport, lanes, cmd.value_a,
                                       cmd.value_b, cmd.value_c);
  return true;
}
void SimulationLabExperiment::step_airports(SimulationLabRun &run,
                                            const FieldGrid &field,
                                            std::optional<size_t> owner) {
  const auto count = run.entities.size();
  const uint32_t batch = std::max(1U, config_.runtime_callback_step_ticks);
  for (size_t visit = 0; visit < (owner ? 1U : count); ++visit) {
    const auto index = owner.value_or(count - 1 - visit);
    auto &e = run.entities[index];
    if (e.runtime_removed) {
      if (e.runtime_model_class_id == 21 && e.aircraft.initialized &&
          e.production_parent_entity_index &&
          *e.production_parent_entity_index < run.entities.size()) {
        auto &pad = run.entities[*e.production_parent_entity_index];
        simulation_class22_release_linked_child_slot(pad.airport,
                                                     e.aircraft.slot);
        sync_pad(pad);
      }
      continue;
    }
    if (e.runtime_model_class_id == 22) {
      initialize_pad(e);
      auto &a = e.airport;
      const auto flags =
          e.runtime_flags |
          (e.root_model_component_index < e.model_components.size()
               ? e.model_components[e.root_model_component_index]
                     .runtime_flags_ec
               : 0U);
      const auto phase = a.dispatch_phase;
      uint32_t first = 0, second = 0;
      if (runtime_catalog_ && e.runtime_model_hierarchy) {
        for (const auto &n : *e.runtime_model_hierarchy)
          if (n.parent_index == UINT32_MAX) {
            first = n.simulation.runtime_class_effect_sound_asset_id_26c;
            second = n.simulation.creation_sound_asset_id_270;
            break;
          }
      }
      if (flags & 0x10U) {
        for (auto &child : run.entities)
          if (child.runtime_model_class_id == 21 &&
              child.production_parent_entity_index == index) {
            child.runtime_flags |= 0x10U;
            child.root_model_terminal = true;
          }
      }
      switch (phase) {
      case SimulationClass22DispatchPhase::Opening:
        if (simulation_class22_tick_open(a, e.production_door_step_q12, batch))
          journal(run, e, second);
        break;
      case SimulationClass22DispatchPhase::RaisingPlatform:
        simulation_class22_tick_raise_platform(a, e.airport_platform_step_q12,
                                               e.airport_platform_limit_q12,
                                               batch);
        break;
      case SimulationClass22DispatchPhase::LoweringPlatform:
        if (simulation_class22_tick_lower_platform(
                a, e.airport_platform_step_q12, e.airport_platform_baseline_q12,
                batch))
          journal(run, e, first);
        break;
      case SimulationClass22DispatchPhase::Closing:
        simulation_class22_tick_close(a, e.production_door_step_q12, batch);
        break;
      case SimulationClass22DispatchPhase::Reset:
        a.dispatch_phase = SimulationClass22DispatchPhase::Idle;
        [[fallthrough]];
      case SimulationClass22DispatchPhase::Idle: {
        if (flags & 9U)
          break;
        // Exactly one attempt per nonempty lane/callback. Failed lane 0 may try
        // lane 1, but must not drain all of lane 0's count in one callback.
        bool spawned = false;
        for (size_t lane = 0; lane < 3 && !spawned; ++lane) {
          if (!a.pending_launch_counts[lane])
            continue;
          --a.pending_launch_counts[lane];
          const auto id = kSimulationClass22LinkedArmyAssetIds[lane];
          const auto slot = simulation_class22_find_free_stored_slot(a, id);
          const auto *product = production_asset(id);
          if (!slot || !product ||
              product->entity_seed.runtime_model_class_id != 21)
            continue;
          grant_creation_technologies(run, e.faction_index,
                                      product->entity_seed);
          auto child = make_entity_from_seed(
              product->entity_seed, e.faction_index, e.actor.position, true,
              run.faction_technology_masks[e.faction_index]);
          const auto cmd = a.launch_commands[lane];
          if (!simulation_class22_mark_linked_child_spawned(a, *slot))
            continue;
          child.production_parent_entity_index = index;
          child.production_linked_to_parent = true;
          child.production_parked_on_pad = true;
          child.actor.body_yaw_turn16 = e.actor.body_yaw_turn16;
          child.actor.move_target.reset();
          child.construction_start_tick = child.construction_complete_tick =
              run.tick;
          child.aircraft.initialized = true;
          child.aircraft.slot = uint32_t(*slot);
          child.aircraft.mission_heading = uint32_t(cmd.value_a) & 65535;
          child.aircraft.mission_x = cmd.value_b;
          child.aircraft.mission_y = cmd.value_c;
          child.aircraft.platform_height = child.aircraft.body_height =
              a.platform_local_z_q12;
          if (e.root_model_maximum_integrity &&
              child.root_model_component_index <
                  child.model_components.size()) {
            auto &root =
                child.model_components[child.root_model_component_index];
            root.integrity = wrap(uint32_t(int64_t(e.root_model_integrity) *
                                           root.maximum_integrity /
                                           e.root_model_maximum_integrity));
            child.root_model_integrity = root.integrity;
          }
          sync_pad(e);
          journal(run, e, first);
          ++run.metrics.class22_aircraft_spawned;
          claim_model_smoke_slots(run, child);
          run.entities.push_back(std::move(child));
          spawned = true;
        }
        if (spawned)
          continue;
        break;
      }
      default:
        break;
      }
      sync_pad(e);
      continue;
    }
    if (e.runtime_model_class_id != 21 || !e.aircraft.initialized)
      continue;
    auto &f = e.aircraft;
    const auto d = e.aircraft_definition;
    auto *parent =
        e.production_parent_entity_index &&
                *e.production_parent_entity_index < run.entities.size()
            ? &run.entities[*e.production_parent_entity_index]
            : nullptr;
    if (parent && parent->runtime_removed)
      parent = nullptr;
    const auto retire = [&]() {
      if (parent) {
        simulation_class22_release_linked_child_slot(parent->airport, f.slot);
        sync_pad(*parent);
      }
      if (runtime_catalog_ && e.runtime_model_hierarchy)
        for (const auto &n : *e.runtime_model_hierarchy)
          if (n.parent_index == UINT32_MAX) {
            journal(run, e, 0, n.simulation.construction_destruction_effect_id);
            break;
          }
      simulation_lab_release_army_slot(run,e);
      e.runtime_removed = true;
      e.production_linked_to_parent = false;
    };
    const auto offmap = [&]() {
      e.actor.position.x = -256.F;
      e.actor.position.y = 256.F;
      f.offmap = true;
    };
    const auto advance = [&](int32_t distance) {
      const auto delta =
          retail_direction_scaled(0, e.actor.body_yaw_turn16, distance);
      e.actor.position.x =
          q12_to_float(add(float_to_q12(e.actor.position.x), delta[0]));
      e.actor.position.y =
          q12_to_float(add(float_to_q12(e.actor.position.y), delta[1]));
    };
    const auto arc = [&](int32_t coefficient, int32_t reference) {
      const auto square = qmul(f.arc_position, f.arc_position);
      f.body_height =
          add(qmul(mult(coefficient, int32_t(batch)), square), reference);
      f.body_pitch = simulation_class21_flight_pitch_turn16(f.arc_position,
                                                            coefficient, batch);
      f.arc_position = add(f.arc_position, mult(d.speed_q12, int32_t(batch)));
    };
    // Dispatch from the entry state once; never execute a newly selected phase
    // in this callback.
    switch (f.runtime.phase) {
    case SimulationClass21LinkedChildPhase::Attached:
      if (!parent ||
          parent->airport.dispatch_phase ==
              SimulationClass22DispatchPhase::WaitingForChildRelease) {
        f.arc_position = 0;
        f.runtime.phase = SimulationClass21LinkedChildPhase::OutboundRelease;
        f.runtime.release_countdown = d.release_ticks;
        f.runtime.outbound_countdown = d.pad_ticks;
        e.production_parked_on_pad = false;
      } else if (parent->airport.dispatch_phase ==
                     SimulationClass22DispatchPhase::Idle ||
                 parent->airport.dispatch_phase ==
                     SimulationClass22DispatchPhase::Reset) {
        retire();
      } else
        f.body_height = f.platform_height =
            parent->airport.platform_local_z_q12;
      break;
    case SimulationClass21LinkedChildPhase::OutboundRelease:
      arc(d.takeoff_arc_q12, f.platform_height);
      advance(mult(d.speed_q12, int32_t(batch)));
      if (simulation_class21_tick_outbound_release(f.runtime, batch) &&
          parent) {
        parent->airport.dispatch_phase =
            SimulationClass22DispatchPhase::LoweringPlatform;
        journal(run, *parent,
                parent->runtime_model_hierarchy
                    ? parent->runtime_model_hierarchy->front()
                          .simulation.creation_sound_asset_id_270
                    : 0);
      }
      if (f.runtime.phase == SimulationClass21LinkedChildPhase::OffmapOutbound)
        offmap();
      break;
    case SimulationClass21LinkedChildPhase::OffmapOutbound: {
      const auto delta = retail_direction_scaled(
          0, f.mission_heading ^ 0x8000U, mult(d.speed_q12, d.travel_ticks));
      e.actor.position = {q12_to_float(add(f.mission_x, delta[0])),
                          q12_to_float(add(f.mission_y, delta[1])), 0};
      e.actor.body_yaw_turn16 = f.mission_heading;
      f.arc_position = wrap(0U - uint32_t(mult(d.speed_q12, d.travel_ticks)));
      int32_t maximum = 0;
      auto x = float_to_q12(e.actor.position.x),
           y = float_to_q12(e.actor.position.y);
      const auto step =
          retail_direction_scaled(0, f.mission_heading, mult(d.speed_q12, 10));
      // Native count is signed and decremented by 10, including the initial
      // sample.
      for (int64_t remaining = int64_t(d.travel_ticks) * 2; remaining >= 0;
           remaining -= 10) {
        const auto surface = field.sample_retail_surface_heights_q12(x, y);
        if (surface.valid)
          maximum = std::max(maximum, surface.top);
        x = add(x, step[0]);
        y = add(y, step[1]);
      }
      f.reference_height = add(add(maximum, 0x800), mult(f.platform_height, 2));
      simulation_class21_begin_mission_pass(f.runtime, mult(d.travel_ticks, 2));
      f.offmap = false;
      break;
    }
    case SimulationClass21LinkedChildPhase::MissionPass: {
      advance(mult(d.speed_q12, int32_t(batch)));
      const auto surface = field.sample_retail_surface_heights_q12(
          float_to_q12(e.actor.position.x), float_to_q12(e.actor.position.y));
      arc(d.mission_arc_q12, add(f.reference_height, -surface.top));
      // Each substep decrements the run counter BEFORE its sound call. Until it
      // goes negative all attempts share this point; afterwards native uses the
      // off-map sentinel. No economy tick runs between these attempts, so only
      // the first eligible one can play/reset the faction cooldown.
      const int32_t countdown_before=f.runtime.mission_countdown;
      const auto events = simulation_class21_tick_mission_pass_events(
          f.runtime, d.model_point_step, batch);
      // The native state-5 loop decrements before attempting the mapped sound,
      // including the substep crossing below zero. Off-map calls use the
      // sentinel position; the sound gate rejects that position as needed.
      for (uint32_t attempt=0;attempt<events.mapped_terrain_sound_attempts;++attempt) {
        if (int64_t(countdown_before)-int64_t(attempt)-1LL<0LL) offmap();
        mapped_terrain_sound(run,e,field);
      }
      if (events.completed) offmap();
      if (runtime_catalog_ && events.emission_count &&
          e.runtime_model_hierarchy) {
        const auto poses =
            simulation_lab_current_model_poses(e, field, *runtime_catalog_);
        for (size_t node = 0; node < e.runtime_model_hierarchy->size();
             ++node) {
          const auto &n = (*e.runtime_model_hierarchy)[node];
          if (n.parent_index == UINT32_MAX || n.child_slot != 0 ||
              node >= poses.size())
            continue;
          if ((*e.runtime_model_hierarchy)[n.parent_index].parent_index !=
              UINT32_MAX)
            continue;
          const auto sprite = runtime_catalog_->sprites.find(n.sprite_path);
          if (sprite == runtime_catalog_->sprites.end())
            break;
          const auto root =
              std::find_if(e.runtime_model_hierarchy->begin(),
                           e.runtime_model_hierarchy->end(), [](const auto &n) {
                             return n.parent_index == UINT32_MAX;
                           });
          if (root == e.runtime_model_hierarchy->end())
            break;
          const auto shot =
              runtime_catalog_->shots.find(root->simulation.shot_definition_id);
          if (shot == runtime_catalog_->shots.end())
            break;
          for (uint32_t mark = 0; mark < events.emission_count; ++mark) {
            const auto p = std::find_if(
                sprite->second.shot_points.begin(),
                sprite->second.shot_points.end(), [&](const auto &p) {
                  return p.first == events.model_point_ordinals[mark];
                });
            if (p == sprite->second.shot_points.end())
              continue;
            const auto &pose = poses[node];
            const auto l = p->second;
            const Vec3f launch{pose.position.x + pose.basis[0] * l.x +
                                   pose.basis[1] * l.y + pose.basis[2] * l.z,
                               pose.position.y + pose.basis[3] * l.x +
                                   pose.basis[4] * l.y + pose.basis[5] * l.z,
                               pose.position.z + pose.basis[6] * l.x +
                                   pose.basis[7] * l.y + pose.basis[8] * l.z};
            Vec3f target = launch;
            target.z = pose.position.z - 2.F;
            // Indexed aircraft shots search newest-first and overwrite source
            // range with each candidate's footprint. They are not ordinary
            // root-weapon attacks.
            uint32_t best_score = 0;
            std::optional<size_t> best;
            const auto source_metrics = simulation_lab_combat_metrics(e);
            std::optional<std::vector<SimulationLabLiveShotHull>> hulls;
            for (size_t candidate_visit = 0;
                 candidate_visit < run.entities.size(); ++candidate_visit) {
              const auto candidate_index =
                  run.entities.size() - 1U - candidate_visit;
              const auto &candidate = run.entities[candidate_index];
              if (candidate_index == index || candidate.runtime_removed ||
                  candidate.root_model_terminal ||
                  candidate.faction_index == 0 ||
                  candidate.faction_index == e.faction_index ||
                  candidate.shot_impact_class_index >= 8)
                continue;
              const auto relation =
                  (run.faction_packed_relation_states[e.faction_index] >>
                   (candidate.faction_index * 4U)) &
                  15U;
              if (relation >= 4U)
                continue;
              const auto candidate_metrics =
                  simulation_lab_combat_metrics(candidate);
              const auto raw = simulation_lab_combat_score(
                  {candidate.motion.collision_radius_q12,
                   add(float_to_q12(e.actor.position.x),
                       wrap(0U - uint32_t(float_to_q12(
                                     candidate.actor.position.x)))),
                   add(float_to_q12(e.actor.position.y),
                       wrap(0U - uint32_t(float_to_q12(
                                     candidate.actor.position.y)))),
                   candidate.runtime_model_class_id,
                   source_metrics
                       .class_damage_q12[candidate.shot_impact_class_index],
                   candidate_metrics
                       .class_damage_q12[e.shot_impact_class_index],
                   candidate_metrics.condition_q12, 1});
              if (raw.value > 0 && uint32_t(raw.value) > best_score) {
                uint32_t score = uint32_t(raw.value);
                // Native tests the first attached model's shot only when an
                // attached runtime exists. SPR body nodes alone do not trigger
                // the obstruction callback.
                const auto attached =
                    std::find_if(e.model_components.begin(),
                                 e.model_components.end(), [&](const auto &c) {
                                   return c.parent_component_index ==
                                          e.root_model_component_index;
                                 });
                if (attached != e.model_components.end() &&
                    !e.combat_weapons.empty()) {
                  if (!hulls)
                    hulls = simulation_lab_current_shot_hulls(
                        run, field, *runtime_catalog_);
                  const auto &weapon = e.combat_weapons.front();
                  const auto aim = simulation_lab_target_aim_point(
                      candidate, field, *runtime_catalog_);
                  const bool clear = simulation_lab_weapon_line_of_fire_clear(
                      run, index, candidate_index, pose.position, aim,
                      weapon.profile, weapon.shot, field, *hulls,
                      static_shot_hulls_);
                  score =
                      simulation_class21_apply_candidate_line_of_fire_result(
                          score, best_score, clear,
                          root->simulation.runtime_field_18_raw != 0);
                }
                if (score > best_score) {
                  best_score = score;
                  best = candidate_index;
                }
              }
            }
            if (best) {
              const auto &candidate = run.entities[*best];
              if (simulation_class21_candidate_within_projectile_lock(
                      float_to_q12(pose.position.x),
                      float_to_q12(pose.position.y),
                      float_to_q12(candidate.actor.position.x),
                      float_to_q12(candidate.actor.position.y),
                      candidate.motion.collision_radius_q12)) {
                const auto candidate_poses = simulation_lab_current_model_poses(
                    candidate, field, *runtime_catalog_);
                target = candidate_poses.empty()
                             ? candidate.actor.position
                             : candidate_poses.front().position;
              }
            }
            SimulationWeaponProfile profile;
            profile.shot_definition_id = shot->first;
            std::array launches{launch};
            auto volley = simulation_create_volley(
                index, 0, profile, launches, target, shot->second, run.tick,
                run.next_projectile_serial);
            for(auto& projectile:volley)claim_projectile_world_slot(run,projectile);
            run.projectiles.insert(run.projectiles.end(), volley.begin(),
                                   volley.end());
            ++run.metrics.weapon_shots_fired;
            ++e.weapon_volleys_fired;
            e.last_weapon_fire_tick = run.tick;
            if (shot->second.launch_effect_id)
              journal(run, e, 0, shot->second.launch_effect_id);
          }
          break;
        }
      }
      break;
    }
    case SimulationClass21LinkedChildPhase::OffmapReturn:
      if (!parent) {
        retire();
        break;
      }
      if (simulation_class21_begin_return_when_parent_idle(
              f.runtime, parent->airport, d.pad_ticks)) {
        const auto delta =
            retail_direction_scaled(0, parent->actor.body_yaw_turn16 ^ 0x8000U,
                                    mult(d.speed_q12, d.pad_ticks));
        e.actor.position = {
            q12_to_float(add(float_to_q12(parent->actor.position.x), delta[0])),
            q12_to_float(add(float_to_q12(parent->actor.position.y), delta[1])),
            0};
        e.actor.body_yaw_turn16 = parent->actor.body_yaw_turn16;
        f.arc_position = wrap(0U - uint32_t(mult(d.speed_q12, d.pad_ticks)));
        f.offmap = false;
        journal(run, *parent,
                parent->runtime_model_hierarchy
                    ? parent->runtime_model_hierarchy->front()
                          .simulation.runtime_class_effect_sound_asset_id_26c
                    : 0);
      }
      break;
    case SimulationClass21LinkedChildPhase::ReturningToPlatform:
      arc(d.takeoff_arc_q12, f.platform_height);
      advance(mult(d.speed_q12, int32_t(batch)));
      {
        SimulationClass22AirportState no_parent;
        if (simulation_class21_tick_return_to_platform(
                f.runtime, parent ? parent->airport : no_parent, batch)) {
          f.landed = true;
          e.production_parked_on_pad = true;
          if (parent && parent->root_model_maximum_integrity > 0) {
            const auto scaled = int64_t(parent->root_model_integrity) *
                                e.root_model_maximum_integrity /
                                parent->root_model_maximum_integrity;
            if (e.root_model_integrity <= scaled) {
              const auto damage =
                  uint32_t(scaled - e.root_model_integrity) >> 1;
              parent->root_model_integrity =
                  wrap(uint32_t(parent->root_model_integrity) - damage);
              if (parent->root_model_component_index <
                  parent->model_components.size())
                parent->model_components[parent->root_model_component_index]
                    .integrity = parent->root_model_integrity;
            }
          }
          if (parent)
            journal(run, *parent,
                    parent->runtime_model_hierarchy
                        ? parent->runtime_model_hierarchy->front()
                              .simulation.creation_sound_asset_id_270
                        : 0);
        }
      }
      break;
    }
    if (!e.runtime_removed) {
      e.actor.position = simulation_lab_factory_world_point(
          field, e.actor.position, e.actor.body_yaw_turn16,
          e.motion.placement_contact_kind,
          e.production_root_sprite_height_offset, {},
          e.motion.placement_height_offset_q12);
      e.actor.last_updated_tick = run.tick;
    }
  }
}
} // namespace thandor
