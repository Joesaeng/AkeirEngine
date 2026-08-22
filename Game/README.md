# Game/ — the sample project "TestArena"

Design doc §5 (project layout), §6 (document model), §71 (PoC scenario), ADR-0001 (2D top-down arena). **The JSON in this directory is the source of truth** — the engine reads it to build the play world, and changes are made only through the `akeir` CLI (CommandBus).

```
Game/
├── project.json                 { name, tickRate: 60, seed, defaultWorld: "world_…" }
├── Worlds/TestArena.world.json  entities (id-keyed): Arena, MainCamera, Player (instance of the Player prefab), Encounter_01 ┬ Goblin_01..03 (instances of the Goblin prefab)
├── Prefabs/                     Player / Goblin / GoblinElite (base: Goblin + set)  — one *.prefab.json each
├── Config/input.json            action map: MoveX/MoveY (axis), Attack (button)  (§88.3)
├── Tests/                       data-driven tests (§23): Combat/GoblinBasicCombat, Movement/PlayerMovement, Visual/CombatCapture (requires renderer)  → `akeir test`
│   ├── Golden/<test>/           golden PNGs (produced only by the software renderer: `akeir test Visual --update-golden`)
│   └── .results/                `akeir test` output (gitignored)
├── Data/, Assets/               still empty (texture loading is not implemented — sprites are drawn as tinted shapes)
├── Cache/                       engine-derived data (gitignored): crash/, journal/, history/ (the undo stack)
└── Source/                      this game's C++ components/systems (statically linked; see its README)
```

- Every file must be in §5.3 canonical form — `akeir validate` warns with `JSON_NOT_CANONICAL`, `akeir fmt` fixes it.
- Reference values: `akeir run --headless --ticks 600` → finalHash `0xbc23e49a65efb2e8`; the run-twice check of GoblinBasicCombat in `akeir test` yields the same value. If this value changes, either the data or determinism changed (`Docs/STATUS.md`).
- The scenario (§71): a player (WASD = MoveX/MoveY) and three goblins that chase (`chase`) inside `detectionRange` and deal `damage` every `attackCooldown` inside `attackRange` (`attack`). After 600 ticks the player has 10 HP.
- `Game/` is **yours to replace**: the engine unit tests use the frozen copy in `Tests/Fixtures/TestArena` (ADR-0036), so swapping this directory for your own game (as CatSurvivor did) breaks nothing. The game executable `build/<preset>/bin/<name>.exe` is named after `project.json` → `name`.
- Do not hand-write JSON for new entities/prefabs/worlds; use `akeir entity create` / `akeir prefab create` / `akeir world create` (or `akeir apply batch.json`) — ids (UUIDv7), defaults, canonical form and history are handled automatically.
