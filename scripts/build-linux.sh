#!/usr/bin/env bash
set -euo pipefail

readonly script_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly repository_directory="$(dirname -- "${script_directory}")"
cd "${repository_directory}"
toolchain_directory="$(scripts/ensure-llvm-mingw.sh)"
export PATH="${toolchain_directory}/bin:${PATH}"

scripts/cmake.sh --preset windows-llvm-mingw
scripts/cmake.sh --build --preset windows-llvm-mingw

scripts/package.sh build-windows-clang dist
