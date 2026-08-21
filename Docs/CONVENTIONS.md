# CONVENTIONS — 코드 규약

설계 문서 §5.3 (JSON), §7 (ID), §22.2 (결정론), §42.2 (component 작성 규칙), §59–§62 (Game/ 경계, lint).

## 파일 / 네임스페이스

- 네임스페이스 `akeir`. 모듈별 하위 네임스페이스는 두지 않는다 (CLI 만 `akeir::cli`).
- 헤더는 `Engine/<Module>/include/akeir/<module>/Name.h`, 소스는 `Engine/<Module>/src/Name.cpp`. include 는 `"akeir/core/Id.h"` 형태.
- 모든 헤더 첫 줄 주석: 파일 역할 + **설계 문서 § 번호**. 예: `// akeir/core/Id.h — Persistent ID. 설계 문서 §7.1`.
- 주석은 한국어 가능, 식별자는 영어. 문자열 리터럴(로그 body, 진단 message)은 영어 — agent 가 읽는다.
- UTF-8, LF, 4-space indent (C++), 2-space (CMake/JSON). 배치 파일(.cmd)은 ASCII 만.

## C++

- C++20. 예외는 I/O 경계(파일 파싱)에서만 잡고, 엔진 API 는 `std::optional` / `Result`-스타일로 실패를 돌려준다.
- 컴포넌트 struct 는 **aggregate** 로 유지한다 (생성자·virtual·private 멤버 없음, ≤128 멤버) — §42.2. 나중에 glaze 무매크로 reflection 을 열어 두기 위해.
- property 키는 camelCase, component 타입은 PascalCase, command id 는 `<noun>.<verb>` (§6, §8.1).
- enum → 문자열 함수는 `severityName()`, `propTypeName()` 처럼 `<thing>Name()` 으로 짓는다. **`toString()` 이라는 이름은 금지** — doctest 가 unqualified `toString(x)` 를 ADL 로 찾아 `const char*` 를 받아 컴파일이 깨진다 (`error C2110: '+': 두 포인터`).
- vec2/3/4·quat·color 는 고정 길이 배열 leaf 타입. `/position/0` 처럼 index 로 접근 (§14.1).

## 결정론 (sim 코드 = Engine/Runtime, Engine/ECS, Engine/Physics, Game/)

§22.2 체크리스트를 코드 규칙으로 옮긴 것. `akeir lint`(§62, 예정)가 검사한다.

- wall-clock 금지: `std::chrono::*_clock`, `SDL_GetTicks`, `time()` 는 sim 코드에서 호출하지 않는다. `SimTime` 만 받는다.
- RNG 는 `akeir::RngStream` 만. `rand()`, `std::random_device`, `std::mt19937` 금지. (authoring ID 발급 `Id::generate` 만 예외 — sim 밖이다.)
- `std::unordered_map/set` 순회 금지, `std::hash` 금지, 포인터 값으로 비교/정렬 금지, 동률 가능한 키에 `std::sort` 금지(`std::stable_sort` 또는 total order).
- `std::sin/cos/tan/atan2/exp/pow` 금지 → `akeir::det::Sin/Cos/...` (예정). `std::sqrt` 는 허용.
- float 해시는 bit pattern (`Hasher::f32`).
- 병렬 시스템 출력은 `(entityId, seq)` 로 정렬 후 적용.
- 모든 sim 타깃은 `det_fp_flags` 를 link 한다.

## JSON (§5.3)

- 문서 타입은 `akeir::Json` (ordered_json). 출력은 단 하나의 canonical serializer(`akeir_serialization`, `writeCanonicalFile`)로만 — 직접 `dump()` 해서 프로젝트 파일을 쓰지 않는다.
- `ordered_json` 의 `==` 는 키 순서까지 비교한다. 값 동등성은 `jcsDump(a) == jcsDump(b)` (ADR-0018).
- 파일에 주석 없음. 메모는 `description`/`notes` 필드.
- ID 는 파일 안의 `"id"`; 외부 자산은 `<file>.meta.json` sidecar.

## 쓰기 경로 (§8, §78)

- authoring 문서(`Game/*.json`)를 바꾸는 코드는 `Engine/Commands` 의 command handler 뿐이다. `Project::documentMut()` 를 Commands 밖에서 쓰지 않는다 (`akeir fmt` 의 canonical 재직렬화만 예외).
- 새 Mutation command 는 `Engine/Commands/src/BuiltinCommands.cpp` 에 `CommandDef{id "<noun>.<verb>", kind, description, argsSchema, aliases, handler}` 로 등록한다. handler 는 `ctx.changes`(ChangeBuilder)로만 바꾸고, 실패는 `ctx.fail(category, RULE_ID, 고치는 법)` 으로 보고하며 `false` 를 돌려준다. 부분 적용 걱정은 없다 — 실패하면 fork 가 버려진다.
- handler 는 `ctx.project.resolveSelector/locate/resolveEntityComponents` 로 읽고, 값 검증은 `validateComponentJson`(reflection). 문서 수준 규칙은 commit 전 `validateFork` 가 본다.
- 인스턴스/derived prefab 은 직접 값을 쓰지 않고 `set/add/remove` 맵을 고친다 (§78.1 표는 `Engine/Commands/README.md`).
- CLI 쓰기 명령은 `Tools/CLI/src/MutationCommands.cpp` 에 sugar 만 둔다 — 인자 조립 후 `bus.execute(id, args)`. 로직을 CLI 에 두지 않는다 (Editor/MCP 가 같은 command 를 써야 한다).

## 플랫폼 / 렌더 (§20, §27)

- SDL 을 아는 코드는 `Engine/Platform`, `Engine/Render`, `Tools/CLI/src/SdlCommands.cpp` 뿐이다. sim(`Engine/Runtime`, `Engine/ECS`, `Game/`)과 `Engine/Testing` 은 SDL 헤더를 include 하지 않는다 — 렌더가 필요한 곳은 훅(`WorldFactory`, `CaptureHook`)으로 주입받는다.
- 두 모듈은 `if(AKEIR_WITH_SDL)` 안에서만 추가되고, CLI 는 `AKEIR_HAS_SDL` 매크로로 분기한다. SDL 없는 빌드에서도 같은 command 표가 유지되어야 한다(`FEATURE_UNAVAILABLE`).
- 골든/비교용 픽셀은 software renderer 로만 만든다 (ADR-0026). 창 백엔드 픽셀을 테스트에 쓰지 않는다.

## 로그 / 출력 (§12, §28)

- stdout 에는 envelope 만. 로그는 `AKEIR_LOG(level, scope, name, body, attrs)` → stderr JSONL.
- 게임 전용 attrs 키는 `game.` 접두어.
- 진단은 `akeir::Diagnostic` — `ruleId` 는 SCREAMING_SNAKE, `message.text` 는 "고치는 법"을 말한다.

## 테스트

- 모듈마다 `Tests/<Module>_<Topic>.cpp`. 첫 줄에 §. TEST_CASE 이름에 `;` 를 쓰지 않는다 (ctest 등록이 쪼개진다; `—` 를 쓴다).
- 결정론에 영향을 주는 값(RNG 첫 출력, 해시 상수 등)은 regression 테스트로 고정한다 — 바뀌면 snapshot/replay 호환이 깨진다는 것을 테스트 이름에 적는다.

## 문서

- 결정 → `Docs/DECISIONS.md` ADR. 진행 → `Docs/STATUS.md`. 설계 변경 → 설계 문서 해당 § 에 `▶ v3` 표기.
- 새 모듈을 만들면 `Engine/<Module>/README.md` 를 함께 만든다: 역할, § 대응, 공개 API 요약, 구현 범위/미구현.
- **언어**: 모든 `README.md`, `QUICKSTART.md`, CONTRIBUTING, CLI 출력·오류 메시지·스키마는 **영어**. `Docs/*.md` 와 설계 문서 `AKEIR.md` 는 한국어(작업 언어) — 2026-08-22 통일.
