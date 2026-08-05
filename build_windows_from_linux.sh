#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
deps_dir="${THANDOR_WINDOWS_DEPS_DIR:-${root_dir}/.deps/windows}"
env_file="${deps_dir}/prepared.env"
build_dir="${THANDOR_WINDOWS_BUILD_DIR:-${root_dir}/build-windows-x64}"
dist_root="${THANDOR_WINDOWS_DIST_ROOT:-${root_dir}/dist}"
dist_dir="${THANDOR_WINDOWS_DIST_DIR:-${dist_root}/windows_x64}"
archive="${THANDOR_WINDOWS_ARCHIVE:-${dist_root}/ThandorMapEditor-windows-x64.zip}"
configuration="${THANDOR_WINDOWS_CONFIGURATION:-Release}"
clean_build="${THANDOR_CLEAN_BUILD:-1}"
build_tests="${THANDOR_WINDOWS_BUILD_TESTS:-0}"
auto_prepare="${THANDOR_AUTO_PREPARE:-1}"
wine_smoke_test="${THANDOR_WINE_SMOKE_TEST:-0}"

log() {
    printf '[build-windows] %s\n' "$*"
}

die() {
    printf '[build-windows] ERROR: %s\n' "$*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1"
}

if [[ ! -f "${env_file}" ]]; then
    if [[ "${auto_prepare}" == "1" ]]; then
        log "Prepared dependencies were not found; running prepare_windows_build_dep_linux.sh."
        "${root_dir}/prepare_windows_build_dep_linux.sh"
    else
        die "Run ./prepare_windows_build_dep_linux.sh first."
    fi
fi

# shellcheck disable=SC1090
source "${env_file}"

require_command cmake
require_command "${THANDOR_WINDOWS_TRIPLET}-g++"
require_command "${THANDOR_WINDOWS_TRIPLET}-gcc"
require_command "${THANDOR_WINDOWS_TRIPLET}-objdump"

for required in \
    "${THANDOR_WINDOWS_TOOLCHAIN}" \
    "${THANDOR_WINDOWS_VULKAN_INCLUDE}/vulkan/vulkan.h" \
    "${THANDOR_WINDOWS_VULKAN_LIBRARY}" \
    "${THANDOR_WINDOWS_GLSLC}" \
    "${THANDOR_WINDOWS_GLFW_SOURCE}/CMakeLists.txt" \
    "${THANDOR_WINDOWS_IMGUI_SOURCE}/imgui.h"; do
    [[ -e "${required}" ]] || die "Prepared dependency is missing: ${required}"
done

# Detect an old or incomplete Vulkan staging directory before starting the
# complete build. Current Vulkan headers require the sibling vk_video tree.
vulkan_probe_source=$'#define VK_USE_PLATFORM_WIN32_KHR 1\n#include <vulkan/vulkan.h>\nint main() { return 0; }\n'
if ! printf '%s' "${vulkan_probe_source}" | "${THANDOR_WINDOWS_TRIPLET}-g++" \
        -std=c++20 -fsyntax-only -x c++ \
        -I"${THANDOR_WINDOWS_VULKAN_INCLUDE}" -; then
    die "Prepared Vulkan headers are incomplete. Rerun ./prepare_windows_build_dep_linux.sh."
fi

if [[ "${clean_build}" != "0" ]]; then
    preserved_count="$("${root_dir}/tools/clean_build_directory_preserving_artifacts.sh" "${build_dir}")"
    if [[ "${preserved_count}" -gt 0 ]]; then
        log "Preserved ${preserved_count} existing EXE/PCK artifact(s) across the clean build."
    fi
else
    mkdir -p -- "${build_dir}"
fi

cmake -S "${root_dir}" -B "${build_dir}" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="${THANDOR_WINDOWS_TOOLCHAIN}" \
    -DCMAKE_BUILD_TYPE="${configuration}" \
    -DCMAKE_CROSSCOMPILING_EMULATOR="" \
    -DBUILD_SHARED_LIBS=OFF \
    -DTHANDOR_EDITOR_BUILD_APP=ON \
    -DTHANDOR_EDITOR_BUILD_TESTS="$([[ "${build_tests}" == "1" ]] && printf ON || printf OFF)" \
    -DTHANDOR_EDITOR_FETCH_DEPS=ON \
    -DFETCHCONTENT_SOURCE_DIR_GLFW="${THANDOR_WINDOWS_GLFW_SOURCE}" \
    -DFETCHCONTENT_SOURCE_DIR_IMGUI="${THANDOR_WINDOWS_IMGUI_SOURCE}" \
    -DVulkan_INCLUDE_DIR="${THANDOR_WINDOWS_VULKAN_INCLUDE}" \
    -DVulkan_LIBRARY="${THANDOR_WINDOWS_VULKAN_LIBRARY}" \
    -DVulkan_GLSLC_EXECUTABLE="${THANDOR_WINDOWS_GLSLC}" \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++"

cmake --build "${build_dir}" --parallel

exe="${build_dir}/thandor-map-editor.exe"
shader_source_dir="${build_dir}/shaders"
[[ -f "${exe}" ]] || die "Build completed without ${exe}."
[[ -d "${shader_source_dir}" ]] || die "Compiled shader directory is missing: ${shader_source_dir}"

# Always recreate the shareable folder. This prevents stale DLLs, logs, CMake
# files, test executables, object files, and previous release files from leaking
# into a new package even when THANDOR_CLEAN_BUILD=0 is used.
rm -rf -- "${dist_dir}"
# Remove the legacy hyphenated folder produced by the earlier script when the
# new default underscore path is in use. This avoids leaving two release trees.
if [[ "${dist_dir}" == "${dist_root}/windows_x64" ]]; then
    rm -rf -- "${dist_root}/windows-x64"
fi
mkdir -p "${dist_dir}/shaders"

install -m 0755 "${exe}" "${dist_dir}/thandor-map-editor.exe"

shader_count=0
while IFS= read -r -d '' shader; do
    install -m 0644 "${shader}" "${dist_dir}/shaders/$(basename "${shader}")"
    shader_count=$((shader_count + 1))
done < <(find "${shader_source_dir}" -maxdepth 1 -type f -name '*.spv' -print0 | sort -z)
[[ "${shader_count}" -gt 0 ]] || die "No compiled SPIR-V shaders were found in ${shader_source_dir}."

objdump_tool="${THANDOR_WINDOWS_TRIPLET}-objdump"

list_imported_dlls() {
    "${objdump_tool}" -p "$1" 2>/dev/null \
        | sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p' \
        | tr -d '\r' \
        | awk 'NF && !seen[tolower($0)]++'
}

is_windows_or_driver_dll() {
    local dll="${1,,}"
    case "${dll}" in
        advapi32.dll|bcrypt.dll|cfgmgr32.dll|comctl32.dll|comdlg32.dll|crypt32.dll|d3d*.dll|dbghelp.dll|dwmapi.dll|dxgi.dll|gdi32.dll|imm32.dll|kernel32.dll|msvcrt.dll|ntdll.dll|ole32.dll|oleaut32.dll|powrprof.dll|psapi.dll|rpcrt4.dll|secur32.dll|setupapi.dll|shell32.dll|shlwapi.dll|ucrtbase.dll|user32.dll|version.dll|winmm.dll|winspool.drv|ws2_32.dll|vulkan-1.dll)
            return 0
            ;;
    esac
    return 1
}

find_cross_runtime_dll() {
    local dll="$1"
    local compiler candidate

    # GCC knows where its own runtime DLLs are installed. This works across
    # Arch, Debian/Ubuntu, custom MinGW prefixes, and most packaged toolchains.
    for compiler in "${THANDOR_WINDOWS_TRIPLET}-g++" "${THANDOR_WINDOWS_TRIPLET}-gcc"; do
        candidate="$("${compiler}" -print-file-name="${dll}" 2>/dev/null || true)"
        if [[ -n "${candidate}" && "${candidate}" != "${dll}" && -f "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done

    # Some distributions keep runtime DLLs in the triplet bin directory and
    # do not return them through -print-file-name.
    local compiler_path compiler_bin search_root
    compiler_path="$(command -v "${THANDOR_WINDOWS_TRIPLET}-g++")"
    compiler_bin="$(cd "$(dirname "${compiler_path}")" && pwd)"
    for search_root in \
        "${compiler_bin}" \
        "${compiler_bin}/../${THANDOR_WINDOWS_TRIPLET}/bin" \
        "/usr/${THANDOR_WINDOWS_TRIPLET}/bin" \
        "/usr/lib/gcc/${THANDOR_WINDOWS_TRIPLET}"; do
        [[ -d "${search_root}" ]] || continue
        candidate="$(find "${search_root}" -type f -iname "${dll}" -print -quit 2>/dev/null || true)"
        if [[ -n "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done
    return 1
}

# Copy every non-system DLL reachable from the executable. This includes
# libwinpthread-1.dll and, when dynamically linked, libstdc++-6.dll and
# libgcc_s_seh-1.dll. Newly copied DLLs are scanned as well so their MinGW
# runtime dependencies are included recursively.
declare -a scan_queue=("${dist_dir}/thandor-map-editor.exe")
declare -A scanned_files=()
declare -A copied_dlls=()
scan_index=0

while (( scan_index < ${#scan_queue[@]} )); do
    scan_file="${scan_queue[scan_index]}"
    scan_index=$((scan_index + 1))
    scan_key="${scan_file,,}"
    [[ -z "${scanned_files[${scan_key}]:-}" ]] || continue
    scanned_files["${scan_key}"]=1

    while IFS= read -r dll; do
        [[ -n "${dll}" ]] || continue
        dll_key="${dll,,}"
        is_windows_or_driver_dll "${dll}" && continue
        [[ -z "${copied_dlls[${dll_key}]:-}" ]] || continue

        if dll_path="$(find_cross_runtime_dll "${dll}")"; then
            install -m 0755 "${dll_path}" "${dist_dir}/${dll}"
            copied_dlls["${dll_key}"]=1
            scan_queue+=("${dist_dir}/${dll}")
            log "Bundled runtime DLL: ${dll}"
        else
            die "Required non-system DLL '${dll}' was not found in the MinGW toolchain."
        fi
    done < <(list_imported_dlls "${scan_file}")
done

cat > "${dist_dir}/README-WINDOWS.txt" <<'EOF_README'
Thandor Vulkan Map Editor - Windows x64

Keep thandor-map-editor.exe, the bundled DLL files, and the shaders directory together.
Run the editor and select a Thandor installation containing game.exe or thandor.exe.

Requirements:
- 64-bit Windows
- A Vulkan-capable graphics driver

vulkan-1.dll is installed by the graphics driver and is not bundled.
No Microsoft Visual C++ Redistributable is required for this MinGW-w64 build.
EOF_README

# Enforce a release allowlist. A shareable package contains only the editor,
# required runtime DLLs, compiled shaders, and the short Windows readme.
while IFS= read -r -d '' item; do
    relative="${item#${dist_dir}/}"
    case "${relative}" in
        thandor-map-editor.exe|README-WINDOWS.txt|*.dll|shaders/*.spv)
            ;;
        *)
            rm -f -- "${item}"
            ;;
    esac
done < <(find "${dist_dir}" -type f -print0)
find "${dist_dir}" -depth -type d -empty -delete

# Verify that every non-system import is present beside the executable after
# cleanup. This catches packaging regressions before the ZIP is created.
for packaged_binary in "${dist_dir}/thandor-map-editor.exe" "${dist_dir}"/*.dll; do
    [[ -f "${packaged_binary}" ]] || continue
    while IFS= read -r dll; do
        [[ -n "${dll}" ]] || continue
        is_windows_or_driver_dll "${dll}" && continue
        [[ -f "${dist_dir}/${dll}" ]] || die "Package validation failed: ${dll} is required by $(basename "${packaged_binary}") but is missing."
    done < <(list_imported_dlls "${packaged_binary}")
done

rm -f -- "${archive}"
mkdir -p "${dist_root}"
(
    cd "${dist_root}"
    cmake -E tar cf "${archive}" --format=zip -- "$(basename "${dist_dir}")"
)

if command -v file >/dev/null 2>&1; then
    file "${dist_dir}/thandor-map-editor.exe"
fi

if [[ "${wine_smoke_test}" == "1" ]]; then
    wine_command=""
    if command -v wine64 >/dev/null 2>&1; then
        wine_command=wine64
    elif command -v wine >/dev/null 2>&1; then
        wine_command=wine
    fi
    if [[ -n "${wine_command}" ]]; then
        log "Running optional Wine startup smoke test."
        (
            cd "${dist_dir}"
            "${wine_command}" ./thandor-map-editor.exe --help
        )
        # Wine or the application can create these beside the executable.
        rm -f -- "${dist_dir}/imgui.ini" "${dist_dir}"/*.log "${dist_dir}"/*.dmp
    else
        log "Wine is not installed; skipping the optional smoke test."
    fi
fi

log "Clean Windows folder: ${dist_dir}"
log "Release archive: ${archive}"
