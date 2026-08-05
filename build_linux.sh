#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build="${THANDOR_LINUX_BUILD_DIR:-$root/build}"
parallel="${THANDOR_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"
if [[ "${THANDOR_CLEAN_BUILD:-1}" != "0" ]]; then
  "$root/tools/clean_build_directory_preserving_artifacts.sh" "$build"
fi
cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE="${THANDOR_BUILD_TYPE:-RelWithDebInfo}" \
  -DTHANDOR_EDITOR_BUILD_APP=ON -DTHANDOR_EDITOR_BUILD_TESTS=ON
cmake --build "$build" --parallel "$parallel"
[[ -x "$build/thandor-map-editor" ]] || { echo "missing $build/thandor-map-editor" >&2; exit 88; }
ctest --test-dir "$build" --output-on-failure
