# Engine/ECS (`pme_ecs`)

`PlayWorld` — authoring 문서를 **Flecs world + Box2D** 로 투영하고 tick 한다 (§3.1, §88.2). authoring JSON 이 source of truth, 이것은 투영이다.

- `PlayWorld::build(project, worldId, cfg, diags)` : entity 를 id 순으로 생성, component 는 reflection 으로 Flecs 에 동적 등록(size/align + ctor/dtor/copy/move 훅 → std::string 멤버 안전), 계층 = ChildOf, RigidBody2D+Collider2D+Transform 이면 Box2D body. prefab 의 tags 는 인스턴스에 병합.
- `tick(input, simTime)` : systems(등록 순) → `RigidBody2D.velocity` → physics step → Transform/velocity 동기화 → contact 이벤트(정렬). 끝나면 `currentTick()` = 지금까지 돌린 tick 수.
- `addSystem(name, fn)`, `addSpawnHook(name, fn)`(OnSpawn 초기화; 기존 entity 에 즉시 적용), `get<T>(id)`, `query({with}, {without})`(`#tag` 지원, 결과는 id 순), `spawn`(결정적 v8 id, §7.1), `despawn`, `dumpEntity`, `snapshot`(§26.1), `hash`, `systemHashes`, `rng(name)`.
- Flecs C API 만 사용 (`FLECS_NO_CPP`). Flecs REST/Explorer 는 아직 켜지 않았다 (Phase 6a).

결정론: entity 순회는 항상 `ids_`(정렬) — Flecs 테이블 순서에 의존하지 않는다. hash 는 id 순 component reflected 값(float bit pattern) + rng 상태 + physics.

## 미구현
- Flecs 쿼리/시스템 파이프라인(지금은 선형 스캔 — 수백 entity 규모면 충분), snapshot 복원(§26.1: world 재생성으로 정의), REST, 계층 변경(reparent) 런타임 API.
