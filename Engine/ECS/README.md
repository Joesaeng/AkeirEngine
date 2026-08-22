# Engine/ECS (`akeir_ecs`)

`PlayWorld` — projects the authoring documents into a **Flecs world + Box2D** and ticks it (§3.1, §88.2). The authoring JSON is the source of truth; this is a projection.

- `PlayWorld::build(project, worldId, cfg, diags)`: creates entities in id order, registers components with Flecs dynamically through reflection (size/align + ctor/dtor/copy/move hooks → `std::string` members are safe), hierarchy = ChildOf, RigidBody2D+Collider2D+Transform → a Box2D body. Prefab tags are merged into the instance.
- `tick(input, simTime)`: **PrePhysics** systems (registration order) → `RigidBody2D.velocity` → physics step → Transform/velocity sync → contact events (sorted) → **PostPhysics** systems, which see this tick's `contactEvents()` (ADR-0038). Afterwards `currentTick()` = number of ticks run so far.
- `addSystem(name, fn, phase = PrePhysics)`, `addSpawnHook(name, fn)` (OnSpawn initialization; applied immediately to existing entities), `get<T>(id)`, `query({with}, {without})` (`#tag` supported, results in id order), `spawn(name, components, tags, parent, &error)` (deterministic v8 id, §7.1; an unknown component name refuses the spawn), **`spawnPrefab(idOrName, overrides, tags, parent, name, &error)`** (authoring prefabs resolved at build time — `prefabs()`; overrides are a JSON Pointer map like a test's `set`), `despawn`, **`addComponent` / `removeComponent` / `addTag` / `removeTag`** (immediate; physics bodies follow RigidBody2D+Collider2D), `dumpEntity`, `snapshot` (§26.1), `hash`, `systemHashes`, `rng(name)`.
- `bodyHandle(id)` / `entityForBody(handle)` map contact events back to entities (ADR-0042). Stale Box2D end events for bodies despawned during a contact are dropped by the physics layer.
- `physicsLayers()` — the project's collision matrix as applied to bodies (ADR-0043).
- `profile()` / `profileJson()` / `resetProfile()` — always-on counters: per-system ms, physics ms, contact and query counts (ADR-0044; wall clock, never read by the simulation).
- Uses the Flecs C API only (`FLECS_NO_CPP`). Flecs REST/Explorer is not enabled yet (Phase 6a).

Determinism: entity iteration always goes through `ids_` (sorted) — never through Flecs table order. The hash covers the reflected component values (float bit patterns) in id order + RNG state + physics.

## Not implemented
- Flecs queries/system pipeline (currently a linear scan — enough for a few hundred entities), snapshot restore (§26.1 defines it as world re-creation), REST, a runtime reparent API.
