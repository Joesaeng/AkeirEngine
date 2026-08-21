# Engine/Reflection (`akeir_reflection`)

단일 진실 원천 메타데이터(§42.2 `PropertyMeta`)와 runtime registry. Serializer / Inspector / JSON Schema / CLI help / MCP schema / Validation 이 전부 이 테이블만 본다 (§43).

| 헤더 | § | 제공 |
|---|---|---|
| `akeir/reflection/PropertyMeta.h` | §42.2, §43.1, §88.8 | `PropFlags`, `PropType`, `Visibility` + `isVisible()`(유일한 정의), `PropertyMeta`(get/set 접근자, `toSchema`), `ComponentMeta`(`toSchema`, `toWireFormat`, `defaultJson`, ctor/dtor/copy/move 훅) |
| `akeir/reflection/Registry.h` | §42.2, §14.1 | `Registry::global()` (이름/typeid 조회, 이름순 `all()`), `getByPointer` / `setByPointer` (`/position/0`), `splitPropertyPointer`, `allSchemas` |
| `akeir/reflection/Reflect.h` | §42.2 | `AKEIR_REFLECT_BEGIN/PROP/REQUIRES/LIFECYCLE/VERSION/END`, `AKEIR_REFLECT_ENUM`, `PropTraits<T>` (bool/int/float/double/string/enum/Vec2-4/Quat/Color/Ref), `ComponentBuilder`, `PropBuilder` |

## 등록 예

```cpp
struct Health { float max = 100.f; float current = 100.f; };      // aggregate!
AKEIR_REFLECT_BEGIN(Health, "Hit points and death state")
    AKEIR_REQUIRES("Transform");
    AKEIR_LIFECYCLE("OnSpawn", "after EnemyAttack", "OnDespawn");
    AKEIR_PROP(max, "Maximum hit points").min(1).unit("hp").ui(1, 1000, 1).required();
    AKEIR_PROP(current, "Current hit points").runtimeOnly().readOnly().save();
AKEIR_REFLECT_END(Health)          // 타입 이름을 다시 준다 (토큰 결합)

enum class Shape { Box, Circle };
AKEIR_REFLECT_ENUM(Shape, "box", "circle")   // 전역 네임스페이스에서. 값 = 선언 순서 index, JSON 은 문자열
```

## 규칙 / 함정
- component 는 **aggregate** (`static_assert`). 이름은 PascalCase, property 는 camelCase. 중첩 struct 는 안 됨 — Vec/Quat/Color/Ref 가 leaf.
- `AKEIR_REFLECT_BEGIN(Type, …)` 은 **unqualified 이름**으로, 그 타입의 네임스페이스 안에서 쓴다 (`namespace game { AKEIR_REFLECT_BEGIN(Health, …) … }`). `AKEIR_REFLECT_ENUM` 은 전역에서 qualified 이름으로.
- 등록은 static initializer. **component 만 담긴 정적 라이브러리는 `$<LINK_LIBRARY:WHOLE_ARCHIVE,lib>` 로 링크**하거나 `registerXxxComponents()` 앵커 함수를 호출한다 (`Engine/Runtime/Components.cpp`, `Game/Source/GameComponents.cpp` 참고).
- `requires` 는 C++20 키워드 → `requiresComponent()` / `requiresComponents`.
- `toString(Enum)` 이름 금지 (doctest ADL) → `propTypeName()`.
- EnTT meta 대신 자체 registry 를 쓴다 (Flecs 채택, ADR-0002). C++26 annotation 으로 front-end 를 바꿔도 `PropertyMeta` 소비자는 그대로 (§42.2 backend 교체 계획).

## 미구현
- 배열/리스트 property, 중첩 struct, Ref 의 sub-asset 존재 검증(§88.7)은 Phase 3+.
