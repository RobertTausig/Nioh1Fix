#!/usr/bin/env bash
set -euo pipefail

readonly script_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly versions_file="${script_directory}/../cmake/toolchain-versions.env"
# shellcheck disable=SC1090
source "${versions_file}"
readonly toolchain_release="${NIOH1FIX_LLVM_MINGW_RELEASE}"
readonly clang_major="${NIOH1FIX_CLANG_MAJOR}"
readonly toolchain_archive="llvm-mingw-${toolchain_release}-ucrt-ubuntu-22.04-x86_64.tar.xz"
readonly toolchain_url="https://github.com/mstorsjo/llvm-mingw/releases/download/${toolchain_release}/${toolchain_archive}"
readonly toolchain_sha256="${NIOH1FIX_LLVM_MINGW_SHA256}"
readonly cache_directory="${XDG_CACHE_HOME:-${HOME}/.cache}/nioh1fix"
readonly bundled_archive_path="${script_directory}/../third_party/archives/${toolchain_archive}"
readonly toolchain_directory="${cache_directory}/llvm-mingw-${toolchain_release}"
readonly native_clang="${toolchain_directory}/bin/clang++"
readonly windows_clang="${toolchain_directory}/bin/x86_64-w64-mingw32-clang++"

mkdir -p "${cache_directory}"
archive_path="${cache_directory}/${toolchain_archive}"
if [[ -f "${bundled_archive_path}" ]]; then
    archive_path="${bundled_archive_path}"
elif [[ ! -f "${archive_path}" ]]; then
    curl -fL --proto '=https' --tlsv1.2 --retry 2 \
        "${toolchain_url}" -o "${archive_path}"
fi
readonly archive_path
printf '%s  %s\n' "${toolchain_sha256}" "${archive_path}" |
    sha256sum --check --status

if [[ ! -x "${native_clang}" || ! -x "${windows_clang}" ]]; then
    extraction_directory="$(mktemp -d "${cache_directory}/.llvm-mingw-${toolchain_release}.XXXXXX")"
    trap 'rm -rf "${extraction_directory}"' EXIT
    tar -xJf "${archive_path}" -C "${extraction_directory}" \
        --strip-components=1
    if [[ -e "${toolchain_directory}" ]]; then
        printf 'LLVM-MinGW directory exists but is incomplete: %s\n' \
            "${toolchain_directory}" >&2
        exit 1
    fi
    mv "${extraction_directory}" "${toolchain_directory}"
    trap - EXIT
fi

actual_version="$("${native_clang}" --version |
    awk '/^clang version/ { print $3; exit }')"
actual_major="${actual_version%%.*}"
if [[ "${actual_major}" != "${clang_major}" ]]; then
    printf 'Expected Clang major %s, found %s\n' \
        "${clang_major}" "${actual_version}" >&2
    exit 1
fi

if [[ "$("${native_clang}" -dumpmachine)" != "x86_64-unknown-linux-gnu" ]]; then
    printf 'The LLVM-MinGW native compiler has an unexpected target\n' >&2
    exit 1
fi
if [[ "$("${windows_clang}" -dumpmachine)" != "x86_64-w64-windows-gnu" ]]; then
    printf 'The LLVM-MinGW cross compiler has an unexpected target\n' >&2
    exit 1
fi

printf '%s\n' "${toolchain_directory}"
