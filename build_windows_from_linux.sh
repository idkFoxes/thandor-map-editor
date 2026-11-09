#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
deps_dir="${THANDOR_WINDOWS_DEPS_DIR:-${root_dir}/.deps/windows}"
env_file="${deps_dir}/prepared.env"
build_dir="${THANDOR_WINDOWS_BUILD_DIR:-${root_dir}/build-windows-x64}"
dist_dir="${THANDOR_WINDOWS_DIST_DIR:-${root_dir}/dist/windows-x64}"
configuration="${THANDOR_WINDOWS_CONFIGURATION:-Release}"
build_tests="${THANDOR_WINDOWS_BUILD_TESTS:-0}"
auto_prepare="${THANDOR_AUTO_PREPARE:-1}"

log() {
    printf '[build-windows] %s\n' "$*"
}

die() {
    printf '[build-windows] ERROR: %s\n' "$*" >&2
    exit 1
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

for required in \
    "${THANDOR_WINDOWS_TOOLCHAIN}" \
    "${THANDOR_WINDOWS_VULKAN_INCLUDE}/vulkan/vulkan.h" \
    "${THANDOR_WINDOWS_VULKAN_LIBRARY}" \
    "${THANDOR_WINDOWS_GLSLC}" \
    "${THANDOR_WINDOWS_GLFW_SOURCE}/CMakeLists.txt" \
    "${THANDOR_WINDOWS_IMGUI_SOURCE}/imgui.h"; do
    [[ -e "${required}" ]] || die "Prepared dependency is missing: ${required}"
done

source "${root_dir}/tools/build_common.sh"
thandor_build_setup "${root_dir}" "${build_dir}"
mkdir -p "${build_dir}" "${dist_dir}"

"$THANDOR_CMAKE" -S "${root_dir}" -B "${build_dir}" "${THANDOR_GENERATOR_ARGS[@]}" \
    -DCMAKE_TOOLCHAIN_FILE="${THANDOR_WINDOWS_TOOLCHAIN}" \
    -DCMAKE_BUILD_TYPE="${configuration}" \
    -DCMAKE_CROSSCOMPILING_EMULATOR="" \
    -DBUILD_SHARED_LIBS=OFF \
    -DTHANDOR_EDITOR_BUILD_APP=ON \
    -DTHANDOR_EDITOR_BUILD_TESTS="$([[ "${build_tests}" == "1" ]] && printf ON || printf OFF)" \
    -DTHANDOR_EDITOR_BUILD_PROBES="${THANDOR_BUILD_PROBES:-0}" \
    -DTHANDOR_EDITOR_FETCH_DEPS=ON \
    -DFETCHCONTENT_SOURCE_DIR_GLFW="${THANDOR_WINDOWS_GLFW_SOURCE}" \
    -DFETCHCONTENT_SOURCE_DIR_IMGUI="${THANDOR_WINDOWS_IMGUI_SOURCE}" \
    -DIMGUI_DIR="${THANDOR_WINDOWS_IMGUI_SOURCE}" \
    -DVulkan_INCLUDE_DIR="${THANDOR_WINDOWS_VULKAN_INCLUDE}" \
    -DVulkan_LIBRARY="${THANDOR_WINDOWS_VULKAN_LIBRARY}" \
    -DVulkan_GLSLC_EXECUTABLE="${THANDOR_WINDOWS_GLSLC}" \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libgcc -static-libstdc++"

"$THANDOR_CMAKE" --build "${build_dir}" --parallel "$THANDOR_JOBS"

exe="${build_dir}/thandor-map-editor.exe"
[[ -f "${exe}" ]] || die "Build completed without ${exe}."
[[ -d "${build_dir}/shaders" ]] || die "Compiled shader directory is missing: ${build_dir}/shaders"
[[ -f "${build_dir}/assets/fonts/DejaVuSans.ttf" ]] || die "Runtime font is missing: ${build_dir}/assets/fonts/DejaVuSans.ttf"

cp -f "${exe}" "${dist_dir}/thandor-map-editor.exe"
rm -rf -- "${dist_dir}/shaders"
cp -a "${build_dir}/shaders" "${dist_dir}/shaders"
mkdir -p "${dist_dir}/assets"
rm -rf -- "${dist_dir}/assets/fonts"
cp -a "${build_dir}/assets/fonts" "${dist_dir}/assets/fonts"

# Copy MinGW runtime DLLs only when the linker did not absorb them statically.
objdump_tool="${THANDOR_WINDOWS_TRIPLET}-objdump"
if command -v "${objdump_tool}" >/dev/null 2>&1; then
    while IFS= read -r dll; do
        case "${dll}" in
            libgcc_s_*.dll|libstdc++-6.dll|libwinpthread-1.dll)
                dll_path="$(find "/usr/lib/gcc/${THANDOR_WINDOWS_TRIPLET}" "/usr/${THANDOR_WINDOWS_TRIPLET}" \
                    -type f -iname "${dll}" -print -quit 2>/dev/null || true)"
                [[ -z "${dll_path}" ]] || cp -f "${dll_path}" "${dist_dir}/${dll}"
                ;;
        esac
    done < <("${objdump_tool}" -p "${dist_dir}/thandor-map-editor.exe" | sed -n 's/^[[:space:]]*DLL Name: //p')
fi

cat > "${dist_dir}/README-WINDOWS.txt" <<'EOF_README'
Thandor Vulkan Map Editor - Windows x64

Keep thandor-map-editor.exe and the shaders and assets directories together.
Run the editor and select a Thandor installation containing either game.exe or thandor.exe.

Runtime requirements:
- 64-bit Windows
- A Vulkan-capable graphics driver that installs vulkan-1.dll

The Vulkan loader DLL is supplied by the graphics driver and is intentionally not bundled here.
EOF_README

(
    cd "${dist_dir}"
    sha256sum thandor-map-editor.exe shaders/*.spv assets/fonts/* > SHA256SUMS.txt
)

archive="${root_dir}/dist/ThandorMapEditor-windows-x64.zip"
rm -f -- "${archive}"
mkdir -p "$(dirname "${archive}")"
(
    cd "$(dirname "${dist_dir}")"
    "$THANDOR_CMAKE" -E tar cf "${archive}" --format=zip -- "$(basename "${dist_dir}")"
)

if command -v file >/dev/null 2>&1; then
    file "${dist_dir}/thandor-map-editor.exe"
fi

if [[ "${THANDOR_WINE_SMOKE_TEST:-1}" == "1" ]]; then
    wine_command=""
    if command -v wine64 >/dev/null 2>&1; then
        wine_command=wine64
    elif command -v wine >/dev/null 2>&1; then
        wine_command=wine
    fi
    if [[ -n "${wine_command}" ]]; then
        log "Running the dependency/startup smoke test with Wine (--help)."
        "${wine_command}" "${dist_dir}/thandor-map-editor.exe" --help
    else
        log "Wine is not installed; skipping the executable smoke test."
    fi
fi

log "Built package: ${archive}"
log "Unpacked files: ${dist_dir}"
