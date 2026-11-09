#!/usr/bin/env python3
from pathlib import Path
import sys
root=Path(__file__).resolve().parents[1]
app=(root/'src/editor/editor_app.cpp').read_text()
hdr=(root/'src/editor/editor_app.hpp').read_text()
perf=(root/'src/editor/editor_performance_log.hpp').read_text()
assert 'shadow.projected_pose_cache.find(shadow_position,shadow_yaw)' in app
assert 'shadow.projected_pose_cache.store(shadow_position,shadow_yaw,std::move(rebuilt))' in app
assert 'for(auto& shadow:dynamic_shadow_templates_)' in app
assert 'ShadowReceiverPoseCache<ObjectVertex> projected_pose_cache;' in hdr
assert 'generated_shadow_pose_cache_reused_vertices=' in perf
print('shadow receiver pose source-contract PASS')
