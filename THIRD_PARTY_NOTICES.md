# Third-party notices

The engine is AGPL-3.0 (`LICENSE`). What it builds from others arrives through vcpkg, which keeps
each package's licence at `vcpkg_installed/<triplet>/share/<port>/copyright`. This file records what
the engine adds by hand: a library it fetches through an overlay port of its own
(`cmake/ports/`), or vendors, and links into what it ships.

## NvAPI

- **What:** NVIDIA's NvAPI SDK, the headers and `nvapi64.lib`, a static stub that reaches the
  driver's `nvapi64.dll`. Linked into `bgpu` on D3D12 for `GpuContextDesc::preferMaximumPerformance`
  ([docs/bgpu.md § Maximum performance](docs/bgpu.md#maximum-performance)).
- **From:** <https://github.com/NVIDIA/nvapi>, commit `70d337db9186e968eab622f7e786de7e437faf3d`
  (the R615 SDK), by `cmake/ports/nvapi`. The repository publishes no tags, so the port pins a commit.
- **Licence:** MIT. At that commit `License.txt` puts `nvapi.lib`, `nvapi64.lib`, `nvapia64.lib` and
  `nvapia64ec.lib` under MIT, and every header carries `SPDX-License-Identifier: MIT`.

```
SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
SPDX-License-Identifier: MIT

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
```
