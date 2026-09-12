# Preserved build inputs

This directory retains unmodified copies of the exact third-party archives
used to build and package Nioh1Fix:

- `cmake-4.4.2-linux-x86_64.tar.gz`
- `llvm-mingw-20260616-ucrt-ubuntu-22.04-x86_64.tar.xz`
- `Ultimate-ASI-Loader_x64-v9.7.1.zip`

Their expected SHA-256 hashes are defined in
`cmake/toolchain-versions.env` and `scripts/package.sh`. Build scripts prefer
these local copies, fail when a hash does not match, and use the corresponding
upstream release URL only when a copy is absent.

The CMake and LLVM-MinGW archives contain their applicable licenses and
third-party notices. Ultimate ASI Loader's MIT license is reproduced in the
repository's `THIRD_PARTY.md`, which is also included in Nioh1Fix release
packages.
