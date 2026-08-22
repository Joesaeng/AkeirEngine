# Engine/Runtime (`akeir_runtime`)

The authoring document model, the built-in components, and the fixed-tick loop.

| Header | § | Provides |
|---|---|---|
| `akeir/runtime/Project.h` | §5.3, §6, §7, §19, §29, §34, §88.9 | `Project::load/create`, the document map (`Worlds/*.world.json`, `Prefabs/*.prefab.json`), id index (`locate`), `resolveSelector` (`name:` / `path:` / id prefix), `entityPath`, `resolvePrefab` / `resolveEntityComponents` (remove → add → set, absent = inherit, cycle/depth checks), `canonicalizeDocument`, `saveAll` / `saveDocument`, `validate()` |
| `akeir/runtime/Components.h` | §2.1, §6, §57 | Built-in components: `Transform`, `SpriteRenderer`, `Collider2D`, `RigidBody2D`, `Camera2D` + the `registerBuiltinComponents()` anchor |
| `akeir/runtime/Application.h` | §20.1, §22.2 | `ISimulation`, `IInputSource` (Null/Scripted), `RunConfig`, `RunResult`, `Application::runHeadless` (fixed tick without an accumulator, FP env normalization, hashEvery) |
| `akeir/runtime/Input.h` | §88.3, §22.3 | `InputFrame {tick, actions, commands}` + JSON |
| `akeir/runtime/DemoSimulation.h` | — | Placeholder sim for exercising the loop (`akeir run --demo`) |
| `akeir/runtime/Assets.h` | §37, §88.7, ADR-0037 | `AssetMeta` / `SpriteRegion` / `AssetTable` (parsed from `Assets/**/*.meta.json` documents; `resolveSprite("asset_…#sprites/x")`), `pngDimensions` |

## Document format (§6)

world:
`{ $schema: "game://schema/world/1", schemaVersion, id: "world_…", name, entities: { "entity_…": { name, parent, order, tags?, components | prefab + set/add/remove } } }`

prefab:
`{ $schema: "game://schema/prefab/1", schemaVersion, id: "prefab_…", name, tags, components | base + set/add/remove }`

Override paths are JSON Pointers into the resolved base (`/components/Health/max`). `set` requires the target to exist, `add` requires it not to. Application order: remove → add → set.

## ruleIds emitted by validate()

REFLECT_MEMBER_UNLISTED (from the reflection registry, ADR-0035 — a component member that is neither AKEIR_PROP nor AKEIR_SKIP), JSON_PARSE_ERROR, DOCUMENT_NOT_OBJECT, SCHEMA_VERSION_NEWER_THAN_ENGINE, DOCUMENT_SCHEMA_MISMATCH, ID_FORMAT_INVALID, ID_PREFIX_MISMATCH, DUPLICATE_PERSISTENT_ID, WORLD_ENTITIES_MISSING, ENTITY_NOT_OBJECT, PARENT_NOT_FOUND, HIERARCHY_CYCLE, ENTITY_EMPTY (note), PREFAB_COMPONENTS_MISSING, PREFAB_NOT_FOUND, PREFAB_CHAIN_CYCLE, PREFAB_CHAIN_TOO_DEEP, PREFAB_OVERRIDE_INVALID, PREFAB_OVERRIDE_TARGET_MISSING, PREFAB_OVERRIDE_TARGET_EXISTS, COMPONENTS_NOT_OBJECT, COMPONENT_UNKNOWN, COMPONENT_DEPENDENCY_MISSING (fix: component.add), REF_DANGLING, JSON_NOT_CANONICAL (warning, fix: project.fmt), the asset sidecar rules ASSET_META_INVALID / ASSET_SOURCE_MISSING / ASSET_SOURCE_INVALID / ASSET_SUBASSET_RECT_INVALID / ASSET_SUBASSET_MISSING (ADR-0037), plus the codes from Serialization/ComponentJson.

## Not implemented
- Loading Data/ and Config/ as documents (Phase 3+), splitting file granularity (§88.9), `akeir id fix`. Asset sidecars are loaded (ADR-0037) but only the Texture2D importer exists.
