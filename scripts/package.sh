#!/usr/bin/env bash
set -euo pipefail

readonly build_directory="${1:-build-windows}"
readonly output_directory="${2:-dist}"
readonly loader_version="v9.7.1"
readonly loader_url="https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/${loader_version}/Ultimate-ASI-Loader_x64.zip"
readonly loader_sha256="77da5b4c3ab4552b3ba605667961c9a46f1b6c78c80667d572d1e811e9670306"
readonly version="0.5.0"
readonly staging="${output_directory}/Nioh1Fix"
readonly loader_zip="${output_directory}/Ultimate-ASI-Loader_x64.zip"
readonly archive="${output_directory}/Nioh1Fix-${version}.zip"

if [[ ! -f "${build_directory}/Nioh1Fix.asi" ]]; then
    printf 'Nioh1Fix.asi was not found under %s\n' "${build_directory}" >&2
    exit 1
fi

rm -rf "${staging}"
mkdir -p "${staging}"
cp "${build_directory}/Nioh1Fix.asi" Nioh1Fix.ini README.md LICENSE THIRD_PARTY.md "${staging}/"

curl -fL --retry 2 "${loader_url}" -o "${loader_zip}"
printf '%s  %s\n' "${loader_sha256}" "${loader_zip}" | sha256sum --check
unzip -q -o "${loader_zip}" -d "${staging}"
mv "${staging}/dinput8.dll" "${staging}/version.dll"

rm -f "${archive}"
(
    cd "${staging}"
    zip -q -9 "../$(basename "${archive}")" ./*
)
printf 'Created %s\n' "${archive}"
