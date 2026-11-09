#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bash -n "$root/build_linux.sh" "$root/tools/build_common.sh"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/fake-cmake" <<'SCRIPT'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$THANDOR_PARALLEL_TEST_CALLS"
SCRIPT
chmod +x "$tmp/fake-cmake"
THANDOR_PARALLEL_TEST_CALLS="$tmp/calls" \
THANDOR_CMAKE_COMMAND="$tmp/fake-cmake" \
THANDOR_LINUX_BUILD_DIR="$tmp/build" \
THANDOR_BUILD_APP=0 THANDOR_RUN_TESTS=0 \
THANDOR_BUILD_JOBS=7 THANDOR_TEST_JOBS=5 \
bash "$root/build_linux.sh" > "$tmp/output"
grep -q 'compile jobs=7 | test jobs=5' "$tmp/output"
grep -q -- '--build .* --parallel 7' "$tmp/calls"
grep -q -- '-DTHANDOR_EDITOR_BUILD_TESTS=1' "$tmp/calls"
printf 'BUILD_LINUX_PARALLEL PASS compile=7 test=5 one_multi_target_build=1\n'
