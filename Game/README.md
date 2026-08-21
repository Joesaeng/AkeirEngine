# Game/ — 샘플 프로젝트 "TestArena"

설계 문서 §5 (프로젝트 구조), §6 (문서 모델), §71 (PoC 시나리오), ADR-0001 (2D top-down arena). **이 디렉터리의 JSON 이 source of truth** 다 — 엔진은 이것을 읽어 play world 를 만들고, 변경은 `akeir` CLI(CommandBus)로만 한다.

```
Game/
├── project.json                 { name, tickRate: 60, seed, defaultWorld: "world_…" }
├── Worlds/TestArena.world.json  entities (id-keyed): Arena, MainCamera, Player(Player prefab 인스턴스), Encounter_01 ┬ Goblin_01..03 (Goblin prefab 인스턴스)
├── Prefabs/                     Player / Goblin / GoblinElite(base: Goblin + set)  — 각 *.prefab.json
├── Config/input.json            action map: MoveX/MoveY(axis), Attack(button)  (§88.3)
├── Tests/                       데이터화 테스트 (§23): Combat/GoblinBasicCombat, Movement/PlayerMovement, Visual/CombatCapture(requires renderer)  → `akeir test`
│   ├── Golden/<test>/           골든 PNG (software renderer 로만 생성: `akeir test Visual --update-golden`)
│   └── .results/                `akeir test` 출력 (gitignore)
├── Data/, Assets/               아직 비어 있음 (텍스처 로드는 미구현 — 스프라이트는 tint 색 도형으로 그려진다)
├── Cache/                       엔진 파생물 (gitignore): crash/, journal/, history/ (undo 스택)
└── Source/                      이 게임의 C++ component/system (정적 링크, README 참조)
```

- 파일은 전부 §5.3 canonical 형식이어야 한다 — `akeir validate` 가 `JSON_NOT_CANONICAL` 경고, `akeir fmt` 가 고친다.
- 기준값: `akeir run --headless --ticks 600` → finalHash `0xbc23e49a65efb2e8`, `akeir test` 의 GoblinBasicCombat run-twice 도 같은 값. 이 값이 바뀌면 데이터나 결정론이 바뀐 것이다 (`Docs/STATUS.md`).
- 시나리오(§71): 플레이어(WASD = MoveX/MoveY)가 있고 고블린 3마리가 `detectionRange` 안에서 추적(`chase`), `attackRange` 안에서 `attackCooldown` 마다 `damage` 를 준다(`attack`). 600 tick 뒤 플레이어 HP 10.
- 새 entity/prefab/world 는 손으로 JSON 을 쓰지 말고 `akeir entity create` / `akeir prefab create` / `akeir world create` (또는 `akeir apply batch.json`) 로 만든다 — id(UUIDv7), 기본값, canonical 형식, history 가 자동으로 맞는다.
