# Tests/Fixtures — frozen data for the engine unit tests

`TestArena/` is an immutable copy of the original sample project (project.json, Worlds, Prefabs, Config, Tests incl. goldens, and its C++ components/systems under `Source/` as the `akeir_fixture_game` library). The engine tests (`Tests/*.cpp`) load it through the compile-time `AKEIR_TEST_FIXTURES` path and link `akeir_fixture_game`, so **`Game/` can be replaced by any game without breaking `akeir_tests.exe`** (ADR-0036 — this is what broke when CatSurvivor replaced the sample).

Do not edit the fixture to make a test pass; add a new fixture or a test-local document instead. `Game/` stays the live sample that `akeir.exe`, the game executable and `akeir test` use.
