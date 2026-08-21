# Game/Source (`pme_game`) — 샘플 게임 "TestArena"

AI 가 가장 자주 수정하는 영역 (§60). 엔진은 이 디렉터리를 모른다 (§76).

- `GameComponents.h/.cpp` : `Health`, `Movement`, `PlayerController`, `EnemyAI` (+ `AiState` enum). 전부 aggregate, `PME_REFLECT_*` 로 등록. `registerGameComponents()` 앵커.
- `GameSystems.h/.cpp` : `registerGameSystems(world)` — spawn hook `HealthInit`(current = max), systems `PlayerMovement`(MoveX/MoveY → velocity) → `EnemyChase`(가장 가까운 `#player` 추적, detection/attack range) → `EnemyAttack`(cooldown, damage, dead).
- 엔진 API 는 `PlayWorld` 의 `query / get<T> / rng / contactEvents` 만 쓴다 (§59). wall-clock·전역 RNG·unordered 순회 금지 (§22.2). 동률은 id 순으로 결정한다.

데이터: `Game/project.json`, `Game/Prefabs/*.prefab.json`, `Game/Worlds/TestArena.world.json`, `Game/Config/input.json`. `game validate` / `game fmt` / `game run --headless` / `game query` / `game dump` 가 `Game/` 안에서(또는 `--project Game`) 동작한다.

실행 파일/테스트는 이 라이브러리를 `$<LINK_LIBRARY:WHOLE_ARCHIVE,pme_game>` 로 링크한다 (component 등록 보존).

## 새 component / system 을 추가하려면
1. `GameComponents.h` 에 aggregate struct, `GameComponents.cpp` 에 `PME_REFLECT_BEGIN … END` + `registerGameComponents()` 에 앵커 한 줄.
2. system 은 `GameSystems.cpp` 에 함수로 쓰고 `registerGameSystems` 에 순서대로 `addSystem`.
3. prefab JSON 에 component 를 넣고 `game validate` → `game run --headless --ticks N --json` 으로 확인.
