#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
build="${THANDOR_LINUX_BUILD_DIR:-${THANDOR_BUILD_DIR:-$root/build}}"
source "$root/tools/build_common.sh"
thandor_build_setup "$root" "$build"
build_app="${THANDOR_BUILD_APP:-1}"
build_tests="${THANDOR_BUILD_TESTS:-1}"
printf '[Thandor] build=%s | compile jobs=%s | test jobs=%s | generator=%s\n' \
    "$build" "$THANDOR_JOBS" "$THANDOR_TEST_PARALLEL" \
    "${THANDOR_GENERATOR:-auto (cached, otherwise Ninja if available)}"
"$THANDOR_CMAKE" -S "$root" -B "$build" "${THANDOR_GENERATOR_ARGS[@]}" \
  -DCMAKE_BUILD_TYPE="${THANDOR_BUILD_TYPE:-RelWithDebInfo}" \
  -DTHANDOR_EDITOR_BUILD_APP="$build_app" -DTHANDOR_EDITOR_BUILD_TESTS="$build_tests" \
  -DTHANDOR_EDITOR_BUILD_PROBES="${THANDOR_BUILD_PROBES:-0}"
# A single multi-job invocation lets Ninja/Make schedule core, GUI, shaders,
# tests, and requested probes together as dependency edges become available.
# Separate --target invocations would serialize those independent jobs.
"$THANDOR_CMAKE" --build "$build" --parallel "$THANDOR_JOBS"
if [[ "$build_app" =~ ^(1|ON|on|TRUE|true)$ ]]; then
  [[ -x "$build/thandor-map-editor" ]] || { echo "missing $build/thandor-map-editor" >&2; exit 88; }
  appimage="${THANDOR_BUILD_APPIMAGE:-ask}"
  if [[ "$appimage" == ask ]]; then
    appimage=no
    if [[ -t 0 ]]; then
      while true; do
        answer=""
        if ! read -r -p 'Compilation finished. Create an AppImage? [y/N] ' answer; then break; fi
        case "${answer,,}" in
          y|yes) appimage=yes; break ;;
          n|no|'') break ;;
          *) echo 'Please answer yes or no.' ;;
        esac
      done
    fi
  fi
  case "${appimage,,}" in
    y|yes|1|on|true) bash "$root/tools/build_appimage.sh" "$build" ;;
    n|no|0|off|false) ;;
    *) echo 'THANDOR_BUILD_APPIMAGE must be yes, no, or ask.' >&2; exit 64 ;;
  esac
fi
if [[ "$build_tests" =~ ^(1|ON|on|TRUE|true)$ && "${THANDOR_RUN_TESTS:-1}" != 0 ]]; then
  thandor_run_tests "$build"
fi
