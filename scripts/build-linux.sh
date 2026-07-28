#!/usr/bin/env bash
set -euo pipefail

readonly toolchain_version="20260616"
readonly toolchain_archive="llvm-mingw-${toolchain_version}-ucrt-ubuntu-22.04-x86_64.tar.xz"
readonly toolchain_url="https://github.com/mstorsjo/llvm-mingw/releases/download/${toolchain_version}/${toolchain_archive}"
readonly toolchain_sha256="534b92e067b22a6b4441f48ae9240a3341b17825d04d577eab0cf85c44b4deda"
readonly cache_directory="${XDG_CACHE_HOME:-${HOME}/.cache}/nioh1fix"
readonly toolchain_directory="${cache_directory}/llvm-mingw-${toolchain_version}"
readonly archive_path="${cache_directory}/${toolchain_archive}"

mkdir -p "${cache_directory}"
if [[ ! -x "${toolchain_directory}/bin/x86_64-w64-mingw32-clang++" ]]; then
    curl -fL --retry 2 "${toolchain_url}" -o "${archive_path}"
    printf '%s  %s\n' "${toolchain_sha256}" "${archive_path}" | sha256sum --check
    rm -rf "${toolchain_directory}"
    mkdir -p "${toolchain_directory}"
    tar -xJf "${archive_path}" -C "${toolchain_directory}" --strip-components=1
fi

cmake -S . -B build-windows \
    -DCMAKE_SYSTEM_NAME=Windows \
    -DCMAKE_SYSTEM_PROCESSOR=x86_64 \
    -DCMAKE_CXX_COMPILER="${toolchain_directory}/bin/x86_64-w64-mingw32-clang++" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTING=OFF
cmake --build build-windows --parallel

scripts/package.sh build-windows dist
