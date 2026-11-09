#!/usr/bin/env bash
# Reproducible CPU/contract checks; intentionally requires no Vulkan or GLFW SDK.
set -euo pipefail
source_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${THANDOR_PERF_BUILD_DIR:-${source_dir}/build-perf}"
cmake -S "$source_dir" -B "$build_dir" -G Ninja \
  -DTHANDOR_EDITOR_BUILD_APP=OFF -DTHANDOR_EDITOR_BUILD_TESTS=ON -DTHANDOR_EDITOR_FETCH_DEPS=OFF
cmake --build "$build_dir" --parallel "${THANDOR_BUILD_JOBS:-2}" --target \
  editor_performance_log_tests sprite_canvas_cache_tests object_texture_source_reuse_tests rendering_object_texture_updates_tests rendering_object_texture_stable_slots_tests rendering_frame_versioned_atlas_tests simlab_worker_backpressure_tests \
  simlab_worker_pool_tests simlab_worker_wakeup_tests simlab_performance_diagnostics_tests
ctest --test-dir "$build_dir" --output-on-failure \
  -R '^(editor_performance_log_tests|sprite_canvas_cache_tests|object_texture_source_reuse_tests|rendering_object_texture_updates_tests|rendering_object_texture_stable_slots_tests|rendering_frame_versioned_atlas_tests|simlab_worker_backpressure_tests|simlab_worker_pool_tests|simlab_worker_wakeup_tests|simlab_performance_diagnostics_tests|performance_compare_tests)$'
python3 "$source_dir/tests/editor_translation_unit_compile_tests.py" --source "$source_dir"
python3 "$source_dir/tests/rendering_visual_timing_source_contract_tests.py" --source "$source_dir"
python3 "$source_dir/tests/simlab_threading_source_contract_tests.py" --source "$source_dir"
python3 "$source_dir/tests/rendering_swapchain_image_lifetime_source_contract_tests.py"
python3 "$source_dir/tests/rendering_shared_frame_fence_lifetime_source_contract_tests.py"
python3 "$source_dir/tests/rendering_simulation_run_switcher_source_contract_tests.py"
python3 "$source_dir/tests/rendering_frame_slot_lifetime_source_contract_tests.py"
