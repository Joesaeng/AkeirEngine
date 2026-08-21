# MoltEngine 0.1 (ME0.1) — QUICKSTART

이 zip 은 MoltEngine 의 첫 릴리즈다: **소스 + 문서 + 미리 빌드된 `bin/game.exe`(Windows x64, SDL 포함)**.
목적: 이전 대화 맥락이 전혀 없는 세션(사람이든 AI든)이 이 디렉터리만 풀어서 **이 엔진으로 게임을 만들어 보는 것**.

## 0. 무엇이 들어 있나

```
MoltEngine-0.1/
├── QUICKSTART.md              ← 지금 이 파일
├── bin/game.exe               미리 빌드된 CLI (RelWithDebInfo, SDL3 포함: 창/캡처 가능). 빌드 없이 바로 쓴다
├── .mcp.json                  Claude Code 용 MCP 서버 등록 (bin/game.exe mcp --project Game). 상대 경로 — zip 을 푼 디렉터리를 Claude Code 에서 열면 된다
├── Docs/                      00-START-HERE → STATUS → DECISIONS → BUILD → ARCHITECTURE → CONVENTIONS
├── AI_Native_Game_Framework_Design.md   설계 정본 (§0–§89, ▶ v3 구현 주석 포함)
├── Game/                      샘플 프로젝트 "TestArena" (플레이어 + 고블린 3마리, 테스트 3개, 골든 이미지). 건드려도 됨 — 참고용
├── Engine/ Tools/ Tests/ Game/Source/ cmake/ scripts/ CMakeLists.txt CMakePresets.json   소스 (다시 빌드하려면 Docs/BUILD.md)
└── research/                  설계 근거 리서치
```

## 1. 5분 안에 돌려 보기 (빌드 불필요)

```bat
cd Game
..\bin\game.exe version --json
..\bin\game.exe capabilities --json           # tools 15개, busCommands(쓰기 명령 13개 + 인자 스키마), exit/error code 표
..\bin\game.exe project info --json
..\bin\game.exe run --headless --ticks 600 --json      # 결정론 실행. result.finalHash = 0xbc23e49a65efb2e8 이어야 한다
..\bin\game.exe test --json                            # 데이터화 테스트 3개 (Combat / Movement / Visual golden)
..\bin\game.exe capture --ticks 300 --out Cache\capture\f.png --json   # CPU 렌더 PNG
..\bin\game.exe run                                     # 창이 뜬다. WASD/화살표 이동, ESC 종료
```

모든 명령의 stdout 은 JSON envelope 하나(`{ok, command, result|error, changes, warnings, meta}`), exit code 는 `0 ok / 1 failed / 2 usage / 3 findings / 5 not found / 6 crash / 7 timeout`. 사람이 볼 때는 `--json` 을 빼면 pretty 출력.

## 2. 새 게임 프로젝트 만들기

```bat
bin\game.exe project init MyGame --dir C:\work\MyGame --json
cd C:\work\MyGame
<zip경로>\bin\game.exe schema --all --json             # 쓸 수 있는 component 와 속성(타입/범위/enum)
```

`project init` 이 만드는 것: `project.json`, `Worlds/Main.world.json`(MainCamera 하나), `Config/input.json`(MoveX/MoveY/Attack), 빈 `Prefabs/ Tests/ Data/ Assets/`, `.gitignore`, `README.md`(다음 명령들).

데이터만으로 할 수 있는 것 (C++ 불필요):
- entity/prefab/world 만들기·수정·삭제, 속성 설정, 태그, prefab 상속(`--base`)과 override
- 물리(Box2D): `Collider2D`(box/circle/capsule) + `RigidBody2D`(static/kinematic/dynamic)
- 플레이어 이동: `Movement` + `PlayerController` (input.json 의 MoveX/MoveY)
- 적 AI: `EnemyAI`(detectionRange 안에서 추적, attackRange 안에서 공격, targetTag) + `Health`
- 카메라: `Camera2D`(orthoSize, background). 스프라이트는 `SpriteRenderer.tint` 색 도형으로 그려진다(텍스처 로드는 아직 없음)
- 테스트: `Tests/**/*.test.json` (setup / inputs / assert / determinism / capture golden) — 형식은 `Engine/Testing/README.md`

C++ 이 필요한 것: 새 component/system(예: 투사체, 점수, 스폰 웨이브). `Game/Source/` 에 추가하고 `Docs/BUILD.md` 대로 다시 빌드 (VS2022 + 번들 CMake/Ninja; `scripts\build.cmd msvc-release all`).

## 3. AI 클라이언트(MCP)로 쓰기

zip 을 푼 디렉터리를 Claude Code 로 열면 `.mcp.json` 이 `game` 서버를 등록한다(stdio, 대상 프로젝트 = `Game/`). 다른 프로젝트를 대상으로 하려면 `.mcp.json` 의 `--project` 경로를 바꾼다.
tool 15개: `capabilities, project_info, schema_describe, query, inspect, explain, refs, apply, validate, run, run_status, test, capture, tx, history`. 쓰기는 전부 `apply` 하나로(`changes[].op` = `entity.create`, `property.set` …; `$name` 으로 앞 결과 참조; `dryRun`).

손으로 확인:
```bat
echo {"jsonrpc":"2.0","id":1,"method":"tools/list"} | bin\game.exe mcp --project Game
```

## 4. 여러 명령을 한 undo 단위로 / 상주 프로세스

```bat
start bin\game.exe serve --project C:\work\MyGame          # 다른 창. 이후 그 프로젝트의 모든 game 명령이 자동으로 이 프로세스로 간다
bin\game.exe tx begin --json                                  → result.tx
bin\game.exe entity create A --tx tx_… --json
bin\game.exe tx commit tx_… --json
bin\game.exe serve stop
```

## 5. 막히면

- `game capabilities --json` 의 `errorCodes[]`, 각 오류의 `error.ruleId` + `error.details` + `fixes[]`(MachineApplicable 이면 `game validate --fix`).
- `Docs/00-START-HERE.md` → 해당 모듈 README (`Engine/*/README.md`, `Tools/CLI/README.md`) → 설계 문서의 § .
- 알려진 한계: `Docs/STATUS.md` "알려진 문제 / 기술 부채".
- 크래시가 나면 `<project>/Cache/crash/*.dmp` 와 envelope 의 `error.details.lastLogs` 를 같이 보고한다.

git: 이 릴리즈는 태그 `ME0.1`(= `v0.1.0`) 이다. zip 안에는 `.git` 이 없다 — 새 프로젝트는 자기 저장소를 만들면 된다 (`project init` 이 `.gitignore` 를 써 둔다).
