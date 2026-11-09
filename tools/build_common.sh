#!/usr/bin/env bash
# Shared build setup. Incremental by default; an explicit clean retains EXE/PCK files.
thandor_build_setup() {
    local source_dir="$1" target_dir="$2" cpus mem_kib memory_jobs
    THANDOR_CMAKE="${THANDOR_CMAKE_COMMAND:-cmake}"
    if [[ -n "${THANDOR_CTEST_COMMAND:-}" ]]; then
        THANDOR_CTEST="$THANDOR_CTEST_COMMAND"
    elif [[ "$THANDOR_CMAKE" == */* && -x "${THANDOR_CMAKE%/*}/ctest" ]]; then
        THANDOR_CTEST="${THANDOR_CMAKE%/*}/ctest"
    else
        THANDOR_CTEST=ctest
    fi
    cpus="$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)"
    # Large simulation translation units need memory as well as CPU slots.
    mem_kib="$(awk '/MemAvailable:/ {print $2}' /proc/meminfo 2>/dev/null || true)"
    # Respect container limits, which may be much lower than host MemAvailable.
    local limit current remaining
    if [[ -r /sys/fs/cgroup/memory.max && -r /sys/fs/cgroup/memory.current ]]; then
        limit="$(cat /sys/fs/cgroup/memory.max)"
        current="$(cat /sys/fs/cgroup/memory.current)"
        if [[ "$limit" =~ ^[0-9]+$ && "$current" =~ ^[0-9]+$ ]]; then
            remaining=$(((limit - current) / 1024))
            ((remaining > 0)) || remaining=1
            if [[ ! "$mem_kib" =~ ^[0-9]+$ ]] || ((remaining < mem_kib)); then mem_kib=$remaining; fi
        fi
    fi
    # Do not let a low container memory limit silently reduce concurrency
    # without reporting it. Each large simulation TU can use substantial RAM.
    # Users with sufficient memory can override the budget and/or job count.
    local memory_per_job_mib="${THANDOR_BUILD_MEMORY_PER_JOB_MIB:-1536}"
    [[ "$memory_per_job_mib" =~ ^[1-9][0-9]*$ ]] || {
        echo 'THANDOR_BUILD_MEMORY_PER_JOB_MIB must be a positive integer.' >&2; return 64;
    }
    memory_jobs=$cpus
    if [[ "$mem_kib" =~ ^[0-9]+$ ]]; then
        memory_jobs=$((mem_kib / (memory_per_job_mib * 1024)))
        ((memory_jobs > 0)) || memory_jobs=1
    fi
    ((cpus <= memory_jobs)) || cpus=$memory_jobs
    THANDOR_JOBS="${THANDOR_BUILD_JOBS:-$cpus}"
    [[ "$THANDOR_JOBS" =~ ^[1-9][0-9]*$ ]] || {
        echo 'Build job count must be a positive integer.' >&2; return 64;
    }
    # CTest also schedules its independent executables concurrently. Allow
    # the explicit THANDOR_TEST_JOBS override for unusually expensive suites.
    THANDOR_TEST_PARALLEL="${THANDOR_TEST_JOBS:-$THANDOR_JOBS}"
    [[ "$THANDOR_TEST_PARALLEL" =~ ^[1-9][0-9]*$ ]] || {
        echo 'Test job count must be a positive integer.' >&2; return 64;
    }
    if [[ "${THANDOR_CLEAN_BUILD:-0}" != 0 ]]; then
        bash "$source_dir/tools/clean_build_directory_preserving_artifacts.sh" "$target_dir"
    fi
    # Keep a cached generator; prefer Ninja for a fresh build when available.
    THANDOR_GENERATOR_ARGS=()
    if [[ ! -f "$target_dir/CMakeCache.txt" ]]; then
        if [[ -n "${THANDOR_GENERATOR:-}" ]]; then
            THANDOR_GENERATOR_ARGS=(-G "$THANDOR_GENERATOR")
        elif command -v ninja >/dev/null 2>&1; then
            THANDOR_GENERATOR_ARGS=(-G Ninja)
        fi
    fi
}

thandor_run_tests() {
    local target_dir="$1"; shift
    local filters=()
    [[ -z "${THANDOR_TEST_LABEL:-}" ]] || filters+=(-L "$THANDOR_TEST_LABEL")
    [[ -z "${THANDOR_TEST_REGEX:-}" ]] || filters+=(-R "$THANDOR_TEST_REGEX")
    "$THANDOR_CTEST" --test-dir "$target_dir" --output-on-failure \
        --parallel "$THANDOR_TEST_PARALLEL" --no-tests=error "${filters[@]}" "$@"
}
