# AKEIR Engine 0.1.1 — QUICKSTART

This zip is release v0.1.1 of the **AKEIR Engine** (pronounced *ay-KEER*; Greek ἄχειρ "without hands" — a game engine for making games without human hands): **source + docs + a prebuilt `bin\akeir.exe` (Windows x64, SDL included, static CRT — no redistributable needed)**.
Purpose: a session with no prior conversation context (human or AI) unpacks this directory and **tries to build a game with this engine**.

## 0. What is inside

```
AKEIR-0.1.1/
├── QUICKSTART.md              ← this file
├── RELEASE.md                 version, git hash, sha256 of bin/akeir.exe
├── LICENSE (MIT) · THIRD_PARTY_NOTICES.md (notices for the statically linked Flecs/Box2D/nlohmann/SDL3) · CONTRIBUTING.md
├── bin\akeir.exe               the prebuilt CLI. Works without building. `akeir` in the examples below = this file. Put bin\ on PATH or use the full path
├── bin\TestArena.exe           the sample game — double-click to play (WASD, ESC/close to quit). Rebuilding your own game produces bin\<YourProjectName>.exe
├── .mcp.json                  MCP server registration for Claude Code (bin\akeir.exe mcp --project Game). Relative path — open this folder as the Claude Code project root
├── Docs/                      00-START-HERE → STATUS → DECISIONS → BUILD → ARCHITECTURE → CONVENTIONS
├── AKEIR.md   the design spec (§0–§89, with ▶ v3 implementation notes)
├── Game/                      the sample project "TestArena" (a player + 3 goblins, 3 tests, golden images). Feel free to modify — it is a reference
└── Engine/ Tools/ Tests/ Game/Source/ cmake/ scripts/ CMakeLists.txt CMakePresets.json   source (to rebuild see Docs/BUILD.md; there is no .cpm-cache, so the first configure fetches dependencies from GitHub)
```

## 1. Try it in 5 minutes (no build needed)

```bat
set G=%CD%\bin\akeir.exe
cd Game
%G% --help                                      # full command list (`%G% <command> --help` shows that command's usage)
%G% version --json
%G% capabilities --json                         # 15 tools, busCommands (14 write commands + argument schemas), exit/error code tables
%G% project info --json
%G% run --headless --ticks 600 --json           # deterministic run. result.finalHash must be 0x4ac7b45c37618374
%G% test --json                                 # 3 data-driven tests (Combat / Movement / Visual golden)
%G% capture --ticks 300 --out Cache\capture\f.png --json   # CPU-rendered PNG
%G% run --ticks 120                             # the same windowed run from the CLI (closes itself after 2 s). To just play, double-click bin\TestArena.exe
```

- stdout is a single JSON envelope (`{ok, command, result|error, changes, warnings, meta}`). Without `--json` it is pretty-printed on a terminal (TTY); when piped it is always one line.
- stderr carries JSONL logs (normal). Hide them with `2>nul` (cmd) / `2>$null` (PowerShell).
- exit codes: `0 ok / 1 failed / 2 usage / 3 findings / 4 confirm (--yes) / 5 not found / 6 crash / 7 timeout` (full table: `capabilities --json` → exitCodes).
- **Selectors**: to refer to an object use any of an id like `entity_…`, a bare name (`Goblin_01`), `name:Goblin_01`, or `path:TestArena/Arena/Player`. Both entities and prefabs are searched. Several objects with the same name → `AMBIGUOUS_SELECTOR` with a candidate list.

## 2. Create a new game project

```bat
%G% project init MyGame --dir C:\work\MyGame --json
cd C:\work\MyGame
%G% schema --all --json                          # the available components and their properties (type/range/enum — values are lowercase: "circle", "dynamic")
```

`project init` creates: `project.json`, `Worlds/Main.world.json` (one MainCamera), `Config/input.json` (MoveX/MoveY/Attack), empty `Prefabs/ Tests/ Data/ Assets/`, `.gitignore`, and a `README.md` with the next commands (copy-pasteable examples included).

In cmd.exe, escape the double quotes in JSON arguments as `\"` (PowerShell: wrap in single quotes):
```bat
%G% prefab create Hero --components "{\"Collider2D\":{\"shape\":\"circle\",\"radius\":0.4},\"RigidBody2D\":{\"type\":\"dynamic\",\"gravityScale\":0},\"Movement\":{\"speed\":5},\"PlayerController\":{},\"Health\":{\"max\":100}}" --json
%G% prefab instantiate Hero --name Player --json
%G% entity create Wall --components "{\"Collider2D\":{\"size\":[10,1]},\"RigidBody2D\":{\"type\":\"static\"},\"Transform\":{\"position\":[0,-5,0]}}" --json
%G% run --headless --ticks 600 --json
```

What you can do with data alone (no C++):
- Create/modify/delete entities, prefabs and worlds; set properties; tags; prefab inheritance (`--base`) and overrides. One command = one undo step (`akeir undo`; `validate --fix` is one step per fix, `apply`/`tx` make the whole batch one step).
- Physics (Box2D): `Collider2D` (box/circle/capsule) + `RigidBody2D` (static/kinematic/dynamic). **A `Collider2D` on its own has no body and blocks nothing** — give walls a `RigidBody2D {type: static}` too (`validate` warns with `COLLIDER_WITHOUT_BODY` and `--fix` adds it).
- Player movement: `Collider2D` + `RigidBody2D` (dynamic, gravityScale 0) + `Movement` + `PlayerController` (MoveX/MoveY from input.json). The dependency chain (`Movement` → `RigidBody2D` → `Collider2D`) is listed under `x-requires` in `schema --all`; a missing link is rejected with `COMPONENT_DEPENDENCY_MISSING` (the whole creation is rolled back).
- Enemy AI: `EnemyAI` (chases inside detectionRange, attacks inside attackRange, targetTag) + `Health`. **For an attack to have an effect, the target needs `Health` too.**
- Camera: `Camera2D` (orthoSize, background).
- **Textures**: drop a PNG under `Assets/`, register it — `%G% asset import Assets/Textures/cats.png --grid 16x16 --names hero,cat,fish --json` — and point sprites at it: `%G% set <selector> SpriteRenderer.sprite "<asset id>#sprites/hero"` (the id is in the import result / `Assets/Textures/cats.png.meta.json`). Pixel art stays crisp (nearest), `pixelsPerUnit` sets the world size, `tint`/`flipX`/`flipY` apply. Without a sprite ref an entity is drawn as a tinted shape. The sample's `Game/Assets/Textures/arena.png` shows the format.
- Tests: write `Tests/**/*.test.json` **by hand** (setup / inputs / assert / determinism / capture golden). The file format and the assertion language (grammar, functions, examples) come from `%G% schema test --json`; check one expression with `%G% test explain "player.Health.current > 0" --snapshot Cache\snap.json --as player=<id>` (snapshot from `run --headless --snapshot-out`). Typos in function names are parse errors with a suggestion.

What needs C++: new components/systems (e.g. projectiles, score, spawn waves). Add them under `Game/Source/` and rebuild per `Docs/BUILD.md` (VS2022 + bundled CMake/Ninja; `scripts\build.cmd msvc-release all`, tests in `build\msvc-release\Tests\akeir_tests.exe`). A `msvc-release` build also refreshes this zip's `bin\akeir.exe`, and you can rebuild while Claude Code's MCP server is running: the locked file is moved aside and the server switches to the new build on its next tool call (the response carries a `MCP_WORKER_RESTARTED` note). Every struct member must be `AKEIR_PROP`'d or `AKEIR_SKIP`'d — `akeir validate` reports `REFLECT_MEMBER_UNLISTED` otherwise.

## 3. Use it from an AI client (MCP)

Open the unpacked directory in Claude Code and `.mcp.json` registers the `akeir` server (stdio, target project = `Game/`). To target another project, change the `--project` path. Claude Code (Node) resolves relative paths against the project root. Other launchers (Python etc.) may not, so generate an absolute-path version in that case:
```bat
%G% mcp --print-config --project C:\work\MyGame > .mcp.json
```
15 tools: `capabilities, project_info, schema_describe, query, inspect, explain, refs, apply, validate, run, run_status, test, capture, tx, history`. All writes go through the single `apply` tool (`changes[].op` = `entity.create`, `property.set` …; `$name` references an earlier result; `dryRun`). Each op's argument schema is in the `capabilities` tool's `busCommands[]`. `tx` (begin → apply… → commit) works directly inside an MCP session.

Manual check:
```bat
echo {"jsonrpc":"2.0","id":1,"method":"tools/list"} | bin\akeir.exe mcp --project Game
```

## 4. Several CLI calls as one undo step / a resident process

```bat
cd C:\work\MyGame                                    (or pass --project C:\work\MyGame to every command)
start %G% serve                                      # in another window. From now on every akeir command for this project goes to that process automatically (meta.via = "serve")
%G% tx begin --json                                  → result.tx
%G% entity create A --tx tx_… --json
%G% tx commit tx_… --json
%G% serve stop
```
`--tx` without serve → `TX_REQUIRES_SERVE`. To bypass the daemon, use `--local`.

## 5. When stuck

- `akeir <command> --help`, the `errorCodes[]` in `akeir capabilities --json`, and for each error its `error.ruleId` + `error.details` + `fixes[]` (if MachineApplicable: `akeir validate --fix`).
- `Docs/00-START-HERE.md` → the module READMEs (`Engine/*/README.md`, `Tools/CLI/README.md`) → the design doc §.
- Known limitations: the known-issues / technical-debt section of `Docs/STATUS.md` (e.g. MCP argument typos are silently ignored, `query` has `limit` but no cursor).
- On a crash, report `<project>/Cache/crash/*.dmp` together with the envelope's `error.details.lastLogs` (regenerate the `.pdb` symbols by checking out the same tag and running `scripts\build.cmd msvc-release all`).

git: this release is tag `v0.1.1` (v0.1.0 was the first release; v0.1.1 adds the rebuild-safe MCP server, the reflection completeness check and English READMEs). The zip contains no `.git` (`git archive`) — a new project simply creates its own repository (`project init` writes a `.gitignore`).
