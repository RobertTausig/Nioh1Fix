#!/usr/bin/env bash
set -euo pipefail

readonly script_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly cache_directory="${XDG_CACHE_HOME:-${HOME}/.cache}/nioh1fix"
readonly versions_file="${script_directory}/../cmake/toolchain-versions.env"
# shellcheck disable=SC1090
source "${versions_file}"
readonly cmake_release="${NIOH1FIX_CMAKE_RELEASE}"
readonly cmake_version_family="${cmake_release%.*}"
readonly cmake_archive="cmake-${cmake_release}-linux-x86_64.tar.gz"
readonly cmake_url="https://github.com/Kitware/CMake/releases/download/v${cmake_release}/${cmake_archive}"
readonly cmake_sha256="${NIOH1FIX_CMAKE_SHA256}"
readonly bundled_archive_path="${script_directory}/../third_party/archives/${cmake_archive}"
readonly cmake_directory="${cache_directory}/cmake-${cmake_release}"
readonly cmake_binary="${cmake_directory}/bin/cmake"

mkdir -p "${cache_directory}"
cmake_archive_path="${cache_directory}/${cmake_archive}"
if [[ -f "${bundled_archive_path}" ]]; then
    cmake_archive_path="${bundled_archive_path}"
elif [[ ! -f "${cmake_archive_path}" ]]; then
    curl -fL --proto '=https' --tlsv1.2 --retry 2 \
        "${cmake_url}" -o "${cmake_archive_path}"
fi
readonly cmake_archive_path
printf '%s  %s\n' "${cmake_sha256}" "${cmake_archive_path}" |
    sha256sum --check --status

if [[ ! -x "${cmake_binary}" ]]; then
    extraction_directory="$(mktemp -d "${cache_directory}/.cmake-${cmake_release}.XXXXXX")"
    trap 'rm -rf "${extraction_directory}"' EXIT
    tar -xzf "${cmake_archive_path}" -C "${extraction_directory}" \
        --strip-components=1
    if [[ -e "${cmake_directory}" ]]; then
        printf 'CMake directory exists but is incomplete: %s\n' \
            "${cmake_directory}" >&2
        exit 1
    fi
    mv "${extraction_directory}" "${cmake_directory}"
    trap - EXIT
fi

actual_version="$("${cmake_binary}" --version | awk 'NR == 1 { print $3 }')"
if [[ "${actual_version}" != "${cmake_version_family}."* ]]; then
    printf 'Expected CMake %s.x, found %s\n' \
        "${cmake_version_family}" "${actual_version}" >&2
    exit 1
fi

if [[ "${1:-}" == "--print-path" ]]; then
    printf '%s\n' "${cmake_binary}"
    exit 0
fi

exec "${cmake_binary}" "$@"
