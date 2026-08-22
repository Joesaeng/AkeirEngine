# Engine/Testing (`akeir_testing`)

The runner for data-driven test scenarios (§23) and the assertion expression language (§23.1). Used by `akeir test`. Knows nothing about Game/ — the caller supplies a `WorldFactory` that builds the world (including system registration).

## Files

| File | § | Contents |
|---|---|---|
| `include/akeir/testing/Expr.h`, `src/Expr.cpp` | §23.1 | Fixed comparison grammar: tokenizer → recursive-descent parser → AST evaluation. `Expr::parse / eval / evalBool / probeBindings / roots` |
| `include/akeir/testing/TestRunner.h`, `src/TestRunner.cpp` | §23, §24, §22.2 | `TestScenario::fromJson`, `TestRunner::discover / run / runAll / diffSnapshots`, `TestReport::toJson / junitXml` |

## Scenario files (`<project>/Tests/**/*.test.json`)

```json
{ "$schema": "game://schema/test/1", "name": "GoblinBasicCombat", "world": "<id|name:X>  (omitted = defaultWorld)", "seed": 1024,
  "setup":  [ { "entity": "path:TestArena/Arena/Player", "as": "player" },
              { "spawn": "name:Goblin", "as": "g", "name": "G1", "position": [5,0,0], "set": {"/components/Health/max": 5}, "tags": ["wave1"] } ],
  "inputs": [ { "tick": 0, "hold": {"MoveX": 1.0}, "untilTick": 60 }, { "tick": 130, "press": "Attack" },
              { "tick": 200, "axis": {"MoveY": -1}, "untilTick": 260 }, { "tick": 300, "release": "MoveX" } ],
  "run": { "ticks": 600, "tickRate": 60 },
  "determinism": { "runs": 2, "hashEvery": 60, "expectedFinalHash": null },
  "assert": [ { "id": "alive",  "expr": "player.Health.current > 0", "always": true },
              { "id": "dies",   "expr": "g.EnemyAI.state == \"dead\"", "eventually": { "withinTicks": 600 } },
              { "id": "moved",  "expr": "player.Transform.position[0] > 2", "at": 60 },
              { "id": "clean",  "expr": "world.entities.all(e, !has(e.components.EnemyAI) || has(e.components.Collider2D))", "at": "end" } ] }
```

- **setup**: `entity` binds an entity that already exists in the world. `spawn` resolves a prefab and calls `PlayWorld::spawn` (deterministic v8 id, prefab tags merged). `position` is shorthand for Transform.position; `set` is a pointer map `{"/components/...": v}`. Setup runs once, before tick 0.
- **inputs**: `hold`/`axis` hold a value (default 1.0) during `[tick, untilTick)`; without `untilTick`, until the next `release` of the same action (or the end). `press` is 1.0 for that one tick. Action names are the keys of `Config/input.json`.
- **run**: `ticks`, `tickRate` (0/omitted = project.tickRate). `seed` omitted = project.seed.
- **determinism**: with `runs ≥ 2` the scenario is run again and the world hash is compared every `hashEvery` ticks. On divergence, run A is replayed up to that tick and the two snapshots are diffed per entity/path (`determinism.diff[]`, `firstDivergentSystem`); both snapshots are saved as artifacts. `expectedFinalHash` compares against a fixed value.
- **assert** semantics (§23.1): expressions are evaluated on the **frame snapshot after N ticks** (`world.tick == N`). `always` is checked every tick and aborts the run at the first violation (`abortedAt`; the remaining asserts are evaluated on the snapshot at the abort point and annotated). `eventually` passes once it is true anywhere inside the window and fails at the tick the window closes if it never was. `at: N` is checked once at that tick (failing if never reached). `at: "end"` (default) is the end of the run.
- **capture** assertions are reported as `errored` when there is no render layer (Phase 2).

**Discoverability (ADR-0039)**: `akeir schema test --json` (and `capabilities.testScenario`, MCP `schema_describe {kind:"test"}`) returns the scenario JSON Schema plus the expression reference below as JSON; `akeir test explain "<expr>" [--snapshot f --as player=<id>]` parses/evaluates one expression. Unknown functions are parse errors with a "did you mean".

## Expressions (§23.1 — "a comparator, not a language")

```
expr    := or ;  or := and ('||' and)* ;  and := rel ('&&' rel)*
rel     := add (('=='|'!='|'<'|'<='|'>'|'>='|'in') add)?
add     := mul (('+'|'-') mul)* ;  mul := unary (('*'|'/'|'%') unary)*
unary   := ('!'|'-') unary | postfix
postfix := primary ( '.' IDENT | '.' IDENT '(' args ')' | '[' expr ']' )*
primary := NUMBER | STRING | true | false | null | '[' args ']' | IDENT | IDENT '(' args ')' | '(' expr ')'
functions: has(path) size(x) abs(x) dist(a, b) min(a, b) max(a, b)      list macros: .all(e, p) .exists(e, p) .exists_one(e, p) .size()
```
- Bindings: `<as>` → that entity's `components` object (`player.Health.current`), `world` → the whole snapshot (`world.tick`, `world.entities[i].components.X`, `.name`, `.tags`).
- A missing member/binding is *undefined*; touching it with any operator outside `has()` is an **EvalError** — a typo never silently becomes false (forgetting the quotes in `goblin.EnemyAI.state == Dead` is an evaluation error). A despawned binding is null → `has(x.Health)` is false.
- Numbers compare as double; `==` is JSON value equality (1 == 1.0). Enum values are the reflection strings (`"chase"`, lowercase). string `in` string = substring, value `in` list, key `in` object.
- No loops, assignment or function definitions. Compatible with a CEL subset — for anything more, Luau (§61.1).

## Results (§24)

`TestRunner::runAll` writes `<resultsDir>/results.json` and, for every failed assertion, the snapshot of that tick as `<resultsDir>/artifacts/<test>/tick_NNNN.snapshot.json` (`snapshotOnFailure`). `failures[]` entries are `{assertId, expr, tick, expected, actual, note, bindings{path: value}, diagnostic{ruleId: TEST_ASSERTION_FAILED}}`. `junitXml()` uses testsuite = the file's directory (`Tests.Combat`), `<failure message="id @ tick N">`, and artifacts as `<system-out>[[ATTACHMENT|path]]</system-out>`.

## Tests

`Tests/Testing_Expr.cpp` (grammar, undefined, macros, errors, probe), `Tests/Testing_Runner.cpp` (passing scenario, failure reporting/abort/artifacts, expectedFinalHash, parse error → errored, diffSnapshots, discovering `Game/Tests` and passing all of them).

## Not implemented

- `events` (Screenshot/NamedEvent), `capture` assertions, `videoDriver: offscreen` — after the Phase 2 renderer.
- The `threads: [1, 8]` T1 check — the engine is single-threaded, so `threads` is always reported as 1.
- The `GAME_TEST_CONFIG` environment-variable mode, `akeir replay record` → inputs conversion (§22.3), `--fields` projection.
- An `apply` batch in setup (changing authoring data through Commands before the test) — when needed, it is one line applying `CommandBus` to a forked Project.
