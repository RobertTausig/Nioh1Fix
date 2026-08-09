#!/usr/bin/env bash
set -euo pipefail

readonly script_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly cmake_binary="$("${script_directory}/cmake.sh" --print-path)"
readonly ctest_binary="$(dirname -- "${cmake_binary}")/ctest"

exec "${ctest_binary}" "$@"
