from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
app = (root / 'src/editor/editor_app.cpp').read_text()
hdr = (root / 'src/editor/editor_app.hpp').read_text()

checks = {
    'transient ref identity': 'enum class SimulationTransientKind' in hdr and 'SimulationTransientRef' in hdr,
    'transient hierarchy hull helper': 'simulation_transient_pick_hulls' in app and 'has_valid_header_bounds()' in app,
    'shared produced owner scan': 'SimulationTransientKind::ProducedClass13' in app and 'simulation_produced_entities_' in app,
    'shared linked owner scan': 'SimulationTransientKind::LinkedClass21' in app and 'simulation_linked_child_entities_' in app,
    'retail owner-list provenance': 'ModelRuntime_RaycastCandidateListNearestCf walks one WorldRuntime owner list' in app,
    'class21 excludes child not airport': 'SimulationTransientRef{SimulationTransientKind::LinkedClass21,source_serial}' in app and 'candidate.transient,false' in app,
    'intended transient accepted': 'intended_dynamic=model_hit->transient&&intended_transient' in app,
    'point target respects transient identity': '&&!intended_transient.has_value()' in app,
    'transient radius range clearance': 'intended_transient->kind==SimulationTransientKind::ProducedClass13' in app and 'target_radius_q12' in app,
    'shared model raycast helper': 'simulation_model_raycast_nearest' in app,
    'mode0 maintenance reraycast': 'simulation_refresh_mode0_projectile_raycast(projectile,*shot,tick)' in app and 'FieldRaycastSurface::Secondary' in app,
    'non-direct maintenance segment raycast': 'simulation_refresh_non_direct_projectile_model_collision(projectile,*shot,tick)' in app and 'current_tick-1U' in app,
    'non-direct owner exclusion': 'excluded_transient=SimulationTransientRef{SimulationTransientKind::LinkedClass21' in app and 'segment_distance,current_tick,excluded_placement,excluded_transient' in app,
    'non-direct null-effect nearest rejection': 'if(effect_id==0U)return;' in app and 'does not continue searching behind it' in app,
    'same-tick surface ordering': 'projectile.travel_ticks==age' in app and 'surface_distance+1.0e-6F<model->distance' in app,
    'non-direct exact hull contact': 'projectile.impact_position=add(segment_start,mul(direction,model->distance))' in app and 'projectile.travel_ticks=age' in app,
    'post-impact live-target freeze': 'simulation_projectile_impacted(projectile,tick)' in app,
    'projectile transient owner identity': 'SimulationProjectileOwnerKind::LinkedClass21' in app and 'owner_runtime_serial' in app,
}
failed = [name for name, ok in checks.items() if not ok]
for name, ok in checks.items():
    print(f"{'PASS' if ok else 'FAIL'} {name}")
if failed:
    print('V547 transient owner LOS source contract failed: ' + ', '.join(failed), file=sys.stderr)
    raise SystemExit(1)
print('V547_TRANSIENT_OWNER_LOS_SOURCE_CONTRACT PASS')
