# ARCHITECTURE — 코드 ↔ 설계 문서 대응

설계 정본은 `../AKEIR.md`. 이 문서는 **어느 코드가 어느 § 를 구현하는지**와 모듈 간 의존 방향만 적는다.
각 `Engine/<모듈>/README.md` 가 모듈 내부 구조와 구현 범위를 더 자세히 적는다.

## 레이어와 의존 방향 (§4, §76)

```
Tools/CLI (akeir.exe) ─┐   game <cmd> (one-shot)  /  akeir serve (상주, RPC)  /  akeir mcp (stdio MCP)   — 셋 다 같은 CommandSpec 표 + ServeHost
Tools/Editor (Phase 6)├──▶ Engine/Commands ──▶ Engine/Runtime ──▶ Engine/Core
(MCP 는 CLI 안, 별도 sidecar 없음)┘          │                   │
                                 │                   ├──▶ Engine/ECS (Flecs)     runtime 투영
                                 │                   ├──▶ Engine/Physics (Box2D)
                                 ▼                   └──▶ Engine/Serialization ──▶ Engine/Reflection ──▶ Core
                          Engine/Validation
Tools/CLI ──▶ Engine/Testing ──▶ ECS (WorldFactory 주입; Game/ 모름)
Tools/CLI ──▶ Engine/Platform ──▶ Engine/Render ──▶ ECS   (SDL3; AKEIR_WITH_SDL 빌드만. sim 은 이 둘을 모른다)
Game/Source ──▶ Engine/Runtime (Core 가 Game 을 알면 안 된다, §76)
```

규칙: Tools 는 Commands 만 호출한다 (§32, §46). Commands 는 authoring 문서 모델(Runtime)을 바꾸고 ChangeSet 을 남긴다. ECS/Physics 는 runtime 투영이며 source of truth 가 아니다 (§88.2).

## 모듈 표

| 타깃 | 디렉터리 | 구현하는 § | 상태 |
|---|---|---|---|
| `akeir_core` | `Engine/Core` | §7 ID · §13 exit code/envelope · §22.2 RNG/Time/Hash · §28 Log · §79 Diagnostic · §88.4 Crash | 구현됨 (Phase 0) |
| `akeir_reflection` | `Engine/Reflection` | §42.2 PropertyMeta registry, §43.1 속성 어휘, AKEIR_REFLECT_* | 구현됨 (Phase 1) |
| `akeir_serialization` | `Engine/Serialization` | §5.3 canonical JSON / JCS, §14 검증 코드, §26.1 visibility mask | 구현됨 (Phase 1) |
| `akeir_runtime` | `Engine/Runtime` | §6 authoring document model (Project), §20.1 fixed-tick Application, §34 prefab resolve, §29 validate, §19 reference graph(`referencesTo/From`), 내장 component | 구현됨 (Phase 0/1) |
| `akeir_ecs` | `Engine/ECS` | §3.1 Flecs 투영 (PlayWorld), §16 query, §25 dump, §26.1 snapshot, §57 physics sync | 구현됨 (Phase 1) |
| `akeir_physics` | `Engine/Physics` | §57 PhysicsWorld + Box2D v3.1.1, §22.2 contact 정렬 | 구현됨 (Phase 1) |
| `akeir_commands` | `Engine/Commands` | §8 CommandBus/CommandKind/ChangeBuilder · §9 Transaction/journal · §10 Undo/History · §49 apply · §50 dry-run · §78 ChangeSet · §78.1 override 매핑 | 구현됨 (Phase 3) — [README](../Engine/Commands/README.md) |
| `akeir_testing` | `Engine/Testing` | §23 Test Scenario 러너 · §23.1 assertion 표현식(Expr) · §24 results.json/JUnit · §22.2 run-twice + snapshot diff | 구현됨 (Phase 5 테스트 부분) — [README](../Engine/Testing/README.md) |
| `akeir_platform` | `Engine/Platform` | §20 SDL3 init/창/driver 보고 · §88.3 InputMap · §20.1 창 모드 accumulator 루프 + `--record` | 구현됨 (Phase 2, `AKEIR_WITH_SDL`) — [README](../Engine/Platform/README.md) |
| `akeir_render` | `Engine/Render` | §27 Renderer2D(SDL_Renderer; software capture) · §27.1 compareCaptures | 구현됨 (Phase 2, `AKEIR_WITH_SDL`) — [README](../Engine/Render/README.md) |
| `akeir_validation` | `Engine/Validation` | §29 rule registry, SARIF 출력 | 예정 — 지금은 `Project::validate()` + `validateComponentJson` 이 규칙이고 `--fix` 는 CLI 가 CommandBus 로 적용한다 |
| `akeir` | `Tools/CLI` | §11 CLI · §12 envelope/TTY · §13 exit code · §15 capabilities(tools 15 + busCommands) · 읽기: run/dump/query/validate/fmt/schema/explain/entity list · 쓰기(MutationCommands.cpp, 전부 CommandBus 경유): entity/component/set/tag/prefab/world/apply/undo/redo/history/cmd · `validate --fix` · `test`(TestCommands.cpp) · `capture`/`input map`/창 모드 `run`(SdlCommands.cpp, SDL 빌드) · `serve`/`serve status|stop`/`tx *`/`run status`(Serve.cpp) · `mcp`(Mcp.cpp) | 구현됨 (Phase 0–5, 7 명령; one-shot 또는 serve 포워딩) |
| `akeir_game` | `Game/Source` | 샘플 게임 component(Health/Movement/PlayerController/EnemyAI) + systems (§60, §71) | 구현됨 (Phase 1) |
| `akeir_tests` | `Tests` | 각 모듈 단위 테스트 | 74 케이스 (Core/Runtime/Reflection/Serialization/ECS/Commands/Testing + Render[SDL]) |

## Core 모듈 파일 ↔ §

| 파일 | § | 한 줄 |
|---|---|---|
| `akeir/core/Json.h` | §5.3 | `akeir::Json = nlohmann::ordered_json` (키 순서 보존) |
| `akeir/core/Id.h/.cpp` | §7.1–7.4 | TypeID grammar, base32, UUIDv7 단조 생성, UUIDv8 결정적 파생, parse/validate/short-form |
| `akeir/core/Hash.h/.cpp` | §22.2, §37, §52 | FNV-1a 64, SplitMix64, `Hasher`(float bit pattern), SHA-256 |
| `akeir/core/Rng.h/.cpp` | §22.2 | xoshiro256** + SplitMix64 seeding, (worldSeed, name) 스트림, state 노출, jump |
| `akeir/core/Time.h` | §22.2 | `SimTime`(tick, dt 상수) / `WallTime`(ns, ISO-8601) / `Stopwatch` |
| `akeir/core/Log.h/.cpp` | §28 | OTel 필드 JSONL, stderr/파일/ring sink, `AKEIR_LOG` 매크로 |
| `akeir/core/Diagnostic.h/.cpp` | §79 | Diagnostic/Fix/Applicability/Location, toJson/fromJson, fingerprint |
| `akeir/core/Envelope.h/.cpp` | §12, §13 | Envelope, CommandError, ErrorCategory → exit code |
| `akeir/core/ExitCodes.h` | §13 | exit code 표 |
| `akeir/core/Crash.h/.cpp` | §88.4 | minidump 핸들러, watchdog, crash/timeout envelope |

## 현재 구현된 흐름 (Phase 1 + 3)

```
읽기 경로 (Phase 1)
Game/*.json ──Project::load──▶ Project (documents + id index)
                                  │ resolveEntityComponents (prefab chain, §34)
                                  ▼
                      PlayWorld::build (Flecs entities + dynamic components + Box2D bodies)
                                  │ Application::runHeadless (fixed tick, hashEvery)
                                  ▼
                      systems (Game/) → velocity → Box2D step → Transform/velocity → contact events
                                  ▼
                      hash / snapshot / dumpEntity / query   ──▶  CLI envelope (stdout)

쓰기 경로 (Phase 3)  — 파일을 바꾸는 엔진 경로는 이것과 `akeir fmt` 뿐
CLI (akeir set / entity create / apply / undo …) ──▶ CommandBus::execute|apply|undo
   ──▶ fork(Project 복사) ──▶ handler (BuiltinCommands) ──▶ ChangeBuilder(ops + before) ──▶ validateFork(새 오류만 거부)
   ──▶ commit: base 해시 검사 → Cache/journal → 메모리 applyOps → canonical temp+rename → Cache/history → journal 삭제
   ──▶ envelope { result, changes[] (ChangeSet ops), meta.changeSet }
```
`--dry-run` 은 commit 직전에서 멈추고 `changes[]` 만 돌려준다(§50). undo 는 history 항목의 `inverse()` 를 같은 commit 경로로 적용한다(§10.1).

```
테스트 경로 (Phase 5)
Game/Tests/**/*.test.json ──TestScenario::fromJson──▶ TestRunner::run
   ──▶ WorldFactory(PlayWorld::build + Game systems) ──▶ setup(spawn/bind) ──▶ inputs → InputFrame 맵
   ──▶ tick 루프: snapshot → Expr 평가 (always / eventually / at)  ──▶ failures[] (tick, bindings, snapshot artifact)
   ──▶ run-twice: hash 비교 → 어긋나면 divergent tick 의 snapshot diff
   ──▶ results.json (+ JUnit) ──▶ CLI envelope (exit 3 on failure)

렌더/capture 경로 (Phase 2, SDL 빌드)
akeir run (창)     : Platform(window) + Renderer2D(window, vsync) + InputMap(Config/input.json) ──▶ runInteractive: accumulator → world.tick(InputFrame) → render → present
akeir capture      : Platform(dummy, 창 없음) → N tick → Renderer2D::createSoftware(w,h).render → SDL_SavePNG   (CPU, byte-deterministic)
akeir test capture : 위 software 경로를 CaptureHook 으로 주입 → Tests/Golden/<test>/<name>_<WxH>.png 과 compareCaptures

상주 경로 (Phase 4/7)
akeir serve : ServeHost{Project, CommandBus(단일 writer), run registry} ◀── 127.0.0.1 NDJSON JSON-RPC (token) ◀── game <cmd> (얇은 클라이언트: Cache/serve.json 보고 포워딩)
                                                                  ◀── --stdio (Editor)
akeir mcp   : relay(adapter, McpAdapter.cpp) ◀── stdin/stdout MCP ── 자식 `akeir.exe mcp --worker`(같은 ServeHost, Mcp.cpp: server/discover | initialize, tools/list, tools/call → structuredContent = envelope). akeir.exe 가 재빌드되면 worker 만 교체 (ADR-0034)
tx: serve 의 bus 가 fork 를 들고 있다 → `--tx` 가 붙은 명령은 fork 에, `tx commit` 이 compose + commit (§9.1/§9.2)
```

## 데이터 흐름 (목표 상태, §4 · §88.1 · §88.2)

```
Game/ (JSON: project.json, Worlds/*.world.json, Prefabs/*.prefab.json, Data/, Config/)
   │  load (Serialization, schema 검증)
   ▼
Authoring document model (Runtime::Project)  ◀── CommandBus (Mutation → ChangeSet → journal → temp+rename 저장)
   │  build play world                               ▲
   ▼                                                │ CLI / Editor / MCP sidecar / file watcher
Play world (ECS: Flecs) + PhysicsWorld (Box2D)      │
   │  tick loop (fixed dt, CommandApply 단계)        │
   ▼                                                │
snapshot / dump / trace / capture / test results ───┘ (Query, RuntimeControl)
```

## 용어

- **authoring world / play world**: §88.2. 파일에 있는 것 vs 실행 중인 것. play 변경은 `promote` 없이는 authoring 에 닿지 않는다.
- **command id**: `<noun>.<verb>` (예 `entity.create`). envelope.command, apply.changes[].op, ChangeSet.intent.op 모두 이것. BRP 이름은 alias (§8.1).
- **tool**: MCP 에 노출되는 16개 (§47 의 15개 + `play`, ADR-0041). command 와 1:1 이 아니다.
- **ChangeSet**: §78. RFC 6902 superset. `replace` 의 새 값은 `value`, 옛 값은 `before`. `doc` 으로 문서를 가리키고 `path` 는 그 문서 안 JSON Pointer. `before` 비교는 JCS(ADR-0018).
- **actor**: history 항목의 주체 문자열 (`cli`, `cli:validate-fix`, `ai:claude#42`, `human:editor`). `--actor` 로 지정, undo 필터에 쓴다.
- **selector**: §7.4. id / id prefix / `name:X` / `path:World/Parent/Child`. Mutation command 의 `entity` 인자는 prefab selector 도 받는다(ADR-0021).
- **handle**: 서버가 발급하는 불투명 id — `tx_…`(TTL), `run_…`(serve 세션 동안), `cs_…`(history 영구). 모르는/만료된 handle 은 `*_UNKNOWN_OR_EXPIRED` (§9.1).
- **serve / one-shot**: `Cache/serve.json` 이 있고 연결되면 CLI 는 클라이언트, 아니면 프로세스 안에서 직접 실행. `meta.via = "serve"` 로 구분.
