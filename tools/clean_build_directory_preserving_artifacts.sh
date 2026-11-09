#!/usr/bin/env bash
set -euo pipefail

if [[ "$#" -ne 1 ]]; then
    printf 'usage: %s BUILD_DIRECTORY\n' "$0" >&2
    exit 64
fi

build_dir="$(realpath -m -- "$1")"
source_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
if [[ "$build_dir" == / || "$source_dir" == "$build_dir" || "$source_dir" == "$build_dir/"* ]]; then
    printf 'Refusing to clean source directory or its ancestor: %s\n' "$build_dir" >&2
    exit 64
fi
preserved="$(mktemp -d "${TMPDIR:-/tmp}/thandor-build-artifacts.XXXXXX")"
cleanup() { rm -rf -- "${preserved}"; }
trap cleanup EXIT

preserved_count=0
if [[ -d "${build_dir}" ]]; then
    while IFS= read -r -d '' artifact; do
        relative_path="${artifact#"${build_dir}"/}"
        mkdir -p -- "${preserved}/$(dirname "${relative_path}")"
        cp -a -- "${artifact}" "${preserved}/${relative_path}"
        preserved_count=$((preserved_count + 1))
    done < <(find "${build_dir}" -type f \( -iname '*.exe' -o -iname '*.pck' \) -print0)
fi

rm -rf -- "${build_dir}"
mkdir -p -- "${build_dir}"
if [[ "${preserved_count}" -gt 0 ]]; then
    cp -a -- "${preserved}/." "${build_dir}/"
fi
printf '%s\n' "${preserved_count}"
