# STATUS — 진행 상태 (AKEIR Engine)

**마지막 갱신: 2026-08-22 (v0.1.0 릴리즈 + 외부 리뷰 반영 시점)**. 매 작업 세션의 끝에 이 파일을 갱신한다. 설계 문서 §74 (Phase) / §86 (체크리스트)를 추적한다.

## 한눈에

| Phase | 내용 | 상태 |
|---|---|---|
| -1 | Substrate spike + §88 결정 | **부분** — 결정은 DECISIONS.md ADR-0001~0014 로 기록(대부분 `가정`). Flecs spike(S-A)는 Phase 1 구현이 겸한다. Godot/Bevy 비교 arm(A3/C)은 미실행 |
| 0 | 최소 Runtime | **완료(headless)** — Core, fixed-tick Application, FPU env, OTel 로그, crash/watchdog, CLI envelope. SDL 창/입력만 Phase 2 로 이월 (SDL3 는 `msvc-debug` 로 빌드됨) |
| 1 | World + Reflection + 데이터 모델 | **완료** — Reflection registry, canonical JSON, Project 문서 모델(prefab resolve, validate, fmt), Flecs PlayWorld, Box2D, Game/ 샘플(Health/Movement/PlayerController/EnemyAI + 3 systems). `akeir run --headless` 가 실제 World 를 결정론적으로 돌린다 |
| 2 | Render | **완료(PoC)** — `Engine/Platform`(SDL3 창/입력/창 모드 루프) + `Engine/Render`(SDL_Renderer placeholder 스프라이트, software capture, golden 비교). `akeir run`(창), `akeir capture`, 테스트 capture assertion + golden. 텍스처/SDL_GPU 는 미구현 |
| 3 | Command | **완료(핵심)** — `Engine/Commands`: ChangeSet(RFC 6902 superset) · CommandBus(fork→handler→validate→commit) · journal/history · undo/redo · in-process tx · apply(batch/$ref/idempotency) · dry-run · 내장 Mutation command 13개 · CLI 17개 쓰기 명령 · `validate --fix`. 남은 것: file.* op, checkpoint(§52), `--if-match`, migration(§53) |
| 4 | CLI 확장 + `akeir serve` | **완료(핵심)** — `akeir serve`(127.0.0.1 NDJSON JSON-RPC + token, `Cache/serve.json`), 모든 `akeir <cmd>` 자동 포워딩, multi-call `tx begin|commit|rollback|list`(TTL), `run status` handle, `--stdio`, journal 복구. 텍스트 출력 포맷/`--fields`/`project.set` 은 미구현 |
| 5 | Headless + Test + Capture | **완료(핵심)** — `Engine/Testing`: §23 시나리오(setup/inputs/assert), §23.1 표현식, run-twice 결정성 + snapshot diff, §24 results.json/JUnit, capture assertion + golden(`requires: ["renderer"]`), `akeir test`. Game/Tests 3개 시나리오(SDL 빌드) 통과 |
| 6 | Editor | 미시작 |
| 7 | MCP sidecar + §72 실험 | **부분** — `akeir mcp`(stdio MCP 서버, initialize/server/discover/tools/list/tools/call, tools 15/15 enabled). §72 비교 실험은 미실행 |

빌드: `scripts\build.cmd msvc-headless all` 과 `msvc-debug all`(SDL3 포함) 통과. 테스트: `akeir_tests.exe` 80 케이스(msvc-debug/release; headless 는 78) 통과 + `akeir test` 3 시나리오(SDL) / 2 + 1 skipped(headless) 통과 (2026-08-21). 샘플 `Game/` 은 `validate` 0 error / 0 warning(canonical), `run --headless --ticks 600` 의 기준 finalHash = `0x404c60567ccb9e85` (2026-08-22 샘플에 스프라이트 asset 참조(`0x4ac7…`)와 HUD TextRenderer entity(`0x404c…`)를 넣으면서 `0xbc23e49a65efb2e8` 에서 바뀜 — reflected 문자열 `SpriteRenderer.sprite` 가 해시에 들어간다; 불변 fixture 는 여전히 `0xbc23…`. 이 값이 바뀌면 결정론 또는 샘플 데이터가 바뀐 것).
git: 공개 저장소 **https://github.com/Joesaeng/AkeirEngine** (branch `main`, 태그 `v0.1.0` → `v0.1.1` → **`v0.1.2`**(2026-08-22: 게임 exe + CatSurvivor 피드백 ADR-0036~0041; 실험용 zip 은 이것), GitHub Release 에 `AKEIR-<ver>.zip` 첨부 — 소스 + Docs + `bin/akeir.exe` + QUICKSTART; 리서치 자료와 `.pdb` 는 제외). 릴리즈 zip 은 `python scripts/package.py`. CI: `.github/workflows/ci.yml` (headless + SDL job) — 2026-08-22 기준 **green**, runner(MSVC 14.51) 의 finalHash 도 기준값과 동일. 태그 `v0.1.0` 은 CI 가 green 이 된 커밋(6ed8d4f) 으로 옮기고 zip 을 재업로드했다. **커밋/푸시는 사용자가 지시할 때만.**

## §86 체크리스트 진행

**Phase -1**
- [x] 목표 게임 이름 붙이기 → ADR-0001 (가정: 2D top-down arena)
- [~] Flecs vs EnTT spike → Flecs 를 기본으로 Phase 1 에서 검증 (ADR-0002)
- [ ] Godot headless / Bevy+brp_mcp 로 §71 시나리오 측정 (A3, C)
- [x] §88 각 항목 결정/가정 기록 → DECISIONS.md (88.0 ADR-0001, 88.1 ADR-0011, 88.4 ADR-0012, 88.5 ADR-0004; 나머지는 구현 시점에 ADR 추가)
- [x] 컴파일러 결정 → ADR-0004
- [x] 의존성 관리 하나 선택 → ADR-0006 (CPM)

**Phase 0 — Runtime**
- [x] CMake ≥ 3.28, C++20, preset 4종, `scripts/build.cmd|ps1`
- [x] Application / Main Loop — fixed tick, headless 경로 accumulator 없음 (§20.1) — `Application::runHeadless`
- [x] SDL3 Window / Input / `dummy` driver 선택 (§20) → Phase 2 에서 구현 (`Engine/Platform`; `offscreen` 은 software renderer 로 대체, ADR-0026)
- [x] `det_fp_flags` INTERFACE target (§41) — `/fp:precise`, hash 노출
- [x] FPU env assert (round-to-nearest, DAZ/FTZ) (§22.2) — `normalizeFpEnv`, run 시작 시 적용
- [x] Logging: OTel JSONL (§28) — `akeir::Logger`, stderr/file/ring sink
- [x] 크래시 핸들러: minidump + 마지막 N 로그 + exit 6, watchdog exit 7 (§88.4) — 실측 검증됨. C++ 예외는 INTERNAL_ERROR envelope(exit 1)

**Phase 1 — World + Reflection + 데이터 모델** (완료)
- [x] Persistent ID: TypeID/UUIDv7 grammar, 형식 검사, v8 결정적 (§7) — `akeir::Id`. DUPLICATE_PERSISTENT_ID 검출 (`akeir id fix` 는 미구현)
- [x] Reflection registry: `AKEIR_REFLECT_*` / `PropertyMeta` / runtime 등록 (§42.2) — Engine/Reflection
- [x] Transform + 내장 component 5종 (SpriteRenderer, Collider2D, RigidBody2D, Camera2D) — Engine/Runtime/Components
- [x] World 문서 모델: entities = id-keyed object, 계층 = parent + order, prefab set/add/remove (§5.3, §6, §34) — `akeir::Project`
- [x] JSON load / save — reflection 기반 serializer + visibility mask 3종 (§26.1, §88.8) — Serialization/ComponentJson
- [x] Canonical serialization + `akeir fmt` + round-trip byte-identical 테스트 (§5.3)
- [x] JSON Schema 2020-12 생성 + wire_format (§14, §14.1) — `akeir schema component X`
- [x] Box2D v3.1.1 통합 (PhysicsWorld 인터페이스 뒤) (§57) — Engine/Physics
- [x] `RngStream` (xoshiro256** + SplitMix64, per-system) (§22.2) — `PlayWorld::rng(name)`
- [x] 결정적 EntityId: runtime spawn 은 `Id::deterministic(seed, tick, ordinal)` (§7.1)
- [x] Flecs 투영 round-trip: project JSON → PlayWorld → snapshot, 두 프로세스 finalHash 동일 (T0). `ecs_world_to_json` 대신 자체 snapshot — authoring JSON 이 source of truth 이므로 Flecs JSON round-trip 은 불필요 (ADR-0002 확정)
- [x] `akeir run --headless` 가 프로젝트 World 를 돌린다: `--ticks --seed --world --hash-every --hash-out --snapshot-out --replay`
- [x] `akeir validate`(exit 3 + fixes) / `fmt` / `schema` / `explain` / `entity list` / `query` / `dump` / `refs`(§19 reference graph: prefab/base/parent/property/override/defaultWorld)
- [x] `akeir project init <name> [--dir]` — 빈 프로젝트(project.json, Worlds/Main.world.json + MainCamera, Config/input.json, .gitignore, README) (v0.1.0 에 포함)

**Phase 3 — Command** (핵심 완료; §86 Phase 3 항목)
- [x] `CommandBus` + `CommandKind` + `ChangeBuilder` (§8) — `Engine/Commands`. handler 는 fork 위에서 ChangeBuilder 로만 변경
- [x] ChangeSet = RFC 6902 superset, `before`/`value`, self-inverting, `inverse()`, `compose()` (§78, §10.1). `file.*` op 는 정의만 (asset store 없음)
- [x] commit 절차 (§9.2): base 해시 검사 → journal → 메모리 적용 → canonical temp+rename → history → journal 삭제. 복구 `recoverJournal()`
- [x] History (§10): `Cache/history/history.jsonl` + cursor, undo/redo, actor 태깅·필터, redo 꼬리 truncate, conflict 검출(`UNDO_CONFLICT`, `BASE_MISMATCH`)
- [x] in-process Transaction (§9): beginTx/commitTx(compose → history 1항목)/rollbackTx. multi-call tx 는 Phase 4
- [x] `apply` batch (§49): atomic, `$name`/`$name.field` 참조, idempotencyKey 재생, dry-run
- [x] dry-run = fork + execute (§50): `--dry-run` 으로 모든 쓰기 명령에서. `--if-match` 는 미구현
- [x] Mutation command: entity.create/delete/rename/reparent, component.add/remove, property.set, tag.add/remove, prefab.create/instantiate, world.create, document.patch. 인스턴스·derived prefab 은 set/add/remove override 로 (§78.1). `entity` 인자에 prefab selector 허용(prefab 편집)
- [x] commit 전 검증: 새로 생기는 error 만 거부 (`VALIDATION_FAILED`), `--no-validate` 로 우회
- [x] CLI: `akeir entity create|delete|rename|reparent`, `component add|remove`, `set`, `tag add|remove`, `prefab create|instantiate`, `world create`, `apply`, `undo|redo|history`, `cmd` (Tools/CLI/MutationCommands.cpp)
- [x] `akeir validate --fix` (§29/§79): MachineApplicable fix 를 CommandBus(apply/document.patch)로 적용, 중복 fix 제거, fmt 는 직접 재직렬화. COMPONENT_DEPENDENCY_MISSING fix 는 전이적 의존성까지 add
- [x] `capabilities`: tools[] 15개(§47, 전부 enabled; `capture` 는 SDL 빌드만) + `busCommands[]`(args JSON Schema) + errorCodes + 자기완결 envelope outputSchema
- [ ] checkpoint (§52), semantic diff 출력 (§51), rename table/migration (§53), `--if-match`
- [ ] `Engine/Validation` 별도 모듈 — 지금은 `Project::validate()` + `validateComponentJson` 이 규칙 전부. SARIF 출력 없음

**Phase 2 — Render** (PoC 완료)
- [x] SDL3 3.4.14 정적 빌드 + `Platform::init` (`SDL_HINT_VIDEO_DRIVER` 로 dummy 선택, 실제 driver 보고) (§20) — Engine/Platform
- [x] `Config/input.json` action map → `InputFrame` (키보드; gamepad/mouse 는 unsupported 보고) (§88.3) — `InputMap`, `akeir input map`
- [x] 창 모드 루프: fixed tick + accumulator + vsync 렌더, `--record inputs.jsonl` → headless `--replay` 가 같은 finalHash (§20.1, §22.3) — `runInteractive`, `akeir run`
- [x] 2D placeholder 스프라이트 렌더(Transform/SpriteRenderer/Collider2D/Camera2D) — `Renderer2D` (SDL_Renderer; ADR-0027)
- [x] software rasterizer capture: 창/GPU 없이 결정적 PNG, `akeir capture`, §27.1 비교(`--compare/--diff`, tolerance) (ADR-0026)
- [x] 테스트 capture assertion + golden (`Tests/Golden/<test>/<name>_<WxH>.png`, `--update-golden`), `requires: ["renderer"]` → headless 에서 skipped
- [ ] 텍스처/스프라이트 아틀라스(`SpriteRenderer.sprite` Ref + Assets/ sidecar), 텍스트, SDL_GPU 경로, offscreen GL driver, 오디오

**Phase 5 — Headless + Test + Capture** (핵심 완료)
- [x] `Tests/**/*.test.json` 스키마 파싱 (setup spawn/entity+as, inputs hold/axis/press/release, run, determinism, assert always/eventually/at) — `TestScenario::fromJson`
- [x] §23.1 assertion 표현식 evaluator (CEL 부분집합: 비교·산술·논리·in·has/size/abs/dist/min/max·all/exists/exists_one), undefined = 오류 — `akeir::expr::Expr`
- [x] snapshot 위 평가, `always` 첫 위반에서 abort, `eventually` 창, `at` tick/end, 실패 시 bindings + snapshot artifact
- [x] run-twice 결정성(§22.2 T0) + 첫 divergent tick 의 snapshot diff(entity/path/a/b) + firstDivergentSystem, `expectedFinalHash`
- [x] `results.json`(§24) + `--junit`(testsuite = 디렉터리, [[ATTACHMENT|…]]) + exit 3
- [x] `akeir test [filter] [--junit f] [--results-dir d] [--no-artifacts] [--list]`, 샘플 `Game/Tests/Combat/GoblinBasicCombat`, `Game/Tests/Movement/PlayerMovement`
- [x] `capture` assertion + golden 비교 (SDL 빌드; `requires: ["renderer"]`)
- [ ] `events`(Screenshot/NamedEvent), `videoDriver: offscreen`(software renderer 가 대신한다)
- [ ] T1 (`threads: [1, 8]`) — 엔진이 단일 스레드
- [ ] `akeir replay record` → test inputs 변환 (§22.3), `GAME_TEST_CONFIG` 환경변수 모드

**Phase 4 — CLI 확장 + `akeir serve`** (핵심 완료)
- [x] `akeir serve`: 프로젝트 1회 로드, 단일 CommandBus, loopback TCP NDJSON JSON-RPC, per-session token, `Cache/serve.json` 발견, `--idle-timeout`, `--port` (§88.1, §46.2; ADR-0029)
- [x] 얇은 클라이언트: 데몬이 있으면 모든 `akeir <cmd>` 가 RPC 로 포워딩(`meta.via = "serve"`), 없거나 죽었으면 stale 파일 제거 후 one-shot. `--local` 로 강제 in-process
- [x] multi-call tx: `tx begin [--ttl]` → `--tx <id>` → `tx commit|rollback|list`, TTL 만료 → `TX_UNKNOWN_OR_EXPIRED` (§9.1). one-shot 에서는 `TX_REQUIRES_SERVE`
- [x] run handle: `run.start` 가 `result.run` 을 돌려주고 serve 안에서 `run status [id]` 로 조회 (§46.2)
- [x] `--stdio` 모드 (Editor 임베딩), `project.reload`(열린 tx 무효화), actor 는 요청마다 호출자의 것
- [ ] 사람용 텍스트 출력 포맷, `--fields`/`--jq`/`--cursor`, `project.set`(defaultWorld/seed), Game 모듈 등록 주입, 파일 watcher → `project.reload_document` (§39)

**Phase 7 — MCP** (서버 완료, 실험 미실행)
- [x] `akeir mcp`: stdio MCP 서버 (`server/discover` 2026-07-28 + `initialize` 2025-xx 호환, `tools/list`, `tools/call` → `structuredContent` = envelope, `isError`), tool → CLI argv / bus.apply 매핑 (§46.2), instructions 텍스트 (ADR-0030)
- [ ] resources (`game://schema/...`, snapshot), prompts(recipe), progress/Tasks, §72 비교 실험(Godot/Bevy arm), `--read-only` tool 집합

(Phase 6 항목은 설계 문서 §86 그대로. 시작할 때 여기에 옮긴다.)

## 알려진 문제 / 기술 부채

- `akeir` CLI 의 사람용 출력(TTY)은 pretty JSON 뿐이다. 명령별 텍스트 포맷은 미구현.
- `capabilities.tools[]` 15개 전부 구현됨 (`capture` 는 SDL 빌드에서만 enabled).
- `project.json` 은 Project 의 문서 맵 밖에 있어 command 로 못 바꾼다 (`defaultWorld`, `seed` 등) — `project.set` 미구현.
- `akeir validate --fix` 의 JSON_NOT_CANONICAL 수정(재직렬화)은 CommandBus 를 거치지 않는다 (JSON 값 변화가 없어 ChangeSet 으로 표현 불가) → undo 대상 아님.
- `apply` 는 changes[] 를 `busCommands[].args` 스키마로 사전 검증하지 않는다 — 각 handler 가 인자를 검사한다 (`ARG_REQUIRED`/`ARG_TYPE`).
- History 는 프로젝트당 하나의 선형 스택 (`Cache/history`). `akeir serve` 가 있으면 그것이 단일 writer; `--local` 로 우회해 두 프로세스가 쓰면 `BASE_MISMATCH` 로 드러난다 — 파일 lock 없음.
- `akeir serve` 는 단일 스레드·연결 하나씩 처리한다. 긴 `run`/`test` 동안 다른 클라이언트는 기다린다.
- MCP `tools/call` 의 인자와 CLI 의 모르는 플래그는 스키마로 사전 검증하지 않는다 — 오타(`tick` vs `ticks`)가 조용히 무시된다. `query` 에 cursor 페이지네이션이 없다(`limit` 만). `inspect` 는 `entity` 필수.
- ctest 경로(`build.cmd … test`)는 doctest 이름을 `;` 로 쪼갠다 — 테스트 이름에 `;` 를 쓰지 않는다(v0.1.0 검증에서 11개가 조용히 건너뛰어진 것을 발견해 전부 `—` 로 바꿨다). 정본은 `akeir_tests.exe` 직접 실행.
- `Project` 전체를 복사해 fork 한다 (O(문서 크기)). 수백 entity 에서는 무시할 수준; 큰 프로젝트면 copy-on-write 필요.
- `akeir query / dump / run` 은 호출마다 play world 를 새로 build 한다. 상주 world 가 필요하면 `run open/step/…`(ADR-0041, serve/MCP 안에서만).
- PlayWorld 의 query 는 선형 스캔 (수백 entity 규모에서 충분). Flecs 쿼리 파이프라인은 필요할 때.
- body 가 있는 entity 의 Transform.x/y 는 physics 가 소유한다 (ADR-0016) — system 이 Transform.position 을 직접 바꿔도 body 에는 반영되지 않는다.
- `Collider2D.layer` 문자열 → Box2D category/mask bit 매핑은 아직 없다 (전부 0x0001/0xFFFF).
- CLI 가 `game::registerGameSystems` 를 직접 호출한다 — Game/ 을 교체 가능한 모듈로 만들려면 등록 함수 주입이 필요.
- `Crash.cpp` 의 `stopWatchdog()` 은 `std::thread` 를 join 하므로 watchdog 스레드가 깨어나는 것을 기다린다(최대 수 ms). 문제 없음이지만 알아둘 것.
- CMake configure 시 CPM 이 `<name>_SOURCE_DIR` 을 내보내려면 `akeir_add_dep` 가 macro 여야 한다 (function 이면 삼킨다). 이미 macro.

## 다음 할 일 (우선순위 순)

(2026-08-22 외부 리뷰 반영 후) 새 엔진 기능보다 **실제 게임 제작으로 검증**이 먼저다. 리뷰에서 미룬 항목은 ADR-0033 참조.

1. **§72 실험**: 저장소 루트 `.mcp.json` 이 `akeir mcp`(msvc-release 빌드, Game/ 프로젝트)를 Claude Code 에 등록한다. 실험 중 Game/Source 를 고치고 재빌드해도 MCP 연결은 유지되고 다음 호출부터 새 빌드가 답한다(ADR-0034) — 새 세션에서 MCP tool 만으로 §71 시나리오(고블린 추가, 속도 조정, 테스트, capture)를 수행하고 tool call 수·오류율을 기록. Godot/Bevy arm 은 선택.
2. Phase 4 잔여: 사람용 텍스트 출력, `--fields`, `project.set`, Flecs REST, 파일 watcher (play world 상주는 ADR-0041 로 완료).
3. Phase 7 잔여: MCP resources(`game://schema/*`, snapshot), prompts(recipe), `--read-only`.
4. Phase 2 잔여: TTF/한글 폰트 asset, 카메라 종횡비 (텍스처 ADR-0037, bitmap 텍스트 ADR-0040 완료). Phase 3 잔여: `--if-match`, checkpoint(§52), semantic diff(§51), migration(§53), `Engine/Validation` + SARIF.
5. Phase 6 Editor (ImGui) — serve 의 `--stdio` 또는 in-process ServeHost 위에.

## 세션 로그

- **2026-08-21 세션 1 (전반)**: 설계 문서 v2 보강·검증 → 구현 시작. 저장소 골격, Docs 6종, CMake/CPM/preset, Core 모듈, CLI 골격, 테스트 19 케이스. crash(exit 6, minidump)·watchdog(exit 7) 실측.
- **2026-08-21 세션 1 (후반)**: Phase 0 마무리(Application, FpEnv) + Phase 1 전체 — Reflection, Serialization(canonical/ComponentJson), Runtime(Project/Components), Physics(Box2D), ECS(PlayWorld/Flecs), Game/Source 샘플, CLI 명령 11개. 테스트 47 케이스. `akeir run --headless --ticks 600` 이 두 프로세스에서 같은 finalHash — T0 확인. 고블린 3마리가 플레이어를 추적·공격해 600 tick 후 HP 10 (§71 시나리오 2~5 의 headless 부분).
- **2026-08-21 세션 1 (Phase 3)**: `Engine/Commands` 전체 + CLI 쓰기 명령 17개 + `validate --fix` + capabilities tools[] 15개. 테스트 63 케이스. 샘플 프로젝트 복사본에서 `set` → `entity create` → `apply`(prefab.create + instantiate×2 + tag, `$ref`) → `run` → `undo 2` 로 파일이 원본과 byte-identical 복귀, `validate --fix` 가 범위/의존성/canonical 오류 4건을 전부 고치는 것을 확인.
- **2026-08-21 세션 1 (Phase 5 테스트)**: `Engine/Testing`(Expr + TestRunner) + `akeir test` + 샘플 시나리오 2개. GoblinBasicCombat 의 run-twice finalHash 가 `akeir run` 기준값 `0xbc23e49a65efb2e8` 과 같다(러너와 run 경로의 동치 확인). 테스트 71 케이스.
- **2026-08-21 세션 1 (Phase 2)**: `Engine/Platform` + `Engine/Render` + CLI `run`(창)/`capture`/`input map` + 테스트 capture assertion/golden. software capture 두 번이 byte-identical, `akeir run --ticks 90 --record` → `--headless --replay` 가 같은 finalHash(`0xafcd091ec8be292a`). 골든 `Tests/Golden/CombatCapture/combat_end_256x256.png` 생성. 발견한 버그: `std::optional<unique_ptr>` 를 돌려주며 out-param 을 move 해 null 역참조(crash handler 가 exit 6 + minidump 로 잡음 — §88.4 경로 실증).
- **2026-08-21 세션 1 (Phase 4/7)**: `akeir serve`(ServeHost + Winsock NDJSON JSON-RPC + token) + 자동 포워딩 + multi-call tx(TTL) + run handle + `--stdio`, `akeir mcp`(stdio MCP 서버). 데몬 위에서 tx begin → create/tag(--tx) → 밖에서는 안 보임 → commit → history 1항목, stop 후 one-shot 으로 history 이어짐을 확인. MCP: initialize/tools/list(13~14)/tools/call(query, apply dryRun, inspect 오류 → isError) 확인. `akeir refs`(§19) 추가로 tools 15/15. 루트 `.mcp.json` 등록. 테스트 75 케이스.
- **2026-08-21 세션 1 (v0.1.0 릴리즈)**: `akeir project init` 추가, 이름 결정(AKEIR Engine → 충돌 발견 → **AKEIR Engine**, 실행 파일 `akeir.exe`; ADR-0032), 첫 커밋 + 태그, `scripts/package.py` → `dist/AKEIR-0.1.zip`. 독립 에이전트 5명이 zip 만 풀어 QUICKSTART 를 따라가는 blind 검증 → 5/5 "동작, 마찰 있음"(blocker 없음). 발견·수정: ctest 가 `;` 든 테스트 11개를 건너뜀, `.mcp.json` 상대경로(`bin\\akeir.exe` 로), MCP outputSchema 의 풀리지 않는 `$ref`(inline 스키마로), 선택자 문법 미노출(bare name 허용 + 설명), `--help` 부재(추가), 릴리즈 exe 의 VC++ 재배포 의존(static CRT), Collider2D 만 있는 벽이 안 막힘(`COLLIDER_WITHOUT_BODY` 경고 + fix), 기타 문서 불일치. 수정 후 재패키징·재태깅.
- **2026-08-22 (v0.1.2 릴리즈)**: 버전 0.1.2, `AKEIR-0.1.2.zip`(bin/akeir.exe + bin/TestArena.exe). 내용 = 아래 CatSurvivor 피드백 반영 전부.
- **2026-08-22 (CatSurvivor 피드백 반영)**: 코덱스가 v0.1.1 로 CatSurvivor 를 만든 피드백(`C:\Project\AE_Survivor\AkeirEngine\Docs\CATSURVIVOR_ENGINE_FEEDBACK.md`, §72 첫 실측) → 게임 실행 파일 `bin/<ProjectName>.exe`(Tools/Player, 더블클릭), ADR-0036 테스트 fixture 분리(`Game/` 교체 가능 — CatSurvivor 로 검증), ADR-0037 텍스처 asset(§37 sidecar 문서 + `asset.import` + nearest 스프라이트 렌더, 샘플에 arena.png). 기준 finalHash `0x4ac7b45c37618374`. ADR-0038 PlayWorld 런타임 primitive(Pre/PostPhysics phase, `spawnPrefab`, 런타임 component/tag add·remove, spawn 의 unknown component 거부). ADR-0039 테스트 DSL 발견성(`schema test`, `capabilities.testScenario`, parse 시점 "did you mean", `test explain`). ADR-0040 `TextRenderer` + 내장 5×7 폰트(샘플 HUD), ADR-0041 상주 play world(`run open/step/inspect/…`, MCP `play`). **피드백 5항목 전부 반영 완료** — 남은 것은 CatSurvivor 를 새 엔진으로 옮겨 보는 것(코덱스/사용자).
- **2026-08-22 (v0.1.1 릴리즈)**: 버전 0.1.1 (`project(... VERSION 0.1.1)`, `release v0.1.1`, zip `AKEIR-0.1.1.zip`). 내용 = v0.1.0 + 영어 README/QUICKSTART + ADR-0034(상주 MCP vs 재빌드, zip 의 `bin/akeir.exe` 자동 갱신) + ADR-0035(reflection completeness). PRINCIPLES.md §6/§17/§26 에 사용자 승인 하에 '상태: 해결' 한 줄씩.
- **2026-08-22 (sharp edge 둘 해결, ADR-0034/0035)**: ① 상주 `akeir mcp`/`serve` 가 잡은 `akeir.exe` 때문에 재빌드가 `LNK1168` 로 깨지던 문제 — 빌드는 잠긴 exe 를 `akeir.exe.stale-*` 로 옮기고(`cmake/UnlockExe.cmake`, PRE_LINK) MCP 는 relay(adapter) + `--worker` 자식 구조로 바꿔 재빌드 후 첫 요청에서 worker 를 새 exe 로 교체한다(`MCP_WORKER_RESTARTED` note). `version`/`capabilities.info` 에 `exe.sha256`, `serve.json` 에 `exeSha256` + `SERVE_STALE_EXE`. `scripts/test_resident_rebuild.py` 가 end-to-end 검증(CI 포함). 이 세션의 Claude Code 가 exe 를 잡은 채로 실제 재빌드 성공. ② reflection completeness — `aggregateArity<T>()` 로 멤버 수를 세어 `AKEIR_PROP`+`AKEIR_SKIP` 과 다르면 `REFLECT_MEMBER_UNLISTED`(validate 가 포함, 테스트가 검사). 단위 테스트 78 (headless) / 80 (SDL). spike 교훈: `file(LOCK)` 은 exe 를 truncate 한다 — 쓰지 말 것.
- **2026-08-22 (원칙 문서)**: 외부 설계 원칙 초안(GPT) 을 코드 대조 리뷰(5 lens + 적대 검증) → 사용자가 반영한 `Docs/PRINCIPLES.md` 를 로컬 전용(gitignore) 으로 둠. Rule 0 = AKEIR.md §84 전제, 우선순위 ADR > AKEIR.md > PRINCIPLES > 요약 문서. §26 의 sharp edge 둘(reflection completeness, 상주 exe rebuild LNK1168) 은 같은 날 해결(위 항목).
- **2026-08-22 (리뷰 반영)**: 외부 아키텍처 리뷰(GPT) 의 P0/P1 를 반영 — `pme`→`akeir` 네임스페이스/헤더/타깃/매크로 전면 개명(`akeir_tests.exe`, `AKEIR_REFLECT_*`, `AKEIR_LOG`, `AKEIR_WITH_SDL`), MIT LICENSE + THIRD_PARTY_NOTICES + CONTRIBUTING, `.mcp.json` gitignore + `.mcp.json.example`, README 를 한 줄 메시지 + 4 포인트로, GitHub Actions CI 2 job. 미룬 항목은 ADR-0033. CI 첫 실행에서 드러난 이식성 버그 수정: `std::filesystem::absolute("")` 가 MSVC STL 14.51(VS 18, GitHub runner) 에서 throw → `capabilities`/MCP `tools/list` 가 INTERNAL_ERROR (로컬 14.44 는 cwd 반환). `Project::create/load` 와 CLI 가 빈 경로를 `current_path()` 로 정규화 + 회귀 테스트. CI 주의점 2개 (pwsh 래퍼는 마지막 native 명령의 exit code 를 step 결과로 쓴다 → `exit 0` 명시; 헤드리스 빌드의 MCP tools/list 는 capture 가 빠져 14개). CI green 후 태그 v0.1.0 을 6ed8d4f 로 이동, zip 재생성·재업로드, 릴리즈 노트 갱신.
