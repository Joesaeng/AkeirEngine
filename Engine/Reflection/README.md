# Engine/Reflection (`akeir_reflection`)

The single-source-of-truth metadata (§42.2 `PropertyMeta`) and the runtime registry. Serializer / Inspector / JSON Schema / CLI help / MCP schema / Validation all read this one table and nothing else (§43).

| Header | § | Provides |
|---|---|---|
| `akeir/reflection/PropertyMeta.h` | §42.2, §43.1, §88.8 | `PropFlags`, `PropType`, `Visibility` + `isVisible()` (the only definition), `PropertyMeta` (get/set accessors, `toSchema`), `ComponentMeta` (`toSchema`, `toWireFormat`, `defaultJson`, ctor/dtor/copy/move hooks) |
| `akeir/reflection/Registry.h` | §42.2, §14.1 | `Registry::global()` (lookup by name / typeid, `all()` sorted by name), `getByPointer` / `setByPointer` (`/position/0`), `splitPropertyPointer`, `allSchemas` |
| `akeir/reflection/Reflect.h` | §42.2 | `AKEIR_REFLECT_BEGIN/PROP/SKIP/REQUIRES/LIFECYCLE/VERSION/END`, `AKEIR_REFLECT_ENUM`, `PropTraits<T>` (bool/int/float/double/string/enum/Vec2-4/Quat/Color/Ref), `ComponentBuilder` (`check()`, `finishInto(registry)`), `PropBuilder` |
| `akeir/reflection/Aggregate.h` | ADR-0035 | `aggregateArity<T>()` — compile-time member count of an aggregate (nested aggregates count as one) |

## Registration example

```cpp
struct Health { float max = 100.f; float current = 100.f; };      // aggregate!
AKEIR_REFLECT_BEGIN(Health, "Hit points and death state")
    AKEIR_REQUIRES("Transform");
    AKEIR_LIFECYCLE("OnSpawn", "after EnemyAttack", "OnDespawn");
    AKEIR_PROP(max, "Maximum hit points").min(1).unit("hp").ui(1, 1000, 1).required();
    AKEIR_PROP(current, "Current hit points").runtimeOnly().readOnly().save();
AKEIR_REFLECT_END(Health)          // repeats the type name (token pasting)

enum class Shape { Box, Circle };
AKEIR_REFLECT_ENUM(Shape, "box", "circle")   // at global scope. Value = declaration index; JSON carries the string
```

## Rules / pitfalls
- A component must be an **aggregate** (`static_assert`). Names are PascalCase, properties camelCase. No nested structs — Vec/Quat/Color/Ref are the leaves.
- **Every data member must be listed** (ADR-0035): `AKEIR_PROP` to reflect it, or `AKEIR_SKIP(member, "reason")` to leave it out on purpose (the exclusion shows up in the schema as `x-skipped`). `ComponentBuilder` counts the struct's members with `aggregateArity<T>()`; a mismatch is recorded as `REFLECT_MEMBER_UNLISTED` in `Registry::global().diagnostics()`, which `Project::validate()` includes (so `akeir validate` and CI fail) and `Tests/Reflection_Registry.cpp` asserts empty. Without this, a forgotten `AKEIR_PROP` silently dropped the member from JSON, schema, set/undo, query, snapshot and finalHash.
- `AKEIR_REFLECT_BEGIN(Type, …)` takes the **unqualified name** and is written inside the type's namespace (`namespace game { AKEIR_REFLECT_BEGIN(Health, …) … }`). `AKEIR_REFLECT_ENUM` goes at global scope with the qualified name.
- Registration happens in a static initializer. **A static library that only contains components must be linked with `$<LINK_LIBRARY:WHOLE_ARCHIVE,lib>`**, or you call a `registerXxxComponents()` anchor function (see `Engine/Runtime/Components.cpp`, `Game/Source/GameComponents.cpp`).
- `requires` is a C++20 keyword → use `requiresComponent()` / `requiresComponents`.
- Do not name anything `toString(Enum)` (doctest ADL) → `propTypeName()`.
- We use our own registry instead of EnTT meta (Flecs was adopted, ADR-0002). Swapping the front-end for C++26 annotations leaves `PropertyMeta` consumers untouched (§42.2 backend replacement plan).

## Not implemented
- Array/list properties, nested structs, sub-asset existence checks for `Ref` (§88.7) — Phase 3+.
