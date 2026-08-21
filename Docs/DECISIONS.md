# DECISIONS — Architecture Decision Records

형식: **결정 / 상태 / 근거 / 대안 / 영향 / 설계 문서 참조**. 상태는 `가정`(사용자 확인 전, 구현은 진행) · `확정` · `폐기`.
새 결정은 맨 아래에 번호를 이어 붙인다. 결정을 뒤집으면 기존 항목을 지우지 말고 상태를 `폐기`로 바꾸고 새 ADR 을 가리킨다.
설계 문서 §88 의 "미결 설계 결정" 11개는 여기서 하나씩 닫힌다.

---

## ADR-0001 목표 게임 = 2D top-down arena ("TestArena")
- **상태**: 가정 (2026-08-21). 사용자가 §88.0 을 답하면 확정/수정.
- **결정**: 첫 게임은 2D top-down arena. 플레이어 WASD 이동, 근접 고블린 N마리, 충돌, 체력. Windows 전용, 싱글플레이, entity 수백 규모. 즉 설계 문서 §71 PoC 시나리오 그 자체.
- **근거**: §57 (2D-first 가 AI authoring 검증에 가장 빠름), §69 (규칙/데이터 중심 게임이 적합), §88.0 (이름이 있어야 physics/렌더/규모 결정이 닫힘).
- **영향**: Physics = Box2D (ADR-0003), 렌더 = sprite 2D (SDL_GPU), 파일 granularity = world 당 1 파일로 시작 (§88.9).
- **참조**: §57, §69, §71, §88.0

## ADR-0002 ECS substrate = Flecs v4.1.6 (기본 후보, spike 로 확정)
- **상태**: **확정** (2026-08-21, Phase 1). S-A 조건 충족: authoring JSON → PlayWorld(Flecs) → query/dump/snapshot 동작, 두 프로세스 finalHash 동일. EnTT arm(S-B)은 돌리지 않았다 — §3.1 규칙 "S-A가 동작하면 Flecs 채택".
- **결정**: runtime world 는 Flecs. authoring source of truth 는 여전히 우리 JSON 문서 모델(§6)이며 Flecs world 는 그 투영이다. Flecs REST/Explorer 는 play world 디버깅 전용 (authoring 변경은 CommandBus 만).
- **근거**: Flecs 가 reflection·query DSL·JSON·REST·Explorer 를 1st-party 로 제공해 §12–§19/§25/§26 의 60–70% 를 덮는다 (2026-08 리서치; 원문은 저장소 밖). EnTT v4 는 storage 만.
- **대안**: EnTT v4.0.0 (S-B) — Flecs 가 2주 내 동작하지 않을 때의 fallback. Godot headless / Bevy+BRP — §72 비교 arm 으로만.
- **영향**: C++17 이상, Flecs 64-bit entity id 는 외부 비노출(§7), archetype 특성상 빈번한 add/remove 는 field/CanToggle 로.
- **참조**: §3.1, §55, §88.10

## ADR-0003 2D physics = Box2D v3.1.1
- **상태**: 가정 (ADR-0001 에 종속). 목표 게임이 3D 로 바뀌면 Jolt v5.6.0.
- **결정**: `PhysicsWorld` 인터페이스(Step / CreateBody / Query / DrainContactEvents) 뒤에 Box2D. PoC 에서 두 엔진을 동시에 지원하지 않는다.
- **근거**: 설정 없이 cross-platform + thread-count 독립 bit-exact 결정성 (FAQ, CI test_determinism.c). C API 라 reflection/command 래핑이 쉽다.
- **대안**: Jolt (3D, SaveState 제공). Box3D (alpha — 금지).
- **영향**: rollback netcode 가 목표가 되면 재검토 (Box2D 는 SaveState 없음, §58).
- **참조**: §3, §22, §57, §58

## ADR-0004 컴파일러 = MSVC (VS2022 17.14 / 14.44) 기본, clang-cl 은 두 번째
- **상태**: 확정 (환경 확인 2026-08-21: MSVC 14.44, clang 22.1 설치됨).
- **결정**: 개발 빌드는 MSVC + Ninja (VS 번들 CMake 3.31.6 / Ninja 1.12.1). clang-cl 은 sanitizer / SARIF 진단 / C++26 실험용 CI 두 번째 컴파일러 (preset `clang-cl-debug`).
- **근거**: §88.5 권장 기본값. C++26 reflection 은 MSVC 미지원 → 매크로 registry (§42.2).
- **영향**: `/fp:precise` 가 det_fp_flags (VS2022+ 필수 — cmake/DetFpFlags.cmake 가 1930 미만이면 FATAL).
- **참조**: §41, §88.5

## ADR-0005 C++ 표준 = C++20
- **상태**: 확정.
- **결정**: `CMAKE_CXX_STANDARD 20`. glaze(C++23) 를 채택하면 23 으로 올린다.
- **근거**: §41 floor. Flecs C++ API 는 17, nlohmann 11, doctest 11 — 모두 호환.
- **참조**: §41, §42.1

## ADR-0006 의존성 관리 = CPM.cmake + 버전 고정 + 로컬 소스 캐시
- **상태**: 확정.
- **결정**: `cmake/CPM.cmake` (v0.43.1 vendored). `cmake/Dependencies.cmake` 에 태그 고정. `.cpm-cache/<name>/` 에 같은 태그의 shallow clone 이 있으면 `SOURCE_DIR` 로 쓰고 없으면 GitHub 에서 받는다. vcpkg 는 쓰지 않는다 (§41 "하나만").
- **근거**: 이 머신에 vcpkg 없음. CPM 은 단일 파일, 오프라인 캐시가 쉽다.
- **영향**: 새 머신은 `scripts/fetch-deps.ps1` 로 캐시 재생성 (또는 네트워크로 자동).
- **참조**: §3, §41

## ADR-0007 정적 링크만 (Game/ 포함)
- **상태**: 확정 (Phase 0–5). cr.h 도입 시 재검토.
- **결정**: `BUILD_SHARED_LIBS OFF`. Flecs/Box2D/SDL3 정적. Game/ 도 static lib.
- **근거**: §39/§88.5 — DLL 경계에서 reflection registry(static initializer)와 Flecs world 공유 문제를 검증하기 전까지 단순하게.
- **참조**: §39, §88.5

## ADR-0008 JSON 문서 타입 = nlohmann::ordered_json
- **상태**: 확정.
- **결정**: `akeir::Json = nlohmann::ordered_json`. 기본 `nlohmann::json` 은 키를 알파벳 정렬하므로 §5.3 키 순서 규약을 지킬 수 없다.
- **영향**: RFC 6901/6902 patch·diff 는 ordered_json 에서도 동작 (ChangeSet 구현에 사용).
- **참조**: §5.3, §78

## ADR-0009 로그 = OTel Logs Data Model 을 JSONL 로, SDK 없이
- **상태**: 확정.
- **결정**: `akeir::Logger` 가 `{ts, sev, level, event, body, scope, attrs}` 를 stderr/파일에 JSONL 로 쓴다. opentelemetry-cpp 는 가져오지 않는다. stdout 은 envelope 전용.
- **참조**: §28

## ADR-0010 오류 객체 = §79 Diagnostic + category/retryable/details
- **상태**: 확정 (설계 문서 v2 검증 반영 사항).
- **결정**: `akeir::CommandError` = `Diagnostic` + `category` + `retryable` + `details`. 필드명은 Diagnostic 과 동일 (`ruleId`, `message.text`, `logical`, `physical`, `fixes`, `fingerprint`, `helpUri`). exit code 표는 `akeir/core/ExitCodes.h`.
- **참조**: §13, §79

## ADR-0011 CLI 프로세스 모델 = Phase 0–3 은 one-shot in-process, Phase 4 부터 `akeir serve` 데몬
- **상태**: 확정 (Phase 4 구현: `akeir serve` 가 있으면 모든 `akeir <cmd>` 가 자동 포워딩, 없으면 one-shot. ADR-0029).
- **결정**: 지금은 `akeir <cmd>` 가 프로젝트를 직접 열고 닫는다. multi-call transaction(`tx begin … commit`)은 Phase 4 `akeir serve` 이후. Phase 3 의 transaction 은 in-process(test/Editor) 범위.
- **참조**: §9.1, §74 Phase 3/4, §88.1

## ADR-0012 Crash/행 진단 = Windows minidump + watchdog, exit 6/7
- **상태**: 확정 (구현·검증됨 2026-08-21).
- **결정**: `installCrashHandler` (SetUnhandledExceptionFilter + MiniDumpWriteDump → `<project>/Cache/crash/<stem>-<ts>.dmp`), `startWatchdog(--timeout)`. 둘 다 §13 envelope 을 stdout 에 쓰고 각각 exit 6 / 7. 마지막 64개 로그가 `error.details.lastLogs` 에 실린다.
- **참조**: §13, §88.4

## ADR-0013 ID = TypeID v0.3 grammar, authoring 은 UUIDv7, runtime-spawned 는 UUIDv8(결정적)
- **상태**: 확정 (구현·검증됨).
- **결정**: `akeir::Id`. 출력 소문자, 입력은 정규화. 검증기는 v7/v8 만 허용. runtime-spawned id 는 `Id::deterministic(prefix, worldSeed, tick, ordinal)`.
- **참조**: §7.1–§7.4

## ADR-0014 테스트 프레임워크 = doctest v2.5.3 (MSVC 14.44 에서 `DOCTEST_CONFIG_USE_STD_HEADERS` 필요)
- **상태**: 확정.
- **결정**: 단일 실행 파일 `akeir_tests.exe`, `doctest_discover_tests` 로 ctest 등록. MSVC 14.44 STL `<string_view>` 와 doctest 의 ostream 전방 선언이 충돌하므로 `DOCTEST_CONFIG_USE_STD_HEADERS` 를 정의한다.
- **참조**: Tests/CMakeLists.txt

## ADR-0015 Play world 의 component 는 reflection 으로 Flecs 에 동적 등록한다
- **상태**: 확정.
- **결정**: `PlayWorld` 가 `ComponentMeta`(size/align/ctor/dtor/copy/move)로 `ecs_component_init` + `ecs_set_hooks_id`. Flecs C++ API(`world.component<T>()`)는 쓰지 않는다 (`FLECS_NO_CPP`).
- **근거**: component 의 단일 진실 원천은 PropertyMeta 테이블(§42.2). Game/ 이 새 component 를 추가해도 ECS 코드가 바뀌지 않는다. 동적 등록이라 문자열 이름으로 `component(id, "Health")` 가 바로 된다 (§8 SetProperty, CLI/MCP 경로).
- **영향**: 훅 thunk 가 `type_info->hooks.binding_ctx` 로 meta 를 받는다. entity 순회는 Flecs 테이블 순서가 아니라 자체 정렬 id 목록(§22.2).

## ADR-0016 physics 가 body 를 가진 entity 의 Transform.x/y 를 소유한다
- **상태**: 확정 (Phase 1 모델).
- **결정**: system 은 `RigidBody2D.velocity` 만 쓴다. tick 마다 velocity → body, step, body → Transform.position.xy + velocity. 순간이동은 `physics().setTransform` (추후 `teleport` API).
- **근거**: 단순하고 결정적. Box2D 가 유일한 위치 적분기가 되어 두 소스가 다투지 않는다.

## ADR-0017 runtimeOnly 초기화는 spawn hook 으로 (Health.current = max)
- **상태**: 확정.
- **결정**: `PlayWorld::addSpawnHook(name, fn)` — 등록 즉시 기존 entity 에 적용, 이후 `spawn()` 마다 호출. Game/ 이 `HealthInit` 을 등록한다.
- **근거**: runtimeOnly 값은 authoring 파일에 없고 aggregate 기본값(100)이 max(30)와 달라 §18 lifecycle "init" 지점이 필요했다. component 에 생성자를 두면 aggregate 규칙(§42.2)이 깨진다.

## ADR-0018 ChangeSet 의 `before` 비교와 문서 동등성은 JCS(키 순서 무시)로 한다
- **상태**: 확정 (Phase 3).
- **결정**: `applyOps` 가 `remove/replace` 의 `before` 를 현재 값과 비교할 때 `ordered_json ==`(키 순서까지 비교)가 아니라 `jcsDump` 문자열로 비교한다. base 해시도 `sha256(JCS(canonicalizeDocument(doc)))`.
- **근거**: 문서가 파일로 갔다 오면 §5.3 규칙으로 키가 재정렬되어 같은 값인데 undo 가 `CHANGESET_BEFORE_MISMATCH` 로 실패했다 (CLI 프로세스 간 undo 에서 발견). 값 동등성이 의미이고 키 순서는 직렬화 세부다.
- **참조**: §5.3, §10.2, §78

## ADR-0019 `"/arr/-"`(append) 는 ChangeBuilder 가 구체 인덱스로 바꿔 기록한다
- **상태**: 확정.
- **결정**: RFC 6902 의 `-` 는 역연산(`remove /arr/-`)이 정의되지 않아 §78 규칙 1(self-inverting)을 깬다. `ChangeBuilder::add` 가 배열 길이로 치환한다. `document.patch` 로 들어온 raw op 도 같은 경로를 지난다.
- **참조**: §78 (▶ v3 주석)

## ADR-0020 commit 전 검증은 "새로 생긴 error" 만 거부한다
- **상태**: 확정.
- **결정**: `CommandBus::validateFork` 는 bus 생성 시점의 `Project::validate()` error fingerprint 를 baseline 으로 두고, fork 검증에서 baseline 에 없는 error 가 touched 문서에 생기면 `VALIDATION_FAILED` 로 commit 을 거부한다. `--no-validate`(`ExecOptions.validateAfter=false`) 로 우회. `validate --fix` 는 항상 끈다.
- **근거**: 이미 깨진 프로젝트에서도 편집이 가능해야 하고(그래야 고칠 수 있다), 반대로 AI 가 dangling ref 나 의존성 누락을 만드는 것은 commit 전에 막아야 한다 (§29 "오류는 저장 전에").
- **참조**: §29, §50

## ADR-0021 prefab 편집은 entity 명령과 같은 command 로 한다 (`entity` 인자에 prefab selector 허용)
- **상태**: 확정.
- **결정**: `component.add/remove`, `property.set` 의 `entity` 인자는 entity 또는 prefab 을 받는다. prefab 이면 문서 루트(`/components/...`)를, derived prefab(`base`)이면 `set/add/remove` 를 고친다 — 인스턴스와 같은 규칙(§78.1). 별도 `prefab.set_property` 류 command 를 만들지 않는다.
- **근거**: "고블린을 전부 빠르게" 는 prefab 한 줄 수정이어야 하고(§34), tool/command 수를 늘리지 않는다(§47). validate 의 fix 가 prefab 문서를 가리킬 때도 같은 command 가 동작해야 `--fix` 가 단순해진다.
- **참조**: §8, §34, §47, §78.1

## ADR-0022 `validate --fix` 는 CommandBus 로 적용하되 canonical 재직렬화만 예외
- **상태**: 확정.
- **결정**: MachineApplicable fix 는 ① `commands[]` 가 있으면 `apply`(atomic) ② 없고 `artifactChanges` 가 있으면 `document.patch` 로 CommandBus 를 거친다(history 에 남고 undo 가능, actor `cli:validate-fix`). JSON_NOT_CANONICAL 의 fix(`project.fmt`)만 JSON 값이 바뀌지 않아 ChangeSet 으로 표현할 수 없으므로 `Project::saveDocument` 로 직접 다시 쓴다. 같은 fix 가 여러 진단에 반복되면(prefab + 인스턴스들) 한 번만 적용한다. `COMPONENT_DEPENDENCY_MISSING` 의 fix 는 전이적 의존성(예: Movement → RigidBody2D → Collider2D)을 의존 대상부터 모두 add 한다.
- **참조**: §29, §79

## ADR-0023 assertion 표현식 = 자체 evaluator(CEL 부분집합), undefined 는 오류
- **상태**: 확정 (Phase 5).
- **결정**: §23.1 의 "고정 비교 문법"을 `akeir::expr::Expr`(~500 LOC) 로 직접 구현한다. cel-cpp/JSONPath/Luau 는 도입하지 않는다. 존재하지 않는 멤버·바인딩은 `has()` 안에서만 false 이고 다른 연산에 닿으면 `EvalError` → assertion 은 "evaluation error" 로 실패한다.
- **근거**: §23.1 결정 그대로(AI 가 가장 못 쓰는 것은 새 DSL; 비교기 수준은 CEL 호환 문법으로 충분). 오타(`player.Helth`)나 따옴표 누락(`== Dead`)이 조용히 false 가 되면 테스트가 거짓 실패/거짓 통과를 낸다 — §23 초안의 실수가 정확히 그것이었다.
- **참조**: §23.1, §61.1

## ADR-0024 "tick N 의 snapshot" = N tick 을 돌린 뒤 상태; always 위반은 run 을 중단한다
- **상태**: 확정.
- **결정**: `at: N` 과 `eventually` 는 N 번째 tick 이 끝난 뒤의 snapshot(`world.tick == N`)에서 평가한다(setup 직후 tick 0 상태는 평가 대상이 아니다; 필요하면 `at: 1`). `always` 는 첫 위반 tick 에서 run 을 중단하고 `abortedAt/abortReason` 을 기록, 남은 assert 는 중단 시점 snapshot 으로 평가하되 note 를 단다. 도달하지 못한 `at: N` 은 식의 값과 무관하게 실패.
- **근거**: §23.1 "always: 첫 위반에서 실패" + §24 의 `abortedAt` 필드. 중단 뒤 나머지를 평가해 두면 AI 가 한 번의 실행으로 더 많은 정보를 얻는다.
- **참조**: §23.1, §24

## ADR-0025 결정성 검사는 run-twice 만 (T0); 어긋나면 snapshot diff 를 낸다
- **상태**: 확정 (Phase 5). T1 은 엔진이 멀티스레드가 되면.
- **결정**: `determinism.runs ≥ 2` 면 같은 프로세스에서 시나리오를 다시 준비해 돌리고 `hashEvery` 마다 비교한다. 어긋나면 A 를 첫 divergent tick 까지 재실행해 두 snapshot 을 entity/path 단위로 diff 하고(`diff[]`, `firstDivergentSystem`) 둘 다 artifact 로 남긴다. `threads` 는 1 로 보고한다.
- **근거**: §24 "hash 가 다르다" 가 아니라 "몇 tick, 어느 entity 의 어느 property" 를 주기 위해. 재실행 비용은 수백 entity 규모에서 무시할 수준.
- **참조**: §22.2, §24

## ADR-0026 capture 는 SDL software renderer(CPU) 로 — 창·GPU 없이 결정적 PNG
- **상태**: 확정 (Phase 2).
- **결정**: `akeir capture` 와 테스트 capture assertion 은 `SDL_CreateSoftwareRenderer(SDL_Surface)` 로 그린다. SDL 은 `dummy` video driver 로 초기화(창 없음). 같은 world → 같은 PNG 바이트. 골든 이미지는 이 경로로만 만든다. §20 의 `offscreen` driver(EGL/GL)와 §27.1 의 software rasterizer(SwiftShader/WARP) 논의는 이 결정으로 대체한다 — 2D placeholder 렌더에는 CPU rasterizer 가 그대로 "software rasterizer" 다.
- **근거**: §27.1 "CI 에서는 software rasterizer, 골든은 같은 rasterizer 로" 를 가장 싸게 만족한다. GPU 드라이버/WARP/ICD 설치 문제가 없고 headless 머신에서도 같은 결과. 비용: 고해상도·텍스처가 많아지면 느리다(PoC 에서는 무시).
- **영향**: 창 모드(direct3d11 등)의 픽셀은 골든 비교에 쓰지 않는다. `capabilities.tools[].capture.enabled` 는 SDL 빌드에서만 true.
- **참조**: §20, §27, §27.1

## ADR-0027 렌더 API = SDL_Renderer (SDL_GPU 는 보류)
- **상태**: 가정 (Phase 2 PoC). 3D·셰이더·대량 스프라이트가 필요해지면 SDL_GPU 로 교체.
- **결정**: 2D placeholder 스프라이트(색 사각형/원)는 `SDL_Renderer` 로 그린다. 창 타깃은 SDL 기본 백엔드(Windows: direct3d11) + vsync, capture 타깃은 software (ADR-0026). 같은 `Renderer2D` 코드가 두 타깃을 그린다.
- **근거**: §3 표는 "SDL_GPU 우선" 이지만 PoC 의 목적은 AI authoring 루프 검증이지 렌더 품질이 아니다(§57 2D-first). SDL_Renderer 는 API 가 작고 software 백엔드가 있어 결정적 capture 를 공짜로 준다. SDL_GPU 는 software 백엔드가 없어 §20/§27.1 의 WARP/SwiftShader 문제가 생긴다.
- **참조**: §3, §27.1, §57

## ADR-0028 테스트의 `requires: ["renderer"]` — 환경이 못 주면 errored 가 아니라 skipped
- **상태**: 확정.
- **결정**: capture assertion 을 가진 테스트는 `"requires": ["renderer"]` 를 선언한다. `CaptureHook` 이 없는 빌드(msvc-headless)에서는 `skipped`(사유 포함)로 보고하고 exit code 에 영향을 주지 않는다. 선언 없이 capture 를 쓰면 `TEST_CAPTURE_REQUIRES_RENDERER` 로 실패한다(§20 의 규칙 유지).
- **근거**: headless 빌드에서 전체 스위트가 빨갛게 되면 AI 가 "렌더가 없어서" 와 "게임이 깨져서" 를 구분하지 못한다. 선언은 테스트 파일 한 줄이다.
- **참조**: §20, §23, §24

## ADR-0029 serve 전송 = loopback TCP 위 NDJSON JSON-RPC 2.0 + per-session token (HTTP 아님)
- **상태**: 확정 (Phase 4). Streamable HTTP 는 외부 도구가 필요해질 때.
- **결정**: `akeir serve` 는 127.0.0.1:<port>(기본 OS 선택) 에서 **한 줄 = 한 JSON-RPC 메시지** 로 듣는다. 요청마다 `token`(프로세스 시작 시 생성, `Cache/serve.json` 에 기록) 을 요구한다. `method` 는 CLI command id(`params.argv`) 또는 bus command id(`params.args`); 응답은 `{envelope, exitCode}`. `--stdio` 는 같은 프로토콜을 stdin/stdout 으로(token 없음). 외부 HTTP 라이브러리를 들이지 않고 Winsock 만 쓴다.
- **근거**: §88.1 "stdio(로컬 CLI) + localhost JSON-RPC" 와 §46.2 "loopback + per-session token" 을 가장 작은 코드로 만족한다. 클라이언트는 CLI 자신과 Editor/MCP 뿐이라 HTTP 의 이점(브라우저, 프록시)이 없다. 같은 envelope 이 CLI stdout 과 RPC result 에 그대로 쓰인다(§12 "같은 구조").
- **영향**: `Cache/serve.json` 을 읽을 수 있는 로컬 사용자만 접근 가능. `--bind 0.0.0.0` 은 제공하지 않는다. 데몬은 단일 스레드, 연결 순차 처리.
- **참조**: §46.2, §88.1, §88.6

## ADR-0030 MCP 서버는 C++ 네이티브(`akeir mcp`, stdio) — 공식 SDK sidecar 대신
- **상태**: 확정 (Phase 7 PoC). 클라이언트 호환 문제가 생기면 TypeScript SDK sidecar 로 감싼다(§46.1 원안).
- **결정**: `akeir mcp` 가 ServeHost(단일 writer) 위에서 MCP 메서드(`server/discover`(2026-07-28), `initialize`(2025-xx 호환), `ping`, `tools/list`, `tools/call`, 빈 `resources/list`·`prompts/list`)를 newline-delimited JSON-RPC 로 직접 말한다. tool 은 `capabilities.tools[]` 의 `enabled` 항목을 그대로 노출(§15 pass-through), `tools/call` 결과는 `structuredContent = envelope`, `isError = !ok` (§46.2).
- **근거**: 프로토콜 표면이 작고(메서드 5개) 우리 envelope 이 이미 MCP 결과 모양이라 SDK 가 줄여 주는 코드가 거의 없다. Node/Python 런타임 의존이 사라져 "`akeir` 하나로 전부" 가 유지된다. 2026-07-28 의 stateless 설계(§9.1 handle 인자)는 우리 tx/run handle 과 그대로 맞는다.
- **영향**: resources/prompts/progress/Tasks 는 미구현. tool 인자 스키마 검증은 하지 않는다(CLI 인자로 번역 후 명령이 검사).
- **참조**: §46, §46.1, §46.2, §47

## ADR-0031 serve 의 상주 상태는 authoring 모델뿐; 읽기 명령은 복사본, 쓰기는 단일 bus
- **상태**: 확정 (Phase 4).
- **결정**: `ServeHost` 는 `Project`(authoring) + `CommandBus` + run registry 를 든다. 쓰기 명령은 `Context::residentBus` 로 단일 writer 를 쓰고, 읽기/실행 명령(`validate`, `run`, `test`, `query`, `dump`)은 `Context::resident` 의 **복사본** 위에서 동작한다(play world 는 호출마다 새로 build). actor 는 요청마다 호출자의 `--actor`(기본 `cli`)로 설정된다.
- **근거**: Project 복사는 수백 entity 에서 ms 단위라 일관성(쓰기 중 읽기 없음)을 공짜로 얻는다. play world 상주(`run.step`)는 §88.2 의 promote 규칙과 함께 별도 설계가 필요해 미룬다.
- **참조**: §88.1, §88.2

## ADR-0032 이름 = AKEIR Engine (실행 파일 `akeir.exe`); 코드 접두어 `pme` → `akeir`; 릴리즈 = git tag `v0.1.0` + zip
- **상태**: 확정 (2026-08-22, 사용자 결정). 2026-08-21 에 AKEIR Engine/ME 로 정했다가 `moltengine.ai` 가 존재해 같은 날 개명.
- **결정**: 엔진 이름 **AKEIR Engine**(표기 AKEIR, 발음 에이키어). 어원은 그리스어 ἀχειροποίητος(acheiropoiētos, "사람 손으로 만들어지지 않은")의 앞부분 ἄχειρ(acheir, "손이 없는") — "인간의 손을 탈피한다(Molt of the human hand)"는 원래 의도를 한 단어로 담는다. 실행 파일은 `akeir.exe` → **`akeir.exe`** (엔진을 부르는 이름이 곧 CLI 이름; `akeir run`, `akeir mcp`). 코드 접두어도 통일한다 — C++ 네임스페이스 `akeir::`, 헤더 `#include "akeir/..."`, CMake 타깃 `akeir_core`/`akeir::core`, 테스트 `akeir_tests.exe`, 매크로 `AKEIR_*`(`AKEIR_REFLECT_*`, `AKEIR_LOG`, `AKEIR_WITH_SDL` …). (같은 날 오전의 첫 결정은 구 접두어 `pme`(Project ME) 유지였으나, 외부 리뷰가 지적한 대로 사용자가 없는 지금이 public API 를 바꾸는 가장 싼 시점이라 전부 개명했다.) 저장소 디렉터리(`Project_ME`)와 샘플 게임 네임스페이스 `game::` 만 그대로. 사용자에게 보이는 곳(`akeir version`, envelope `meta.engine`, MCP serverInfo, README/Docs, 설계 문서 `AKEIR.md`)도 AKEIR. 버전 정본은 git tag `v0.1.0` 하나(약어 태그 없음); 릴리즈는 `scripts/package.py` 가 만드는 zip(`git archive` + `bin/akeir.exe` + 상대 경로 `.mcp.json` + QUICKSTART; 리서치 자료·`.pdb` 제외).
- **근거**: 웹/GitHub 검색에서 "Akeir Engine"/`AkeirEngine` 충돌 0건(2026-08-22). 다른 탈피 계열 후보(AKEIR Engine, Exuvia, Ecdysis, Instar, Apolysis)는 제품·게임·라이브러리와 충돌하거나(Instar 는 한국어로 인스타그램 연상) 발음이 어려웠다. 공개 저장소: https://github.com/Joesaeng/AkeirEngine. 접두어까지 바꾼 근거: 외부 아키텍처 리뷰(2026-08-22) — "public API 에 옛 코드네임이 남으면 나중에 breaking change 가 된다".
- **참조**: §72, Docs/00-START-HERE.md, QUICKSTART.md

## ADR-0033 라이선스 = MIT; 저장소 위생(THIRD_PARTY_NOTICES, CONTRIBUTING, CI, portable `.mcp.json`)
- **상태**: 확정 (2026-08-22, 외부 아키텍처 리뷰 P0/P1 반영).
- **결정**: `LICENSE` = MIT (저작권자 Cho Seongmin). 정적으로 링크하는 의존성(Flecs/Box2D/nlohmann/doctest MIT, SDL3 zlib, CPM MIT)은 `THIRD_PARTY_NOTICES.md` 에 표로 고지하고 릴리즈 zip 에 포함한다. `CONTRIBUTING.md` 에 깨면 안 되는 원칙 4개(source of truth, CommandBus 경유, headless/어댑터, 결정론)를 적는다. `.mcp.json` 은 머신별 파일이라 gitignore 하고 `.mcp.json.example`(상대 경로) + `akeir mcp --print-config` 로 만든다. GitHub Actions(`.github/workflows/ci.yml`): headless(빌드·doctest·ctest 등록 수 = doctest 수 검사·validate·시나리오·run-twice 결정론·빈 프로젝트 round-trip) + SDL(빌드·테스트·golden capture·MCP tools/list = 15·release zip artifact). 참조 finalHash 와의 차이는 **경고**(컴파일러 버전이 다르면 T0 는 유지되어도 값은 다를 수 있다, §22.2).
- **리뷰에서 거른 것(기록)**: Project fork 의 copy-on-write·증분 reindex·증분 validation(측정 전 최적화 금지 — 리뷰도 동의), CLI/MCP 읽기 경로의 서비스 API 추상화(Phase 6~7 시점), Phase 6 Editor, AgentBench 스위트, `akeir trace`, cook/binary 런타임 포맷, `akeir setup mcp`(`--print-config` 로 충분). 전부 `Docs/STATUS.md` "다음 할 일" 에만 둔다.
- **참조**: §41, §46.2, §72
