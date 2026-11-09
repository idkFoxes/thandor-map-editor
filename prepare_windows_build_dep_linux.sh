#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
deps_dir="${THANDOR_WINDOWS_DEPS_DIR:-${root_dir}/.deps/windows}"
src_dir="${deps_dir}/src"
vulkan_prefix="${deps_dir}/vulkan"
toolchain_file="${deps_dir}/x86_64-w64-mingw32-toolchain.cmake"
triplet="${THANDOR_WINDOWS_TRIPLET:-x86_64-w64-mingw32}"
glfw_tag="${THANDOR_GLFW_TAG:-3.4}"
imgui_tag="${THANDOR_IMGUI_TAG:-v1.90.9}"
skip_packages="${THANDOR_SKIP_SYSTEM_PACKAGES:-0}"

log() {
    printf '[prepare-windows] %s\n' "$*"
}

die() {
    printf '[prepare-windows] ERROR: %s\n' "$*" >&2
    exit 1
}

run_root() {
    if [[ "${EUID}" -eq 0 ]]; then
        "$@"
    elif command -v sudo >/dev/null 2>&1; then
        sudo "$@"
    else
        die "sudo is required to install system packages. Install the dependencies manually or set THANDOR_SKIP_SYSTEM_PACKAGES=1."
    fi
}

install_system_packages() {
    [[ "${skip_packages}" == "1" ]] && {
        log "Skipping system package installation."
        return
    }

    if command -v pacman >/dev/null 2>&1; then
        log "Installing Arch Linux build dependencies."
        run_root pacman -S --needed --noconfirm \
            base-devel cmake ninja git python mingw-w64-gcc shaderc vulkan-headers
    elif command -v apt-get >/dev/null 2>&1; then
        log "Installing Debian/Ubuntu build dependencies."
        run_root apt-get update
        local packages=(build-essential cmake ninja-build git python3 mingw-w64 libvulkan-dev)
        if apt-cache show glslc >/dev/null 2>&1; then
            packages+=(glslc)
        fi
        run_root apt-get install -y "${packages[@]}"
    else
        log "Unsupported package manager. Install CMake, Ninja, Git, Python 3, MinGW-w64, Vulkan headers, and host glslc manually."
    fi
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "Required command not found: $1"
}

clone_tag() {
    local url="$1"
    local tag="$2"
    local destination="$3"

    if [[ -d "${destination}/.git" ]]; then
        log "Refreshing $(basename "${destination}") at ${tag}."
        git -C "${destination}" fetch --tags --force --prune
        git -C "${destination}" checkout --detach --force "${tag}"
        git -C "${destination}" clean -ffd
    else
        rm -rf -- "${destination}"
        log "Downloading $(basename "${destination}") ${tag}."
        git clone --depth 1 --branch "${tag}" "${url}" "${destination}"
    fi
}

find_glslc() {
    if [[ -n "${THANDOR_GLSLC:-}" && -x "${THANDOR_GLSLC}" ]]; then
        printf '%s\n' "${THANDOR_GLSLC}"
        return
    fi
    if [[ -n "${VULKAN_SDK:-}" && -x "${VULKAN_SDK}/bin/glslc" ]]; then
        printf '%s\n' "${VULKAN_SDK}/bin/glslc"
        return
    fi
    command -v glslc 2>/dev/null || true
}

find_vulkan_include_root() {
    local candidate
    for candidate in \
        "${THANDOR_VULKAN_INCLUDE_DIR:-}" \
        "${VULKAN_SDK:-}/include" \
        "/usr/${triplet}/include" \
        "/usr/include" \
        "/usr/local/include"; do
        [[ -n "${candidate}" && -f "${candidate}/vulkan/vulkan.h" ]] || continue
        printf '%s\n' "${candidate}"
        return
    done
}

install_system_packages

require_command cmake
require_command git
require_command python3
require_command ninja
require_command "${triplet}-gcc"
require_command "${triplet}-g++"
require_command "${triplet}-windres"
require_command "${triplet}-dlltool"

mkdir -p "${src_dir}" "${vulkan_prefix}/include" "${vulkan_prefix}/lib"
clone_tag https://github.com/glfw/glfw.git "${glfw_tag}" "${src_dir}/glfw"
clone_tag https://github.com/ocornut/imgui.git "${imgui_tag}" "${src_dir}/imgui"

vulkan_include_root="$(find_vulkan_include_root)"
[[ -n "${vulkan_include_root}" ]] || die "Vulkan headers were not found. Install vulkan-headers/libvulkan-dev or set THANDOR_VULKAN_INCLUDE_DIR."
rm -rf -- "${vulkan_prefix}/include/vulkan"
cp -a "${vulkan_include_root}/vulkan" "${vulkan_prefix}/include/vulkan"
# Recent Vulkan core headers include the sibling video codec headers too.
rm -rf -- "${vulkan_prefix}/include/vk_video"
if [[ -d "${vulkan_include_root}/vk_video" ]]; then
    cp -a "${vulkan_include_root}/vk_video" "${vulkan_prefix}/include/vk_video"
fi

# Windows normally provides vulkan-1.dll through the installed GPU driver. The
# cross-link only needs an import library. Generate one from the Vulkan calls
# used by this project and the pinned Dear ImGui Vulkan backend.
def_file="${vulkan_prefix}/lib/vulkan-1.def"
python3 - "${root_dir}" "${src_dir}/imgui" "${def_file}" <<'PY'
from pathlib import Path
import re
import sys

project_root = Path(sys.argv[1])
imgui_root = Path(sys.argv[2])
output = Path(sys.argv[3])

files = []
for base in (project_root / "src", project_root / "include"):
    files.extend(path for path in base.rglob("*") if path.suffix in {".c", ".cc", ".cpp", ".h", ".hpp"})
files.extend([
    imgui_root / "backends" / "imgui_impl_vulkan.cpp",
    imgui_root / "backends" / "imgui_impl_vulkan.h",
])

symbols = {
    "vkCreateInstance",
    "vkEnumerateInstanceExtensionProperties",
    "vkEnumerateInstanceLayerProperties",
    "vkEnumerateInstanceVersion",
    "vkGetDeviceProcAddr",
    "vkGetInstanceProcAddr",
}
pattern = re.compile(r"\b(vk[A-Z][A-Za-z0-9_]*)\s*\(")
for path in files:
    if path.is_file():
        symbols.update(pattern.findall(path.read_text(encoding="utf-8", errors="ignore")))

output.parent.mkdir(parents=True, exist_ok=True)
output.write_text("LIBRARY vulkan-1.dll\nEXPORTS\n" + "".join(f"    {name}\n" for name in sorted(symbols)), encoding="ascii")
print(f"Generated {output} with {len(symbols)} Vulkan imports")
PY

vulkan_import_library="${vulkan_prefix}/lib/libvulkan-1.dll.a"
"${triplet}-dlltool" \
    --dllname vulkan-1.dll \
    --input-def "${def_file}" \
    --output-lib "${vulkan_import_library}"

cat > "${toolchain_file}" <<EOF_TOOLCHAIN
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER ${triplet}-gcc)
set(CMAKE_CXX_COMPILER ${triplet}-g++)
set(CMAKE_RC_COMPILER ${triplet}-windres)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH /usr/${triplet} "${vulkan_prefix}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOF_TOOLCHAIN

glslc_path="$(find_glslc)"
[[ -n "${glslc_path}" && -x "${glslc_path}" ]] || die "Host glslc was not found. Install shaderc/glslc, source the Linux Vulkan SDK, or set THANDOR_GLSLC."

cat > "${deps_dir}/prepared.env" <<EOF_ENV
THANDOR_WINDOWS_TRIPLET='${triplet}'
THANDOR_WINDOWS_TOOLCHAIN='${toolchain_file}'
THANDOR_WINDOWS_VULKAN_INCLUDE='${vulkan_prefix}/include'
THANDOR_WINDOWS_VULKAN_LIBRARY='${vulkan_import_library}'
THANDOR_WINDOWS_GLSLC='${glslc_path}'
THANDOR_WINDOWS_GLFW_SOURCE='${src_dir}/glfw'
THANDOR_WINDOWS_IMGUI_SOURCE='${src_dir}/imgui'
EOF_ENV

log "Windows cross-build dependencies are ready."
log "Next: ./build_windows_from_linux.sh"
