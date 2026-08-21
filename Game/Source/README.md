# Game/Source (`akeir_game`) — the sample game "TestArena"

The area an AI edits most often (§60). The engine knows nothing about this directory (§76).

- `GameComponents.h/.cpp`: `Health`, `Movement`, `PlayerController`, `EnemyAI` (+ the `AiState` enum). All aggregates, registered with `AKEIR_REFLECT_*`. `registerGameComponents()` is the anchor.
- `GameSystems.h/.cpp`: `registerGameSystems(world)` — the spawn hook `HealthInit` (current = max), then the systems `PlayerMovement` (MoveX/MoveY → velocity) → `EnemyChase` (chases the nearest `#player` within detection/attack range) → `EnemyAttack` (cooldown, damage, dead).
- The only engine API used is `PlayWorld`'s `query / get<T> / rng / contactEvents` (§59). No wall clock, no global RNG, no unordered iteration (§22.2). Ties are broken by id order.

Data: `Game/project.json`, `Game/Prefabs/*.prefab.json`, `Game/Worlds/TestArena.world.json`, `Game/Config/input.json`. `akeir validate` / `akeir fmt` / `akeir run --headless` / `akeir query` / `akeir dump` work from inside `Game/` (or with `--project Game`).

Executables and tests link this library with `$<LINK_LIBRARY:WHOLE_ARCHIVE,akeir_game>` (keeps the component registrations).

## Adding a component / system
1. Add an aggregate struct to `GameComponents.h`, an `AKEIR_REFLECT_BEGIN … END` block to `GameComponents.cpp`, and one anchor line in `registerGameComponents()`.
2. Write the system as a function in `GameSystems.cpp` and `addSystem` it in order inside `registerGameSystems`.
3. Put the component into a prefab JSON, then `akeir validate` → `akeir run --headless --ticks N --json` to check.
