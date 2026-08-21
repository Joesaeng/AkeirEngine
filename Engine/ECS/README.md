# Engine/ECS (`akeir_ecs`)

`PlayWorld` — projects the authoring documents into a **Flecs world + Box2D** and ticks it (§3.1, §88.2). The authoring JSON is the source of truth; this is a projection.

- `PlayWorld::build(project, worldId, cfg, diags)`: creates entities in id order, registers components with Flecs dynamically through reflection (size/align + ctor/dtor/copy/move hooks → `std::string` members are safe), hierarchy = ChildOf, RigidBody2D+Collider2D+Transform → a Box2D body. Prefab tags are merged into the instance.
- `tick(input, simTime)`: systems (in registration order) → `RigidBody2D.velocity` → physics step → Transform/velocity sync → contact events (sorted). Afterwards `currentTick()` = number of ticks run so far.
- `addSystem(name, fn)`, `addSpawnHook(name, fn)` (OnSpawn initialization; applied immediately to existing entities), `get<T>(id)`, `query({with}, {without})` (`#tag` supported, results in id order), `spawn` (deterministic v8 id, §7.1), `despawn`, `dumpEntity`, `snapshot` (§26.1), `hash`, `systemHashes`, `rng(name)`.
- Uses the Flecs C API only (`FLECS_NO_CPP`). Flecs REST/Explorer is not enabled yet (Phase 6a).

Determinism: entity iteration always goes through `ids_` (sorted) — never through Flecs table order. The hash covers the reflected component values (float bit patterns) in id order + RNG state + physics.

## Not implemented
- Flecs queries/system pipeline (currently a linear scan — enough for a few hundred entities), snapshot restore (§26.1 defines it as world re-creation), REST, a runtime reparent API.
