# Engine/Commands (`akeir_commands`)

**The project's only write path.** CLI, Editor, MCP and `validate --fix` all go through `CommandBus`, and every result is recorded as a `ChangeSet` (§78).
Design doc §8 (CommandBus / CommandKind / ChangeBuilder), §8.1 (command ids), §9 (transactions, commit procedure), §10 (undo/history), §49 (apply), §50 (dry-run), §78 (ChangeSet), §78.1 (command → ops mapping).

```
Tools (CLI / Editor / MCP)
   │  execute(id, args) · apply(batch) · undo/redo · beginTx/commitTx
   ▼
CommandBus ──▶ fork (copy of Project) ──▶ handler(CommandContext) ──▶ ChangeBuilder (ops, `before` filled automatically) ──▶ validateFork (rejects new errors only)
   │                                                                                                                         │
   │  dryRun ─────────────────────────────────────────────────────────────────────────────────────────────────────────────────┘→ envelope.changes (nothing saved)
   ▼
commit(cs): base check → Cache/journal/<cs>.json → applyOps on the project → touched documents written canonical via temp+rename → Cache/history/history.jsonl → journal removed
```

## Files

| File | § | Contents |
|---|---|---|
| `include/akeir/commands/ChangeSet.h`, `src/ChangeSet.cpp` | §78, §10.1, §9.2 | `ChangeOp{op, doc, path, from, value, before, blob, beforeBlob}`, `ChangeSet{id, tx, actor, createdAt, intent, base, ops, touched, lossy, diagnostics}`, `toJson/fromJson`, `inverse()`, `compose()`, `applyOps(docs, ops)`, `escapeToken/unescapeToken` |
| `include/akeir/commands/History.h`, `src/History.cpp` | §9.2, §10, §49 | `Cache/history/history.jsonl` (append-only) + `cursor.json` + `idempotency.json`, `Cache/journal/<cs>.json` (write-ahead) |
| `include/akeir/commands/CommandBus.h`, `src/CommandBus.cpp` | §8, §9, §10, §49, §50 | `CommandKind`, `ChangeBuilder`, `CommandContext` (fail/resolveEntity/resolvePrefab/resolveWorldDoc), `CommandDef`, `CommandBus` (execute/apply/tx/undo/redo/recoverJournal), `nextOrderKey` |
| `src/BuiltinCommands.cpp` | §8, §34, §78.1 | the 14 built-in Mutation commands + a JSON Schema for each one's args |

## Command list (`akeir capabilities --json` → `result.busCommands[]`)

| id | args | notes |
|---|---|---|
| `entity.create` | `{world?, name, parent?, order?, tags?, components?, prefab?, set?}` | Adds Transform when missing; missing props get defaults. With `prefab` it creates an instance |
| `entity.delete` | `{entity, recursive?=true}` | Includes descendants. The inverse restores parents first |
| `entity.rename` / `entity.reparent` | `{entity, name}` / `{entity, parent\|null, order?}` | reparent rejects cycles (`HIERARCHY_CYCLE`) |
| `component.add` / `component.remove` | `{entity, component, value?}` / `{entity, component}` | **`entity` also accepts a prefab selector** (editing a prefab = every instance). remove is rejected while dependent components exist (`COMPONENT_DEPENDENCY`); Transform cannot be removed |
| `property.set` | `{entity, component, path, value}` | `path` = a JSON Pointer inside the component (`max`, `/position/0`). Type/range/enum validated through reflection. runtimeOnly/readOnly are rejected |
| `tag.add` / `tag.remove` | `{entity, tag}` | |
| `prefab.create` | `{name, components?, base?, set?, add?, remove?, tags?}` | creates `Prefabs/<Name>.prefab.json` |
| `prefab.instantiate` | `{prefab, world?, name?, parent?, position?, set?, tags?}` | sugar over `entity.create` |
| `world.create` | `{name}` | `Worlds/<Name>.world.json` (empty entities) |
| `asset.import` | `{source, grid?, names?, pixelsPerUnit?, filter?, pivot?, id?}` | Creates `Assets/<png>.meta.json` (§37, ADR-0037) with a generated `asset_` id; grid → one sprite per cell (row-major, named), no grid → one whole-image sprite |
| `document.patch` | `{doc, ops:[RFC 6902]}` | Raw-edit escape hatch. `validate --fix` routes its `artifactChanges` through here. `copy` is not allowed |

Selectors follow §7.4: id / id prefix / `name:X` / `path:World/Parent/Child`. Ambiguity → `AMBIGUOUS_SELECTOR` + candidates.

## Editing rules for instances and derived prefabs (§78.1)

When the target is a prefab instance (`prefab` key) or a derived prefab (`base` key), component/property commands only edit the document's **`set`/`add`/`remove` maps**:

| command | target exists in base | target is in the `add` map | absent |
|---|---|---|---|
| `property.set` | `set["/components/C/p"] = v` — **if equal to the base value, the set entry is deleted** (`override: "inherited"`) | edits `add["/components/C"]` in place (`override: "add"`) | `COMPONENT_NOT_ON_ENTITY` |
| `component.add` | `COMPONENT_EXISTS` (but if it is listed in `remove`, it is taken out of there and the value goes to `set`) | `COMPONENT_EXISTS` | `add["/components/C"] = defaults+value` |
| `component.remove` | `remove[] += "/components/C"` + cleans that component's `set` entries | removed from `add` | `COMPONENT_NOT_ON_ENTITY` |

## Execution model details

- **fork**: `Project` is a value type, so it is copied. Handlers change only the fork, through its `ChangeBuilder` — the builder applies each op immediately and `reindex()`es, so a handler sees its own changes through `locate()`/`resolveSelector()` right away.
- **ChangeBuilder rules**: `set()` is replace when the path exists, add when it does not, and emits no op for an equal value (an all-no-op command does not commit). `remove/replace/move` fill `before` from the current value. `"/arr/-"` (append) cannot be inverted, so it is **recorded with the concrete index**.
- **Validation before commit**: `validateFork()` runs `Project::validate()` on the fork and rejects only **newly introduced errors** (fingerprints absent from the baseline) → `VALIDATION_FAILED` + `details.diagnostics`. Disable with `ExecOptions.validateAfter=false` (CLI `--no-validate`). `validate --fix` disables it (it is in the middle of fixing errors).
- **Base check (optimistic concurrency)**: when the bus is created it remembers `sha256(JCS(canonical(doc)))` per document; at commit it re-reads the file on disk and compares → `BASE_MISMATCH` (conflict) when different. Recorded in `ChangeSet.base`.
- **`before` comparison**: `applyOps` compares `before` with the current value via JCS (key order ignored), because a document that went through disk comes back reordered per §5.3 and `ordered_json ==` would fail.
- **history**: a linear stack + cursor. undo = apply `inverse(ops)` through the commit path and cursor--, redo = re-apply the original ops and cursor++ (neither pushes to history). A new commit drops the redo tail (the only time history.jsonl is rewritten). With `--actor X`, `UNDO_ACTOR_MISMATCH` if the latest entry belongs to a different actor.
- **tx**: `beginTx(ttlMs)` takes a fork and returns an opaque handle with a TTL; `execute(…, {tx})` accumulates on it, `commitTx()` produces one history entry via `ChangeSet::compose`. Expired/unknown handles → `TX_UNKNOWN_OR_EXPIRED` (§9.1). `txInfo/txList/expireTransactions`. The one-shot CLI uses tx only inside `apply`; `akeir serve` uses it across CLI calls.
- **apply (§49)**: `{atomic?=true, dryRun?, idempotencyKey?, changes:[{op, ...args, as?}]}`. `"$name"` → that change's `result.id` (or the whole result when there is no id), `"$name.field"` → a field, `"$$x"` → the literal. atomic runs everything in one tx; on failure `details.index/op/results`. An idempotencyKey stores the response in `Cache/history/idempotency.json` and returns it without re-executing (`meta.idempotentReplay`).
- **Journal recovery (§9.2)**: `recoverJournal()` — if every touched document of a leftover journal still has the base hash, the ops are re-applied and saved (`completed`); if all of them differ, the write is assumed done and only history is appended (`already-applied`); mixed → left as `partial`. The CLI calls this before every write command.
- **`persist=false`** (`BusOptions`) writes no files, journal or history (tests, schema listing for capabilities).

## Error codes (by category)

`ARG_REQUIRED`/`ARG_TYPE`/`AMBIGUOUS_SELECTOR`/`WORLD_REQUIRED`/`PARENT_IN_OTHER_WORLD` (usage) · `ENTITY_NOT_FOUND`/`PREFAB_NOT_FOUND`/`WORLD_NOT_FOUND`/`COMPONENT_UNKNOWN`/`COMPONENT_NOT_ON_ENTITY`/`PROPERTY_UNKNOWN`/`TAG_NOT_ON_ENTITY`/`TX_UNKNOWN_OR_EXPIRED`/`DOCUMENT_NOT_FOUND` (not_found) · `COMPONENT_EXISTS`/`DOCUMENT_EXISTS`/`BASE_MISMATCH`/`UNDO_CONFLICT`/`REDO_CONFLICT`/`UNDO_ACTOR_MISMATCH`/`CHANGESET_APPLY_FAILED` (conflict) · `PROPERTY_RUNTIME_ONLY`/`PROPERTY_READ_ONLY`/`COMPONENT_REQUIRED`/`COMPONENT_DEPENDENCY`/`HIERARCHY_CYCLE`/`ENTITY_HAS_CHILDREN`/`NOTHING_TO_UNDO`/`NOTHING_TO_REDO` (precondition) · `VALIDATION_FAILED` + the reflection rules (`PROPERTY_TYPE_MISMATCH`, `PROPERTY_OUT_OF_RANGE`, `ENUM_VALUE_INVALID` …) (validation) · `APPLY_INVALID`/`APPLY_BAD_REFERENCE` (usage) · `JOURNAL_WRITE_FAILED`/`SAVE_FAILED`/`HISTORY_WRITE_FAILED`/`INTERNAL_ERROR` (internal).

## Tests

`Tests/Commands_ChangeSet.cpp` (ops/inverse/compose/escape), `Tests/Commands_Bus.cpp` (copies the sample Game/ to a temp directory: commit, canonical files, byte-identical undo, history persistence, actor filter, BASE_MISMATCH, instance overrides, prefab editing, entity/prefab/world creation, dry-run, validation rejection, tx, apply, idempotency, journal recovery).

## Not implemented / next

- The `file.*` op (binary assets, §78 rule 4) — `applyOps` rejects it until an asset store exists (Phase 2+).
- `project.json` lives outside the document map and cannot be changed by a command (`defaultWorld` changes etc. → Phase 4).
- `--if-match <cs>` (commit a dry-run result as-is, §50), checkpoints (§52), semantic diff output (§51), rename tables/migration (§53).
- Pre-validating `apply`'s changes[] against the `busCommands[].args` schemas (handlers check directly for now).
- Query-kind commands (`entity.get`, `query`) still have the CLI call Project/PlayWorld directly — moving them to the bus's `CommandKind::Query` goes together with `akeir serve`.
