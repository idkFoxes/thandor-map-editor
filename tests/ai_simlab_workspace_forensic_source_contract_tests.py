#!/usr/bin/env python3
"""Static V587 contract: forensic sampling must not alter AI/placement control flow."""
from pathlib import Path
import re
root = Path(__file__).resolve().parents[1]
h = (root/'include/thandor/simulation_lab.hpp').read_text()
c = (root/'src/core/simulation_lab.cpp').read_text()
e = (root/'src/editor/editor_app.cpp').read_text()
# V634 formatter moved to a joined writer; retain all forensic/control assertions.
formatter = (root/'src/editor/simulation_lab_diagnostic_writer.hpp').read_text()
t = (root/'tools/analyze_simlab_faction_progress.py').read_text()
assert 'struct SimulationLabResourceSiteSample' in h
assert 'std::array<SimulationLabResourceSiteSample, 32> resource_site_samples' in h
assert 'std::min(ws.resource08.size(), info.resource_site_samples.size())' in c
assert 'sample.in_build_reach = false' in c
assert 'info.resource_site_sample_count' in c
assert 'plan_construction(run, field, selected_faction);' in c
assert 'class18_special_site_target(run,entity,field,runtime_catalog,' in c
assert 'static_placement_owners,&entity.pioneer_site_selector)' in c
assert 'RESOURCE_SITE run=' in formatter and 'reach_only=' in formatter
assert 'pioneer_ai_commands=' in formatter and 'pioneer_movement_flags=' in formatter
# This diagnostic contract outlives V587; the active log version advances per release.
paths = (root/'src/editor/editor_paths.hpp').read_text()
assert 'simulation_lab_logs_directory()' in e
assert 'editor_logs_directory()' in paths and 'return application_directory() / "Log";' in paths and 'return editor_logs_directory() / "Simlab";' in paths
assert 'latest_sites' in t and 'latest_pending' in t
print('V587_SIMLAB_WORKSPACE_FORENSIC_SOURCE_CONTRACT PASS')
