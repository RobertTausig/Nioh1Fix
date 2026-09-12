# Third-party software

## Ultimate ASI Loader

Release packages include the x64 build of Ultimate ASI Loader, renamed to
`version.dll`.

- Version: 9.7.1
- Copyright: ThirteenAG and contributors
- License: MIT
- Source and license:
  <https://github.com/ThirteenAG/Ultimate-ASI-Loader/tree/v9.7.1>

The pinned upstream archive is retained unmodified in the source repository.
Because that archive contains only the loader binary, its license text is
reproduced below and included in Nioh1Fix release packages through this file.

### License text

MIT License

Copyright (c) 2023 ThirteenAG

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

## CMake

CMake is used to configure and drive the Linux-hosted builds.

- Version: 4.4.2
- Copyright: 2000-2026 Kitware, Inc. and contributors
- License: BSD 3-Clause
- Source and license: <https://github.com/Kitware/CMake/tree/v4.4.2>

The unmodified Linux binary archive is retained in the source repository but
is not included in Nioh1Fix release packages. Its license and bundled
component notices remain inside the archive under `doc/cmake`.

## LLVM-MinGW

LLVM-MinGW provides the Linux-hosted Clang toolchain used for native tests and
for cross-compiling the Windows plugin.

- Version: 20260616, Clang 22
- Copyright: llvm-mingw, LLVM, MinGW-w64, and component contributors
- Licenses: ISC for llvm-mingw; Apache 2.0 with LLVM exceptions and other
  licenses for the bundled components
- Source and licenses:
  <https://github.com/mstorsjo/llvm-mingw/tree/20260616>

The unmodified toolchain archive is retained in the source repository but is
not included in Nioh1Fix release packages. Its license and component notices
remain inside the archive in `LICENSE.TXT` and the target share trees.
