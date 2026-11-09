#!/usr/bin/env bash
set -euo pipefail
# Retained Release entry point with stock asset probes. Asset locations are
# selected through the editor / CMake configuration.
root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export THANDOR_BUILD_TYPE="${THANDOR_BUILD_TYPE:-Release}"
export THANDOR_BUILD_PROBES="${THANDOR_BUILD_PROBES:-1}"
exec bash "$root_dir/build_linux.sh" "$@"
