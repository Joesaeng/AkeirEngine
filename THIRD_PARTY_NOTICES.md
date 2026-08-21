# Third-party notices

AKEIR Engine links the following libraries statically (fetched by CPM.cmake at configure time; versions pinned in `cmake/Dependencies.cmake`). The prebuilt `bin/akeir.exe` in release zips contains all of them.

| Dependency | Version | License | Used for |
|---|---:|---|---|
| [Flecs](https://github.com/SanderMertens/flecs) | v4.1.6 | MIT | ECS runtime world (play-world projection) |
| [Box2D](https://github.com/erincatto/box2d) | v3.1.1 | MIT | 2D physics |
| [nlohmann/json](https://github.com/nlohmann/json) | v3.12.0 | MIT | JSON documents, RFC 6901/6902 |
| [doctest](https://github.com/doctest/doctest) | v2.5.3 | MIT | unit tests (not linked into `akeir.exe`) |
| [SDL3](https://github.com/libsdl-org/SDL) | release-3.4.14 | zlib | window, input, software renderer, PNG I/O (`AKEIR_WITH_SDL=ON` builds) |
| [CPM.cmake](https://github.com/cpm-cmake/CPM.cmake) | v0.43.1 (vendored as `cmake/CPM.cmake`) | MIT | dependency fetching at configure time |

Each library's full license text is in its source tree under `.cpm-cache/<name>/` after the first configure (or in the upstream repository linked above). The MIT and zlib licenses require that these notices accompany binary distributions; this file satisfies that for the release zip.

Algorithms referenced but not vendored: TypeID v0.3 / UUIDv7 (RFC 9562) id grammar, xoshiro256** (public domain, reimplemented in `Engine/Core`), RFC 8785 JSON Canonicalization Scheme (reimplemented in `Engine/Serialization`).
