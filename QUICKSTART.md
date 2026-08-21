# MoltEngine 0.1 (ME0.1) — QUICKSTART

이 zip 은 MoltEngine 의 첫 릴리즈다: **소스 + 문서 + 미리 빌드된 `bin\game.exe`(Windows x64, SDL 포함, static CRT — 재배포 패키지 불필요)**.
목적: 이전 대화 맥락이 전혀 없는 세션(사람이든 AI든)이 이 디렉터리만 풀어서 **이 엔진으로 게임을 만들어 보는 것**.

## 0. 무엇이 들어 있나

```
MoltEngine-0.1/
├── QUICKSTART.md              ← 지금 이 파일
├── RELEASE.md                 버전, git 해시, bin/game.exe 의 sha256
├── bin\game.exe (+ game.pdb)  미리 빌드된 CLI. 빌드 없이 바로 쓴다. 아래 예시의 `game` = 이 파일. PATH 에 bin\ 을 넣거나 전체 경로로 부른다
├── .mcp.json                  Claude Code 용 MCP 서버 등록 (bin\game.exe mcp --project Game). 상대 경로 — 이 폴더를 Claude Code 의 프로젝트 루트로 열면 된다
├── Docs/                      00-START-HERE → STATUS → DECISIONS → BUILD → ARCHITECTURE → CONVENTIONS
├── AI_Native_Game_Framework_Design.md   설계 정본 (§0–§89, ▶ v3 구현 주석 포함)
├── Game/                      샘플 프로젝트 "TestArena" (플레이어 + 고블린 3마리, 테스트 3개, 골든 이미지). 건드려도 됨 — 참고용
├── Engine/ Tools/ Tests/ Game/Source/ cmake/ scripts/ CMakeLists.txt CMakePresets.json   소스 (다시 빌드하려면 Docs/BUILD.md; .cpm-cache 는 없어 첫 configure 가 GitHub 에서 의존성을 받는다)
└── research/                  설계 근거 리서치
```

## 1. 5분 안에 돌려 보기 (빌드 불필요)

```bat
set G=%CD%\bin\game.exe
cd Game
%G% --help                                      # 전체 명령 목록 (`%G% <명령> --help` 는 그 명령의 usage)
%G% version --json
%G% capabilities --json                         # tools 15개, busCommands(쓰기 명령 13개 + 인자 스키마), exit/error code 표
%G% project info --json
%G% run --headless --ticks 600 --json           # 결정론 실행. result.finalHash = 0xbc23e49a65efb2e8 이어야 한다
%G% test --json                                 # 데이터화 테스트 3개 (Combat / Movement / Visual golden)
%G% capture --ticks 300 --out Cache\capture\f.png --json   # CPU 렌더 PNG
%G% run --ticks 120                             # 창이 뜨고 2초 뒤 스스로 닫힌다. --ticks 없이 실행하면 ESC/닫기까지 돈다. WASD/화살표 이동
```

- stdout 은 JSON envelope 하나(`{ok, command, result|error, changes, warnings, meta}`). 터미널(TTY)에서 `--json` 을 빼면 pretty, 파이프면 항상 한 줄.
- stderr 에는 JSONL 로그가 나온다(정상). 숨기려면 `2>nul` (cmd) / `2>$null` (PowerShell).
- exit code: `0 ok / 1 failed / 2 usage / 3 findings / 4 confirm(--yes) / 5 not found / 6 crash / 7 timeout` (전체 표: `capabilities --json` → exitCodes).
- **선택자(selector)**: 객체를 가리킬 때 `entity_…` 같은 id, 그냥 이름(`Goblin_01`), `name:Goblin_01`, `path:TestArena/Arena/Player` 중 아무거나. entity 와 prefab 둘 다 찾는다. 같은 이름이 여럿이면 `AMBIGUOUS_SELECTOR` 와 후보 목록.

## 2. 새 게임 프로젝트 만들기

```bat
%G% project init MyGame --dir C:\work\MyGame --json
cd C:\work\MyGame
%G% schema --all --json                          # 쓸 수 있는 component 와 속성(타입/범위/enum — 값은 소문자: "circle", "dynamic")
```

`project init` 이 만드는 것: `project.json`, `Worlds/Main.world.json`(MainCamera 하나), `Config/input.json`(MoveX/MoveY/Attack), 빈 `Prefabs/ Tests/ Data/ Assets/`, `.gitignore`, `README.md`(다음 명령들 — 복붙 가능한 예시 포함).

cmd.exe 에서 JSON 인자는 큰따옴표를 `\"` 로 escape 한다 (PowerShell 은 작은따옴표로 감싼다):
```bat
%G% prefab create Hero --components "{\"Collider2D\":{\"shape\":\"circle\",\"radius\":0.4},\"RigidBody2D\":{\"type\":\"dynamic\",\"gravityScale\":0},\"Movement\":{\"speed\":5},\"PlayerController\":{},\"Health\":{\"max\":100}}" --json
%G% prefab instantiate Hero --name Player --json
%G% entity create Wall --components "{\"Collider2D\":{\"size\":[10,1]},\"RigidBody2D\":{\"type\":\"static\"},\"Transform\":{\"position\":[0,-5,0]}}" --json
%G% run --headless --ticks 600 --json
```

데이터만으로 할 수 있는 것 (C++ 불필요):
- entity/prefab/world 만들기·수정·삭제, 속성 설정, 태그, prefab 상속(`--base`)과 override. 명령 하나 = undo 한 단위(`game undo`; `validate --fix` 는 fix 마다 하나, `apply`/`tx` 는 묶음이 하나).
- 물리(Box2D): `Collider2D`(box/circle/capsule) + `RigidBody2D`(static/kinematic/dynamic). **Collider2D 만 있으면 body 가 없어 아무것도 막지 못한다** — 벽은 `RigidBody2D {type: static}` 을 같이 준다 (`validate` 가 `COLLIDER_WITHOUT_BODY` 로 경고하고 `--fix` 가 붙여 준다).
- 플레이어 이동: `Collider2D` + `RigidBody2D`(dynamic, gravityScale 0) + `Movement` + `PlayerController` (input.json 의 MoveX/MoveY). 의존성 사슬(`Movement` → `RigidBody2D` → `Collider2D`)은 `schema --all` 의 `x-requires` 에 있고, 빠뜨리면 `COMPONENT_DEPENDENCY_MISSING` 으로 거부된다(생성 자체가 롤백된다).
- 적 AI: `EnemyAI`(detectionRange 안에서 추적, attackRange 안에서 공격, targetTag) + `Health`. **공격이 효과를 내려면 맞는 쪽에도 `Health` 가 있어야 한다.**
- 카메라: `Camera2D`(orthoSize, background). 스프라이트는 `SpriteRenderer.tint` 색 도형으로 그려진다(텍스처 로드는 아직 없음).
- 테스트: `Tests/**/*.test.json` 을 **손으로** 쓴다 (setup / inputs / assert / determinism / capture golden) — 형식은 `Engine/Testing/README.md`.

C++ 이 필요한 것: 새 component/system(예: 투사체, 점수, 스폰 웨이브). `Game/Source/` 에 추가하고 `Docs/BUILD.md` 대로 다시 빌드 (VS2022 + 번들 CMake/Ninja; `scripts\build.cmd msvc-release all`, 테스트는 `build\msvc-release\Tests\pme_tests.exe`).

## 3. AI 클라이언트(MCP)로 쓰기

zip 을 푼 디렉터리를 Claude Code 로 열면 `.mcp.json` 이 `game` 서버를 등록한다(stdio, 대상 프로젝트 = `Game/`). 다른 프로젝트를 대상으로 하려면 `--project` 경로를 바꾼다. Claude Code(Node) 는 상대 경로를 프로젝트 루트 기준으로 푼다. Python 등 다른 런처는 못 풀 수 있으니 그때는 절대 경로 버전을 만들어 쓴다:
```bat
%G% mcp --print-config --project C:\work\MyGame > .mcp.json
```
tool 15개: `capabilities, project_info, schema_describe, query, inspect, explain, refs, apply, validate, run, run_status, test, capture, tx, history`. 쓰기는 전부 `apply` 하나로(`changes[].op` = `entity.create`, `property.set` …; `$name` 으로 앞 결과 참조; `dryRun`). 각 op 의 인자 스키마는 `capabilities` tool 의 `busCommands[]`. `tx`(begin→apply…→commit)는 MCP 세션 안에서 바로 쓸 수 있다.

손으로 확인:
```bat
echo {"jsonrpc":"2.0","id":1,"method":"tools/list"} | bin\game.exe mcp --project Game
```

## 4. 여러 CLI 호출을 한 undo 단위로 / 상주 프로세스

```bat
cd C:\work\MyGame                                    (또는 모든 명령에 --project C:\work\MyGame)
start %G% serve                                      # 다른 창. 이후 이 프로젝트의 모든 game 명령이 자동으로 이 프로세스로 간다 (meta.via = "serve")
%G% tx begin --json                                  → result.tx
%G% entity create A --tx tx_… --json
%G% tx commit tx_… --json
%G% serve stop
```
serve 없이 `--tx` 를 주면 `TX_REQUIRES_SERVE`. 데몬을 무시하고 싶으면 `--local`.

## 5. 막히면

- `game <명령> --help`, `game capabilities --json` 의 `errorCodes[]`, 각 오류의 `error.ruleId` + `error.details` + `fixes[]`(MachineApplicable 이면 `game validate --fix`).
- `Docs/00-START-HERE.md` → 해당 모듈 README (`Engine/*/README.md`, `Tools/CLI/README.md`) → 설계 문서의 § .
- 알려진 한계: `Docs/STATUS.md` "알려진 문제 / 기술 부채" (예: MCP 인자 오타는 조용히 무시됨, `query` 는 `limit` 만 있고 cursor 없음).
- 크래시가 나면 `<project>/Cache/crash/*.dmp` 와 envelope 의 `error.details.lastLogs` 를 같이 보고한다 (`bin\game.pdb` 로 심볼 해석).

git: 이 릴리즈는 태그 `ME0.1`(= `v0.1.0`) 이다. zip 안에는 `.git` 이 없다(`git archive`) — 새 프로젝트는 자기 저장소를 만들면 된다 (`project init` 이 `.gitignore` 를 써 둔다).
