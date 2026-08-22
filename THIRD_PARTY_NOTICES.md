# Third-party notices

AKEIR Engine links the following libraries statically (fetched by CPM.cmake at configure time; versions pinned in `cmake/Dependencies.cmake`). The prebuilt `bin/akeir.exe` in release zips contains all of them.

| Dependency | Version | License | Used for |
|---|---:|---|---|
| [Flecs](https://github.com/SanderMertens/flecs) | v4.1.6 | MIT | ECS runtime world (play-world projection) |
| [Box2D](https://github.com/erincatto/box2d) | v3.1.1 | MIT | 2D physics |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.12.0 | MIT | JSON documents, RFC 6901/6902 |
| [doctest](https://github.com/doctest/doctest) | v2.5.3 | MIT | unit tests (not linked into `akeir.exe`) |
| [SDL3](https://github.com/libsdl-org/SDL) | release-3.4.14 | zlib | window, input, software renderer, PNG I/O (`AKEIR_WITH_SDL=ON` builds) |
| [stb](https://github.com/nothings/stb) (`stb_truetype.h`) | v1.26 (commit 2c980bb, 2026-08-01) | public domain / MIT (dual) | TTF/OTF glyph rasterization for `TextRenderer.font` (`AKEIR_WITH_SDL=ON` builds, ADR-0046) |
| [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) | v0.43.1 (vendored as `cmake/CPM.cmake`) | MIT | dependency fetching at configure time |

Full license texts are in each upstream repository linked above, and locally in the fetched sources (`.cpm-cache/<dir>/` when created with `scripts/fetch-deps.ps1`, otherwise in the CPM source cache used at configure time). The MIT and zlib licenses require that the copyright notices below accompany binary distributions; this file satisfies that for the release zip.

## Notices

- **Flecs** — Copyright (c) 2025 Sander Mertens; portions Copyright (c) Meta Platforms, Inc. and affiliates. MIT License.
- **Box2D** — Copyright (c) 2022 Erin Catto. MIT License.
- **nlohmann/json** — Copyright (c) 2013-2025 Niels Lohmann. MIT License.
- **doctest** — Copyright (c) 2016-2023 Viktor Kirilov. MIT License (test binary only).
- **SDL3** — Copyright (C) 1997-2026 Sam Lantinga. zlib License: this software is provided 'as-is', without any express or implied warranty; altered source versions must be plainly marked as such; this notice may not be removed or altered from any source distribution.
- **stb_truetype.h** — Copyright (c) 2017 Sean Barrett. Dual-licensed: MIT License or public domain (Unlicense); AKEIR uses it under the MIT alternative.
- **CPM.cmake** — Copyright (c) 2019-2023 Lars Melchior and contributors. MIT License (build-time only).

MIT License text (applies to the MIT entries above): Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files, to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, subject to the condition that the above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.

Algorithms referenced but not vendored: TypeID v0.3 / UUIDv7 (RFC 9562) id grammar, xoshiro256** (public domain, reimplemented in `Engine/Core`), RFC 8785 JSON Canonicalization Scheme (reimplemented in `Engine/Serialization`).
