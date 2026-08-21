# Engine/Testing (`pme_testing`)

데이터화된 테스트 시나리오(§23) 러너와 assertion 표현식(§23.1). `game test` 가 쓴다. Game/ 을 모른다 — world 를 만드는 `WorldFactory`(system 등록 포함)를 호출자가 준다.

## 파일

| 파일 | § | 내용 |
|---|---|---|
| `include/pme/testing/Expr.h`, `src/Expr.cpp` | §23.1 | 고정 비교 문법: 토크나이저 → 재귀 하강 파서 → AST 평가. `Expr::parse / eval / evalBool / probeBindings / roots` |
| `include/pme/testing/TestRunner.h`, `src/TestRunner.cpp` | §23, §24, §22.2 | `TestScenario::fromJson`, `TestRunner::discover / run / runAll / diffSnapshots`, `TestReport::toJson / junitXml` |

## 시나리오 파일 (`<project>/Tests/**/*.test.json`)

```json
{ "$schema": "game://schema/test/1", "name": "GoblinBasicCombat", "world": "<id|name:X>  (생략 = defaultWorld)", "seed": 1024,
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

- **setup**: `entity` 는 world 에 이미 있는 entity 를 binding 한다. `spawn` 은 prefab 을 resolve 해서 `PlayWorld::spawn` (결정적 v8 id, prefab tags 병합). `position` 은 Transform.position 단축, `set` 은 `{"/components/...": v}` 포인터 맵. setup 은 tick 0 전에 한 번.
- **inputs**: `hold`/`axis` 는 `[tick, untilTick)` 동안 값(기본 1.0), `untilTick` 이 없으면 같은 action 의 다음 `release` 까지(없으면 끝까지). `press` 는 그 tick 한 번 1.0. action 이름은 `Config/input.json` 의 키.
- **run**: `ticks`, `tickRate`(0/생략 = project.tickRate). `seed` 생략 = project.seed.
- **determinism**: `runs ≥ 2` 면 같은 시나리오를 한 번 더 돌려 `hashEvery` 마다 world hash 비교. 어긋나면 그 tick 까지 A 를 다시 돌려 두 snapshot 을 entity/path 단위로 diff (`determinism.diff[]`, `firstDivergentSystem`), 두 snapshot 을 artifact 로 저장. `expectedFinalHash` 가 있으면 고정값과 비교.
- **assert** 의미론 (§23.1): 표현식은 **N tick 을 돌린 뒤의 frame snapshot** 위에서 평가된다(`world.tick == N`). `always` 매 tick, 첫 위반에서 run 중단(`abortedAt`, 나머지 assert 는 중단 시점 snapshot 으로 평가하고 note 를 단다). `eventually` 는 창 안에서 한 번 참이면 통과, 끝까지 거짓이면 창이 닫히는 tick 에 실패. `at: N` 은 그 tick 한 번(도달 못 하면 실패). `at: "end"`(기본) 는 run 끝.
- **capture** assertion 은 렌더 레이어(Phase 2)가 없어 `errored` 로 보고한다.

## 표현식 (§23.1 "언어가 아니라 비교기")

```
expr    := or ;  or := and ('||' and)* ;  and := rel ('&&' rel)*
rel     := add (('=='|'!='|'<'|'<='|'>'|'>='|'in') add)?
add     := mul (('+'|'-') mul)* ;  mul := unary (('*'|'/'|'%') unary)*
unary   := ('!'|'-') unary | postfix
postfix := primary ( '.' IDENT | '.' IDENT '(' args ')' | '[' expr ']' )*
primary := NUMBER | STRING | true | false | null | '[' args ']' | IDENT | IDENT '(' args ')' | '(' expr ')'
함수: has(path) size(x) abs(x) dist(a, b) min(a, b) max(a, b)      리스트 매크로: .all(e, p) .exists(e, p) .exists_one(e, p) .size()
```
- 바인딩: `<as>` → 그 entity 의 `components` 객체 (`player.Health.current`), `world` → snapshot 전체 (`world.tick`, `world.entities[i].components.X`, `.name`, `.tags`).
- 없는 멤버/바인딩은 *undefined* 이고, `has()` 밖에서 연산에 닿으면 **EvalError(오류)** 다 — 오타가 조용히 false 가 되지 않는다 (`goblin.EnemyAI.state == Dead` 처럼 따옴표를 빠뜨리면 평가 오류). despawn 된 binding 은 null → `has(x.Health)` 가 false.
- 숫자 비교는 double, `==` 는 JSON 값 동등(1 == 1.0). enum 값은 reflection 문자열(`"chase"`, 소문자). 문자열 `in` 문자열 = 부분 문자열, 값 `in` 리스트, 키 `in` 객체.
- 루프·대입·함수 정의 없음. CEL 부분집합과 호환 — 더 필요하면 Luau (§61.1).

## 결과 (§24)

`TestRunner::runAll` 은 `<resultsDir>/results.json` 을 쓰고, 실패한 assertion 마다 그 tick 의 snapshot 을 `<resultsDir>/artifacts/<test>/tick_NNNN.snapshot.json` 로 남긴다(`snapshotOnFailure`). `failures[]` 는 `{assertId, expr, tick, expected, actual, note, bindings{path: value}, diagnostic{ruleId: TEST_ASSERTION_FAILED}}`. `junitXml()` 은 testsuite = 파일 디렉터리(`Tests.Combat`), `<failure message="id @ tick N">`, artifact 는 `<system-out>[[ATTACHMENT|path]]</system-out>`.

## 테스트

`Tests/Testing_Expr.cpp`(문법·undefined·매크로·오류·probe), `Tests/Testing_Runner.cpp`(통과 시나리오, 실패 보고·abort·artifact, expectedFinalHash, 파싱 오류 → errored, diffSnapshots, `Game/Tests` discover 후 전부 통과).

## 미구현

- `events`(Screenshot/NamedEvent), `capture` assertion, `videoDriver: offscreen` — Phase 2 렌더 뒤.
- `threads: [1, 8]` T1 검사 — 엔진이 단일 스레드라 `threads` 는 항상 1 로 보고.
- `GAME_TEST_CONFIG` 환경변수 모드, `game replay record` → inputs 변환(§22.3), `--fields` 투영.
- setup 에서 `apply` batch(Command 로 authoring 을 바꾼 뒤 테스트) — 필요하면 `CommandBus` 를 fork 한 Project 에 적용하는 한 줄.
