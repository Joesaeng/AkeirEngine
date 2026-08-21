# Engine/Runtime (`pme_runtime`)

authoring 문서 모델, 내장 component, 고정 tick 루프.

| 헤더 | § | 제공 |
|---|---|---|
| `pme/runtime/Project.h` | §5.3, §6, §7, §19, §29, §34, §88.9 | `Project::load/create`, 문서 맵(`Worlds/*.world.json`, `Prefabs/*.prefab.json`), id index (`locate`), `resolveSelector`(`name:` / `path:` / id prefix), `entityPath`, `resolvePrefab` / `resolveEntityComponents`(remove → add → set, absent = inherit, cycle/depth 검사), `canonicalizeDocument`, `saveAll` / `saveDocument`, `validate()` |
| `pme/runtime/Components.h` | §2.1, §6, §57 | 내장 component: `Transform`, `SpriteRenderer`, `Collider2D`, `RigidBody2D`, `Camera2D` + `registerBuiltinComponents()` 앵커 |
| `pme/runtime/Application.h` | §20.1, §22.2 | `ISimulation`, `IInputSource`(Null/Scripted), `RunConfig`, `RunResult`, `Application::runHeadless` (accumulator 없는 고정 tick, FP env normalize, hashEvery) |
| `pme/runtime/Input.h` | §88.3, §22.3 | `InputFrame {tick, actions, commands}` + JSON |
| `pme/runtime/DemoSimulation.h` | — | 루프 검증용 placeholder sim (`akeir run --demo`) |

## 문서 형식 (§6)

world:
`{ $schema: "game://schema/world/1", schemaVersion, id: "world_…", name, entities: { "entity_…": { name, parent, order, tags?, components | prefab + set/add/remove } } }`

prefab:
`{ $schema: "game://schema/prefab/1", schemaVersion, id: "prefab_…", name, tags, components | base + set/add/remove }`

override 경로는 resolved base 에 대한 JSON Pointer (`/components/Health/max`). `set` 은 대상이 존재해야, `add` 는 없어야 한다. 적용 순서 remove → add → set.

## validate() 가 내는 ruleId

JSON_PARSE_ERROR, DOCUMENT_NOT_OBJECT, SCHEMA_VERSION_NEWER_THAN_ENGINE, DOCUMENT_SCHEMA_MISMATCH, ID_FORMAT_INVALID, ID_PREFIX_MISMATCH, DUPLICATE_PERSISTENT_ID, WORLD_ENTITIES_MISSING, ENTITY_NOT_OBJECT, PARENT_NOT_FOUND, HIERARCHY_CYCLE, ENTITY_EMPTY(note), PREFAB_COMPONENTS_MISSING, PREFAB_NOT_FOUND, PREFAB_CHAIN_CYCLE, PREFAB_CHAIN_TOO_DEEP, PREFAB_OVERRIDE_INVALID, PREFAB_OVERRIDE_TARGET_MISSING, PREFAB_OVERRIDE_TARGET_EXISTS, COMPONENTS_NOT_OBJECT, COMPONENT_UNKNOWN, COMPONENT_DEPENDENCY_MISSING(fix: component.add), REF_DANGLING, JSON_NOT_CANONICAL(warning, fix: project.fmt) + Serialization/ComponentJson 의 코드들.

## 미구현
- Data/, Config/, Assets/*.meta.json 로드 (Phase 3+), 파일 granularity 분할(§88.9), `akeir id fix`.
