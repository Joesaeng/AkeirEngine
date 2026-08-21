# MoltEngine (ME) 설계 문서 — AI-Native Game Framework
## Unreal / Unity의 인간 중심 에디터를 우회하지 않고, AI가 직접 조작하는 게임 제작 환경

> 상태: Architecture / PoC Design Draft **v2 (리서치 보강판, 검증 반영)** + ▶ v3 구현 주석 (§89 끝, Phase 3 까지)  
> 작성 기준일: 2026-08-21 (초안) / 2026-08-21 (보강)  
> 목표: Unreal/Unity를 즉시 대체하는 범용 게임 엔진이 아니라, **AI가 코드·데이터·월드·테스트를 직접 조작하기 쉬운 개인용 게임 프레임워크**의 설계 방향을 정의한다.

> **v2 보강 안내**  
> 초안의 주장을 11개 주제(C++ reflection, Flecs/EnTT, Bevy BRP, Godot, 2025–26 AI 게임툴 지형, MCP 규격·CLI 설계, 결정론, ChangeSet/Undo, 라이브러리 스택·스크립팅, 테스트·관측성, 스키마·ID·에셋 파이프라인)로 나눠 1차 출처 기반으로 검증했다. 결과는 각 절에 `▶ v2` 표기로 반영했고, 출처와 변경 요약은 §87·§89, 아직 결정되지 않은 설계 항목은 §88에 모았다.  
> 가장 큰 변화는 §0.1에 요약했다: **이 문서의 아키텍처 원칙은 이미 업계가 수렴한 방향이며, 진짜 차별점은 더 좁고 날카롭다.**

---

# 0. 결론부터

현재의 문제를 다음처럼 정의한다.

> **문제는 MCP가 아니다.**  
> 문제는 AI가 인간을 위해 설계된 Unreal/Unity Editor를 MCP를 통해 간접 조작해야 한다는 것이다.

현재 작업 구조는 대체로 다음과 같다.

```text
AI
 ↓
MCP
 ↓
MCP 서버
  (2026: 1st-party — Unreal MCP [UE 5.8, 2026-06],
         Unity MCP Server [Unity AI, 2026-05],
         Roblox Studio 내장 MCP [2026-04]
         / 또는 community 서버)
 ↓
Editor API / Python / C# / Blueprint / UObject
 ↓
Scene / Actor / Component / Asset
 ↓
엔진 내부 직렬화 포맷
```

▶ v2: 초안은 세 번째 상자를 "Unity / Unreal용 (서드파티) MCP 서버"로 적었다. 2026-08 현재 MCP 서버는 엔진 벤더가 직접 제공한다. Epic은 UE 5.8(2026-06-17)에 Editor 내장 **Unreal MCP**(Experimental, loopback HTTP+SSE, `UFUNCTION(meta=(AICallable))`로 tool 노출, cooked build에서도 호스팅 가능)를 넣었고, Unity는 Unity AI(6.3+, 2026-05 open beta)에 공식 MCP Server와 승인 기반 Agent mode를 포함했으며, Roblox는 standalone 서버를 archive하고 Studio 내장 MCP로 옮겼다. **그럼에도 이 구조에서 AI가 조작하는 대상은 여전히 인간용 Editor의 object model(UObject / GameObject / Blueprint graph)이다.** Epic 문서는 Unreal MCP를 Experimental로 두고 "많은 기능이 미완성·누락"이라고 명시하며, community 서버들은 Blueprint graph 편집에서 반복적으로 실패를 보고한다. 따라서 아래의 문제 정의는 유효하다. 단, 비교 대상이 "community 서버"가 아니라 "1st-party 기능"으로 바뀌었다는 점은 §72·§82에 반영했다.

이 구조에서는 AI가 게임 데이터를 수정하기 위해 다음을 모두 알아야 한다.

- 엔진 고유 Editor API
- 에디터 상태
- 현재 선택된 오브젝트
- Asset Database / Asset Registry
- UObject / GameObject 생명주기
- Dirty / Save 처리
- Prefab / Blueprint 규칙
- Import Pipeline
- 에디터 전용 함수와 런타임 함수의 구분
- 엔진 버전에 따른 API 변화
- MCP 서버가 해당 기능을 노출했는지 여부

즉, AI 입장에서 게임을 만드는 것보다 **게임 엔진의 인간용 에디터를 조종하는 것**이 별도의 작업이 된다.

따라서 목표를 다음처럼 바꾼다.

> ❌ Unreal Engine 대체품을 만든다.  
> ❌ Unity 대체품을 만든다.  
> ❌ DirectX/Vulkan부터 새로 만든다.  
>
> ✅ 게임 제작 데이터와 런타임을 AI가 직접 이해하고 수정할 수 있게 만든다.  
> ✅ CLI / Editor / MCP가 모두 동일한 Command API를 사용하게 만든다.  
> ✅ 프로젝트의 진실의 원천(Source of Truth)을 인간용 Editor 상태가 아니라 명시적인 Project Data로 둔다.  
> ✅ AI가 실행 → 검사 → 수정 → 재실행을 GUI 없이 반복할 수 있게 만든다.

이 문서에서는 이를 임시로 **AI-Native Game Framework**라고 부른다.

---

## 0.1 ▶ v2: 리서치 후 이 문서의 위치

리서치 결과를 한 문단으로 요약하면 다음과 같다.

> **"Command Core를 먼저 두고 MCP는 어댑터"라는 §1의 원칙은 더 이상 가설이 아니라 업계 합의에 가깝다. 이 문서의 차별점은 그 원칙이 아니라, Command Core 안에 무엇을 1급으로 넣느냐에 있다.**

증거:

| 시스템 | 이미 하고 있는 것 | 빠져 있는 것 |
|---|---|---|
| **Bevy 0.15+ (2024-11) — Bevy Remote Protocol** | 엔진 자체가 JSON-RPC 2.0 서버. `world.query / spawn_entity / insert_components / mutate_components / reparent_entities`, `registry.schema`, `rpc.discover`. reflection 기반. MCP는 별도 crate(bevy_brp_mcp)가 1:1로 감쌈 | transaction, undo, atomic batch, persistent id, wire-format-exact schema, 텍스트 scene 로더(.bsn 미출시) |
| **Flecs v4.1.6 (2026-06)** | reflection(meta, 멤버별 range/warning/error), Query DSL(`EnemyAI, !Collider`), JSON entity/query/world 직렬화, REST API, Explorer(web inspector), Flecs Script, IsA prefab, alerts, observers | command/transaction/undo/CLI/test runner 전부 없음 (§8–§11 ≈ 0%) |
| **Godot 4.7 (2026-06)** | 텍스트 .tscn/.tres, `uid://` + `.uid` sidecar, node `unique_id`(4.6), `--headless --script --check-only --quit-after --fixed-fps`, `.import` sidecar + 재생성 가능 캐시, UndoRedo, gdUnit4(JUnit XML, exit 0/100/101) | transaction/ChangeSet/dry-run, 구조화 진단, query, headless에서 1급인 editor API(EditorInterface는 `--editor --headless`+tool script, ready 신호 없음) |
| **Unreal 5.8 Unreal MCP / Remote Control** | UFUNCTION metadata → MCP tool 자동 생성. Remote Control은 `generateTransaction`으로 undo 가능 | Remote Control은 Editor 전용; 둘 다 인증 없음(loopback만); binary asset; Unreal MCP의 transaction 의미론 미문서화 |
| **soft-ue-cli / Summer Engine (2026)** | CLI argparse → MCP schema 자동 생성, JSON stdout / "fewer tools, sharper tools" + script-error→screenshot→probe 루프 | soft-ue-cli는 transaction/undo와 batch atomicity가 없음을 README에 명시적 한계로 기재; Summer Engine은 undo/tx 언급 없음 |

따라서 이 문서의 **남는 차별점**은 다음으로 좁혀진다. 이것이 PoC에서 가장 먼저 검증할 가치가 있는 항목이다.

1. **데이터 수준 Transaction / ChangeSet** — 모든 mutation이 self-inverting op 목록을 남기고, dry-run·undo·diff·audit이 같은 자료구조를 쓴다 (§9, §10, §50, §51, §78)
2. **Structured Diagnostic with machine-applicable fix** — 오류가 "어느 파일의 어느 JSON pointer"와 "자동 적용 가능 여부"를 함께 들고 온다 (§13, §79)
3. **Determinism contract + 데이터화된 Test Scenario + replay** — "최대한 동일"이 아니라 same-binary bit-identical을 계약으로 (§22, §23)
4. **Persistent ID + canonical text serialization** — 텍스트라서 diff-friendly한 것이 아니라, 직렬화 불변식 때문에 diff-friendly하다 (§5.3, §7)
5. **Editor·CLI·MCP·Test가 정말로 같은 Command API만 쓴다** — Godot/Flecs Explorer/BRP 모두 이 규율을 지키지 못한 지점이 있다 (§32, §46)

반대로 초안에서 **약해진 전제** 두 가지는 정직하게 고친다.

- §3 "라이브러리 선택은 후순위" → **ECS/substrate에 한해 틀렸다.** Renderer·Audio·Image 로더는 후순위가 맞지만, ECS/substrate 선택(Flecs vs EnTT, 혹은 C++ 런타임을 직접 만들지 않는 선택)이 §12–§19, §25, §26의 60–70%를 결정한다. §3.1에 spike로 명시했다.
- §72 "Tool call 수가 적을수록 좋음"을 1순위 지표로 둔 것 → **측정 결과와 다르다.** 2026년 게임 제작 agent 벤치마크(GameCraft-Bench)에서 tool 사용량과 품질의 상관은 r≈0.016이고, 오히려 screenshot 검사 횟수가 디버깅 성과와 상관이 있으며, visual feedback 유무가 성공률을 41.1%→52.0%로 올렸다(GameDevBench). §72를 다시 썼다.

마지막으로, 리서치가 드러낸 것은 "라이브러리"보다 **아직 주인이 없는 아키텍처 결정**이 PoC를 막는다는 점이다 — Command Bus의 프로세스/동시성 모델, authoring world와 play world의 관계, headless 테스트의 입력 주입, AI가 쓴 C++의 크래시 진단, `game build`의 설계. 이들은 §88에 모았다. **§74 Phase 1이 시작되기 전에 §88의 결정 대부분이 내려져야 한다.**

---

# 1. 가장 중요한 설계 원칙

## 1.1 MCP를 엔진의 중심에 놓지 않는다

가장 중요한 원칙이다.

잘못된 구조:

```text
AI
 ↓
MCP
 ↓
Engine
```

이렇게 만들면 엔진의 기능 모델 자체가 MCP의 Tool / Resource / Prompt 구조에 종속된다.

대신:

```text
                 ┌──────── CLI
                 │
                 ├──────── Editor
                 │
Project → Command Core
                 │
                 ├──────── Test Runner
                 │
                 ├──────── HTTP / RPC
                 │
                 └──────── MCP Adapter
```

로 만든다.

MCP는 수많은 클라이언트 중 하나다.

즉:

```text
Engine 기능
    ≠
MCP Tool
```

이어야 한다.

예:

```cpp
CommandResult SetProperty(
    EntityId entity,
    ComponentType component,
    PropertyPath property,
    Value value
);
```

라는 엔진 명령이 먼저 존재한다.

그다음 이것을 CLI에서:

```bash
game set entity_01j5xq… /components/Transform/position/0 300
```

으로 노출할 수 있고,

MCP에서는:

```text
world_mutate_components(
    entity="entity_01j5xq…",
    component="Transform",
    path="/position/0",
    value=300
)
```

(▶ v2: MCP tool 이름과 인자 형태는 §8.1, §46.2, §47 규칙을 따른다.)

으로 감쌀 수 있고,

Editor Inspector에서는 숫자 입력 UI가 같은 명령을 호출하면 된다.

이 구조의 장점은 명확하다.

- MCP 규격이 바뀌어도 Core는 수정하지 않는다.
- 다른 AI protocol이 등장해도 adapter만 추가한다.
- 사람과 AI가 동일한 작업 단위를 사용한다.
- CLI 테스트가 MCP 테스트를 대부분 대체한다.
- 에디터 자동화 기능과 런타임 기능이 분리된다.
- 기능의 존재 여부가 MCP 서버 구현 수준에 좌우되지 않는다.

2026-07-28 MCP 규격에서도 stateless core를 포함한 큰 구조 변화가 있었다.  
따라서 MCP를 시스템 내부 계약이 아니라 **외부 프로토콜 어댑터**로 취급하는 것이 안전하다.

▶ v2: 위 문장은 사실이지만 너무 뭉뚱그려져 있다. 검증한 내용은 다음과 같다.

MCP **2026-07-28 revision**(2025-11-25 이후 8개월 만의 breaking release, RC 2026-05-21)은

- (a) `initialize` handshake와 `Mcp-Session-Id` 세션을 제거하고 protocol version / clientInfo / capabilities를 **매 요청의 `_meta`** 에 싣게 했다 (SEP-2575, SEP-2567). 세션이 필요한 서버는 "server-minted opaque handle"을 일반 tool 인자로 주고받아야 한다 — 우리의 `tx_…`, `run_…`, `snapshot_…` handle이 정확히 이 패턴이다 (§9, §46).
- (b) 모든 result에 `resultType`(`"complete"` | `"input_required"`)을 필수화하고, 서버→클라이언트 요청(elicitation/sampling/roots)을 MRTR(재시도 기반 Multi Round-Trip Request) 패턴으로 대체했다 (SEP-2322).
- (c) `server/discover` RPC를 필수화했다.
- (d) `tools/list` 등 list 결과에 `ttlMs` / `cacheScope`를 필수화했고, tools/list는 **연결별로 달라지거나 다른 요청의 부작용으로 바뀌면 안 된다**(결정적 순서 권장). → tool 목록이 연결별로 다르거나 다른 요청(예: load_project)의 부작용으로 바뀌는 설계는 규격 위반이다. 바뀌어야 한다면 `subscriptions/listen`의 toolsListChanged로 알리되, 프로젝트별 어휘는 schema resource에 둔다 (§15, §46).
- (e) Tasks를 `io.modelcontextprotocol/tasks` extension으로 분리했다 (`tasks/get` polling).
- (f) Roots · Sampling · **Logging** 을 deprecate했다(최소 12개월). 로그는 stderr 또는 OpenTelemetry로 — 우리 §28 로그를 MCP logging에 실으면 안 된다.
- (g) 2025-03-26부터 deprecated였던 legacy HTTP+SSE transport를 lifecycle 정책상 Deprecated로 재분류했다(최소 12개월 후 제거 대상). Unreal MCP 5.8은 아직 HTTP+SSE만 지원한다 — 어댑터 격리가 왜 필요한지 보여주는 사례.

`Mcp-Method` / `Mcp-Name` header routing과 세션 제거는 Streamable HTTP에만 해당한다. **stdio 어댑터에 실제로 영향을 주는 것**은 `server/discover`, 요청별 `_meta`, `resultType`, 결정적·비연결종속 `tools/list`, 그리고 Logging deprecation이다.

규격 유지보수자 스스로 "직접 구현한 경우 상당한 uplift"를 경고했고, 공식 Tier-1 SDK는 TypeScript / Python / Go / C#(Rust는 beta)뿐 **C++는 없다.** 이것이 "MCP 어댑터는 마지막에, 얇게, 가능하면 공식 SDK로 만든 sidecar로"라는 §46·§74의 가장 구체적인 근거다.

---

# 2. 목표와 비목표

## 2.1 목표

초기 목표는 다음 정도로 제한한다.

### Runtime

- Window
- Input
- Time
- Entity
- Component
- Transform
- Sprite 또는 Simple Mesh
- Camera
- Collision / Physics
- Scene / World
- Prefab
- Basic Animation
- Audio
- Save / Load

### Authoring

- 프로젝트 데이터가 텍스트 기반
- Schema 존재
- 안정적인 ID
- CLI로 생성/수정/삭제 가능
- Query 가능
- Validation 가능
- Undo / Redo 가능
- Diff 친화적
- Git 친화적

### AI

- Headless 실행
- 구조화된 로그
- 구조화된 오류
- Entity / Asset 검색
- 참조 관계 검사
- Frame / State Snapshot
- deterministic test
- 자동 validation
- screenshot / capture
- command history
- machine-readable 결과

### Editor

초기에는 다음만 존재해도 된다.

- Hierarchy
- Viewport
- Inspector
- Asset Browser
- Console

Editor 자체는 Project Data의 시각화 도구다.

---

## 2.2 명시적인 비목표

초기에는 절대 하지 않는다.

- Unreal 수준의 범용 Renderer
- Nanite 유사 기능
- Lumen 유사 기능
- 대규모 World Partition
- Niagara급 VFX Editor
- Control Rig급 Animation Tool
- MetaHuman급 Character Pipeline
- 범용 Shader Graph
- AAA Terrain System
- 자체 Physics Engine
- 자체 Audio Codec
- 자체 Image Codec
- 자체 Model Importer 전체 구현
- 여러 플랫폼 동시 지원
- 모바일까지 처음부터 대응
- Marketplace / Package ecosystem 구축
- Visual Scripting
- Plugin ABI 안정화
- 범용 엔진 사용자 지원

이 중 필요한 기능은 게임이 실제로 요구할 때 추가한다.

---

# 3. 왜 “자체 엔진”이 아니라 “게임 프레임워크”인가

자체 엔진이라는 단어를 사용하면 범위가 폭발하기 쉽다.

```text
Renderer
Physics
Audio
Animation
Navigation
Input
Asset Pipeline
Shader Compiler
Scripting
Editor
Networking
Platform
Build System
Profiling
Packaging
...
```

을 모두 직접 만들 필요가 없기 때문이다.

우리가 원하는 것은 다음에 가깝다.

```text
[검증된 Low-Level Library]
        ↓
[얇은 Engine Abstraction]
        ↓
[AI-friendly Project Model]
        ↓
[Game Framework]
        ↓
[Editor / CLI / AI]
```

▶ v2: 초안의 표를 **2026-08-21 기준 검증된 표**로 교체한다. (버전·날짜는 GitHub release / 공식 문서에서 확인. 라이선스 중 SDL/EnTT/Flecs/Jolt/Box2D/Luau/cr.h는 직접 확인, 나머지는 vendoring 시 LICENSE 파일 재확인.)

| 영역 | 후보 | 최신 안정 버전 (릴리스일) | 라이선스 | 언어 요구 | 비고 |
|---|---|---|---|---|---|
| Platform / Window / Input / GPU | **SDL3** | 3.4.14 (2026-08-03) | zlib | C | SDL_GPU·Process API 포함(3.2.0+). 3.4.0부터 `SDL_CreateGPURenderer`, `SDL_LoadPNG/SavePNG`. `dummy` / `offscreen` video driver 내장(§20). **파일 감시 API 없음**(§39) |
| ECS (A안, 기본 후보) | **Flecs** | v4.1.6 (2026-06-29) | MIT | C99 core / C++17 API | reflection(meta) · Query DSL · JSON 직렬화 · REST · Explorer · Flecs Script · IsA prefab · alerts · observers 내장. §3.1 spike로 결정 |
| ECS (B안) | EnTT | v4.0.0 (2026-07-23) | MIT | **C++20 필수**, CMake ≥ 3.28 | header-only. `entt::meta` 있으나 serialization·string lookup·registry 통합 없음. v4에서 meta API 이름 변경(`id`→`alias`, `arity`→`get/set_arity`) |
| Physics 2D (PoC) | **Box2D** | v3.1.1 (2025-06-04) | MIT | C17 | **기본값으로 cross-platform + thread-count 독립 결정성**. rollback용 SaveState 없음. v3.2 미출시 |
| Physics 3D | Jolt Physics | v5.6.0 (2026-07-11) | MIT | C++17, VS2022+/Clang16+/GCC12+ | same-binary 결정성 기본. cross-platform은 `CROSS_PLATFORM_DETERMINISTIC`(~8% 느림). `SaveState/RestoreState` 제공. Godot 4.6이 기본 3D physics로 채택 |
| Physics 3D (관찰) | Box3D | v0.1.0 alpha (2026-06-30) | MIT | C17 | Box2D 형제. v1.0 전까지 채택 금지 |
| Editor GUI | Dear ImGui | v1.92.9b / -docking (2026-07-31) | MIT | C++ | docking은 별도 브랜치지만 공식 "safe and recommended". v1.92.0+ 백엔드 텍스처 계약 변경(`RendererHasTextures`) |
| JSON (authoring / 도구) | nlohmann/json | v3.12.0 (2025-04-11) | MIT | C++11 | RFC 6901 pointer, RFC 6902 `patch/diff`, `merge_patch`, `ordered_json`, 파싱 오류 byte 위치. ChangeSet 구현 의존성(§78) |
| JSON (schema / reflect) | glaze | v8.1.0 (2026-08-18) | MIT | **C++23** | aggregate struct 무매크로 reflection(≤128 members), `glz::schema`로 JSON Schema 2020-12 생성, 옵션 C++26 P2996 백엔드, BEVE binary |
| JSON (대안) | yyjson 0.12.0 (2025-08) / simdjson v4.6.6 (2026-07, read-only) | | MIT / Apache-2.0 | C / C++ | |
| JSON Schema 검증 | jsoncons | 활성 | BSL-1.0 | C++11 | draft 4 ~ 2020-12. JSONPath/JMESPath/Pointer/Patch 동봉. valijson·pboettch는 draft-07까지 |
| glTF | cgltf v1.15 (2025-02) 또는 fastgltf v0.9.0 (2025-07) | | MIT | C / C++17 | tinygltf v3.0.1 (2026-08)도 가능 |
| Image | SDL_LoadPNG (3.4+) + stb_image | stb 2026-08 활성 | zlib / MIT·Unlicense | C | PNG만이면 stb 불필요 |
| Audio | miniaudio | 0.11.25 (2026-03-03) | MIT-0 / Unlicense | C | |
| Profiler | **Tracy** | v0.14.0 (2026-08-09) | BSD-3 | C++ | `tracy-capture`, `tracy-capture-daemon`, `tracy-csvexport`, `tracy-import-chrome`, **MCP server 내장(v0.14.0, 2026-05 merge)** → §63/§65 자체 구현 불필요 |
| Rendering | SDL_GPU 우선 | | | | 필요 시 sokol_gfx(zlib) / bgfx(BSD-2) / wgpu-native v29.0.1.1(2026-06). Diligent은 v2.5.6(2024-09) 이후 태그 없음 |
| Build | CMake + CPM.cmake 또는 vcpkg | CMake 4.4.2 (2026-07-31), CPM v0.43.1, vcpkg 2026.07.29 | BSD-3 (CMake) / MIT (CPM, vcpkg) | | CMake 4.x는 `cmake_minimum_required < 3.5` 호환 제거 → 구형 3rd-party에 `CMAKE_POLICY_VERSION_MINIMUM=3.5` |
| Test | Catch2 v3.15.3 (2026-07-26) 또는 doctest v2.5.3 (2026-07-06) | | BSL-1.0 / MIT | | GoogleTest v1.18.0 (2026-08-10)도 가능. Catch2 JSON reporter가 `--json` 철학과 맞음 |
| File watch | efsw | 2026-08-18 활성 | MIT | C++ | §39 Data Hot Reload |
| Script (옵션, 경계 한정) | Luau | 0.734 (2026-08-14, 주간 릴리스) | MIT | C++11 VM / C++17 compiler | gradual typing + sandbox. §61.1 참조. sol2는 2025-03 이후 정체·Lua 5.5 미지원 |
| Code hot reload (옵션) | cr.h | 2026-06 활성 | MIT | C | Game/를 DLL로 빌드 시 CR_STATE 보존 + crash rollback. §39 |

초안의 문장 "단, 라이브러리 선택은 엔진 설계보다 후순위다"는 **절반만 맞다.** Renderer·Audio·Image·glTF 로더는 후순위가 맞다. 그러나 **ECS/substrate 선택은 후순위가 아니다** — 아래 §3.1 참조.

핵심은 여전히 라이브러리가 아니라 그 위에 놓이는 Command / Data / Inspection 구조다. 다만 그 구조의 상당 부분을 **사서 쓸 수 있는지**를 먼저 확인해야 한다.

---

## 3.1 ▶ v2: Build vs Adopt — 왜 Godot / Bevy / Flecs 위에 올리지 않는가 (또는 올리는가)

리서치 결과 세 개의 기성 시스템이 이 문서의 상당 부분을 이미 제공한다. 정직한 설계 문서라면 "왜 그 위에 올리지 않는가"에 답해야 한다.

| 대안 | 제공하는 것 | 못 주는 것 (우리가 만들어야 할 것) |
|---|---|---|
| **Godot 4.7 + 얇은 CLI** | 텍스트 데이터, 안정 ID, headless 실행, import sidecar, gdUnit4 | Transaction/ChangeSet, 구조화 진단, query, headless-first editor API. 엔진 fork 없이는 mutation 경로 전부를 통제할 수 없음 |
| **Bevy 0.19 + BRP** | reflection 기반 generic command API, schema endpoint, MCP crate | transaction/undo/persistent id/wire-format schema, 텍스트 scene(.bsn 로더 미출시), Rust |
| **Flecs v4.1.6 (C++ 라이브러리)** | reflection, query DSL, JSON, REST, Explorer, prefab, alerts | command/transaction/undo/CLI/test runner — 즉 **우리의 차별점 전부**. 하지만 §12–§19, §25, §26의 60–70%는 제공 |

자체 C++ 프레임워크를 택하는 이유는 네 가지다.

1. Command Core에 Transaction / ChangeSet / Validation을 1급으로 넣으려면 **엔진 내부의 모든 mutation 경로를 통제**해야 한다. Godot plugin이나 Bevy crate로는 엔진이 직접 파일을 쓰는 경로를 막을 수 없다.
2. 프로젝트 데이터 schema를 처음부터 AI용으로 설계한다 (§5–§7).
3. Editor를 Command 소비자로 강제한다 (§32).
4. C++ 런타임 + Jolt/Box2D 통합.

단, 이 네 가지가 정말 기성 시스템으로 불가능한지는 **2주짜리 spike로 먼저 확인**한다.

```text
Phase -1 — Substrate Spike (2주, §74에 추가)

S-A. Flecs v4.1.6 위에
   - 우리 JSON authoring 포맷 → Flecs world 로드
   - ChangeSet(§78)을 Flecs observer로 기록하고 inverse 적용으로 undo
   - Flecs REST/Explorer는 읽기 전용 디버깅으로만 사용
   - 측정: 작성 코드량, §16 query·§25 dump·§14 schema를 Flecs가 얼마나 대체하는가

S-B. EnTT v4.0.0 위에
   - 같은 것을 직접 구현 (meta + 자체 serializer + 자체 query)
   - 측정: 추가 코드량, 컴파일 시간

A3. Godot 4.7 headless + Claude Code (파일 직접 편집 + --headless --script)   (= §72의 A3)
C.  Bevy 0.19 + bevy_brp_mcp                                                (= §72의 C)
   - §71 PoC 시나리오의 1~5번을 그대로 시킨다
   - 측정: §72 지표

결정 규칙:
- S-A 또는 S-B로 §71 시나리오의 Command 경로가 2주 안에 동작하지 않으면 → 자체 프레임워크 중단 (§73)
- A3 또는 C가 §72 1순위 지표에서 S-A/S-B 대비 20% 이내면 → 자체 프레임워크 중단, 그 위에 Command/Transaction/Diagnostics 레이어를 얹는 방향 검토
- S-A가 동작하면 Flecs 채택. S-B만 동작하면 EnTT 채택.
```

**기본 후보는 Flecs다.** 근거: 이 문서의 핵심(§4)은 Command / Project Model / Inspection / Validation인데, EnTT가 "시작이 빠른" 것은 Entity/Component 저장소까지이고 Inspection 레이어는 전부 직접 만들어야 한다. Flecs는 그 레이어를 1st-party addon으로 제공한다.

Flecs를 택할 때 지켜야 할 규율:

- **authoring 데이터의 source of truth는 여전히 우리 JSON 문서다.** Flecs world는 거기서 빌드되는 runtime 투영이다. Flecs Script(.flecs)는 one-way(load/hot-reload) 로만 쓴다 — world → script exporter의 round-trip은 확인되지 않았다.
- Flecs REST `PUT /component`와 Explorer의 drag-to-edit은 **runtime world만 건드린다.** authoring 변경은 CommandBus를 통해서만 들어온다 (§88.2 authoring/play world 분리). 그렇지 않으면 §84-5/6이 깨진다.
- Flecs v4는 prefab 상속 기본값이 **Override(복사)** 다 (v3의 Inherit에서 반전). authoring 의미론(§34: absent = inherit)은 JSON 모델이 정의하고, runtime 인스턴스화 시 flatten하면 되므로 충돌하지 않는다.
- Flecs entity id는 **64-bit**(32-bit index + generation)이고 재활용 id는 "4 billion보다 큰 값이 정상"이다. 초안 §7의 `uint32 RuntimeId` 스케치는 Flecs에서는 틀려서 v2에서 `uint64`로 고쳤다. 어차피 runtime id는 외부에 노출하지 않는다.
- archetype 저장소라 per-entity add/remove는 EnTT(sparse-set)보다 느리다 (abeimler benchmark, 1M entities: add/remove 250ms vs 26ms; 반면 7-system update는 16ms vs 102ms). 매 프레임 붙였다 떼는 상태는 component field / `CanToggle` / `Sparse` trait으로 처리한다. 2D·데이터 중심 첫 게임(§69)에서는 문제가 될 가능성이 낮다.
- Flecs는 4.1.x minor 릴리스에서도 API break를 냈다 (4.1.1 `ecs_emplace_id` 시그니처, 4.1.2 C++17 필수, 4.1.6 entity range API 교체). **버전을 고정**하고 업그레이드는 의식적으로 한다.

---

# 4. 전체 아키텍처

권장 레이어:

```text
┌─────────────────────────────────────────────┐
│                 AI Clients                  │
│ Claude / Codex / Local Agent / Script Bot   │
└─────────────────────────────────────────────┘
                    │
        MCP / CLI / HTTP / stdin
                    │
┌─────────────────────────────────────────────┐
│                Adapter Layer                │
│ MCP Adapter │ CLI Adapter │ Editor Adapter  │
└─────────────────────────────────────────────┘
                    │
┌─────────────────────────────────────────────┐
│               Command Layer                 │
│ Create / Delete / Set / Query / Run / Test  │
│ Validation / Transaction / Undo / History   │
└─────────────────────────────────────────────┘
                    │
┌─────────────────────────────────────────────┐
│              Project Model                  │
│ World / Entity / Component / Asset / Prefab │
│ Schema / Reference Graph / Metadata         │
└─────────────────────────────────────────────┘
                    │
┌─────────────────────────────────────────────┐
│                 Runtime                     │
│ ECS / Render / Physics / Animation / Audio  │
└─────────────────────────────────────────────┘
                    │
┌─────────────────────────────────────────────┐
│               Platform Layer                │
│ SDL / OS / Filesystem / Threads / GPU       │
└─────────────────────────────────────────────┘
```

여기서 가장 중요한 레이어는 Renderer가 아니라:

```text
Command Layer
Project Model
Inspection
Validation
```

이다.

---

# 5. Source of Truth

## 5.1 Editor 상태가 진실이면 안 된다

Unreal / Unity의 전통적인 흐름에서는 대체로 Editor 내부 상태와 자체 Asset format이 중심이다.

AI-native 구조에서는 반대로 간다.

```text
Project Data
   ↓
Runtime
   ↓
Editor View
```

Editor는 데이터를 **표시하고 수정하는 클라이언트**다.

---

## 5.2 권장 프로젝트 구조

예:

```text
/MyGame
│
├── project.json
│
├── Config/
│   ├── input.json
│   ├── rendering.json
│   └── physics.json
│
├── Worlds/
│   ├── Main.world.json
│   └── TestArena.world.json
│
├── Prefabs/
│   ├── Player.prefab.json
│   ├── Goblin.prefab.json
│   └── Bullet.prefab.json
│
├── Data/
│   ├── enemies.json
│   ├── items.json
│   └── skills.json
│
├── Assets/
│   ├── Textures/
│   ├── Meshes/
│   ├── Audio/
│   └── Animations/
│
├── Source/
│   ├── Game/
│   └── Components/
│
├── Tests/
│   ├── Combat/
│   └── World/
│
└── Cache/
```

중요:

```text
Cache/
```

는 Source가 아니다.

삭제해도 다시 생성 가능해야 한다.

▶ v2: "삭제해도 재생성 가능"은 선언만으로는 성립하지 않는다. §36–§38에 cache key 정의(source hash + importer version + settings)를 추가했다. 그 입력이 전부 Source + sidecar에 있을 때만 Cache/ 삭제는 "시간"만 잃고 "데이터"는 잃지 않는다.

---

## 5.3 ▶ v2: Canonical Project JSON — 표기 규약

초안은 "텍스트 기반 = diff 친화적, Git 친화적"(§2.1)이라고 썼다. 리서치는 그것이 **틀렸음**을 보여준다. Godot은 텍스트 .tscn을 쓰면서도 6년간(2020 merge-driver 제안 → 2026 `load_steps` 제거) merge 충돌에 시달렸고, Unity는 Force Text YAML에 더해 UnityYAMLMerge가 필요했다. **diff 친화성은 텍스트가 아니라 직렬화 불변식에서 나온다.**

따라서 Command layer가 쓰는 모든 파일은 **단 하나의 serializer**가 다음 규약으로 출력한다.

```text
인코딩 / 형식
- UTF-8, BOM 없음, LF, 파일 끝 개행, 2-space indent
- 엄격한 JSON (RFC 8259). **모든** 프로젝트 파일(project.json, Config/, Worlds/, Prefabs/, Data/, *.meta.json)에
  주석·trailing comma 금지. Command layer는 이 파일들 전부를 다시 쓸 수 있다 (project.json·Config/는 §10.1의 global history).
  이유: nlohmann을 포함한 C++ 파서는 주석을 공백으로 버린다 → 한 번 저장하면 주석이 조용히 사라진다.
  JSONC 예외는 두지 않는다 — "사람만 편집하는 파일"을 구분하면 언젠가 Command가 그 파일을 써야 하는 날 주석이 사라진다.
- 메모가 필요하면 schema에 정의된 필드를 쓴다: "description", "notes" (런타임은 무시)

키 순서
- 헤더 키는 고정 순서: $schema, schemaVersion, id, name, base, tags, …
- 그 다음 "components": component 이름 A→Z 정렬
- component 내부 property는 reflection 선언 순서 (알파벳순 아님 — position/rotation/scale가 붙어 있어야 읽힌다)
- nlohmann::ordered_json으로 출력. 기본 std::map 정렬에 의존하지 않는다.

숫자
- float32 필드는 std::to_chars(float) shortest-round-trip ("0.3", "0.30000001192092896" 아님)
- double은 to_chars(double). 정수에 ".0"을 붙이지 않는다.
- NaN / Inf 금지 (validation error). schema가 허용할 때만 null.
- vec2/3/4, quat, color는 고정 길이 배열 [x, y, z]. 객체 {x:, y:, z:} 아님. (§14에서 mutation path 규칙과 함께 고정)

문자열
- ID는 소문자 (§7 grammar)
- enum 값은 문자열(snake_case). 정수 저장 금지 (diff 가독성, AI 가독성)

구조
- entities는 **배열이 아니라 persistent id를 key로 하는 object**로 저장한다
  → JSON Pointer 경로가 안정적이고, git diff가 지역적이며, 배열 index 밀림 문제가 없다 (§78)
- 계층(hierarchy)은 nested children 배열이 아니라 **child 쪽의 "parent" + "order"(fractional index 문자열) property**로 표현한다
  → reparent/reorder가 단순 property replace가 된다 (Figma, Loro movable-tree와 같은 선택)
- 파일 헤더에 파생 가능한 카운터/캐시 값을 넣지 않는다 (Godot load_steps 교훈: 노드 하나 추가할 때마다 1행이 바뀜)
- entity object의 키 순서 = id 정렬 (생성 순서 아님 — 저장할 때마다 같아야 한다)

해시
- cache key, checkpoint digest 등 JSON을 해시할 때는 RFC 8785 JCS canonical form을 쓴다.
  pretty on-disk 형식을 해시하지 않는다.
```

`game fmt` 명령은 파일을 이 규약으로 재직렬화하고, `game validate`는 파일이 재직렬화 결과와 byte-identical하지 않으면 `JSON_NOT_CANONICAL`을 낸다 (fix = `game fmt`).

---

# 6. Entity / Component 데이터

예를 들어 Goblin prefab (▶ v2: §5.3 규약과 §7 ID grammar에 맞게 수정):

```json
{
  "$schema": "game://schema/prefab/1",
  "schemaVersion": 1,
  "id": "prefab_01j5xq8z3mf0n9k2c7p4rtvw6y",
  "name": "Goblin",
  "tags": ["enemy"],

  "components": {
    "Collider": {
      "shape": "capsule",
      "radius": 0.4,
      "height": 1.6
    },

    "EnemyAI": {
      "behavior": "melee_basic",
      "targetTag": "Player"
    },

    "Health": {
      "max": 100
    },

    "Movement": {
      "speed": 4.0
    },

    "SpriteRenderer": {
      "sprite": "asset_01j5xq9a7c3d2e1f0g9h8j7k6m#sprites/goblin_idle",
      "layer": "Character"
    },

    "Transform": {
      "position": [0, 0, 0],
      "rotation": [0, 0, 0, 1],
      "scale": [1, 1, 1]
    }
  }
}
```

AI가 이 데이터를 이해하는 데 특별한 Editor knowledge가 필요 없다.

▶ v2: 초안의 예시들은 표기가 서로 달랐다 — §6은 `max`/`position`, §34·§35·§25는 `Health.Max`/`Movement.Speed`/`Current`; ID는 `prefab:goblin`(slug), `entity:103`(정수), `entity:01JDS4J82…`(ULID 풍)가 섞여 있었다. **AI는 예시를 그대로 베낀다.** 이후 전 문서에서 다음으로 통일한다.

```text
component 타입 이름  = PascalCase   (C++ 타입 그대로)        Health, SpriteRenderer
property 키          = camelCase    (reflection 그대로)       max, targetTag
JSON Pointer 경로    = /components/Health/max              (dotted path "Health.max"는 CLI 입력 sugar로만 허용)
persistent ID        = <type>_<26자 base32>                 §7
사람용 이름          = "name" 필드 — 식별자가 아니다
```

world 파일의 entity는 prefab 인스턴스일 경우 전체 component를 복사하지 않고 `prefab` + override(§34의 `set/add/remove`)만 가진다. 계층은 `parent` + `order`로 표현한다 (§5.3).

```json
{
  "$schema": "game://schema/world/1",
  "schemaVersion": 1,
  "id": "world_01j5xqb2…",
  "name": "TestArena",
  "entities": {
    "entity_01j5xqc4…": {
      "name": "Encounter_05",
      "parent": null,
      "order": "a0",
      "components": { "Transform": { "position": [10, 0, 0], "rotation": [0,0,0,1], "scale": [1,1,1] } }
    },
    "entity_01j5xqd6…": {
      "name": "Goblin_01",
      "parent": "entity_01j5xqc4…",
      "order": "a0",
      "prefab": "prefab_01j5xq8z…",
      "set": { "/components/Transform/position": [5, 0, 0] }
    }
  }
}
```

---

# 7. Stable ID

AI 작업에서는 이름보다 ID가 중요하다.

잘못된 방식:

```text
"Hierarchy의 Goblin 세 번째 것"
```

좋은 방식 (▶ v2: §7.1 grammar):

```text
entity_01j5xq8z3mf0n9k2c7p4rtvw6y
```

또는 내부 integer ID + persistent GUID를 같이 둔다.

```cpp
struct EntityHandle
{
    uint64 RuntimeId;      // ▶ v2: Flecs는 64-bit (32-bit index + generation). EnTT는 32-bit. 어느 쪽이든 외부 노출 금지
    Uuid   PersistentId;   // 128-bit in memory
};
```

Runtime ID:

- 빠른 접근
- ECS용
- **직렬화하지 않고, CLI/JSON 출력에 나타나지 않는다** (`--debug` 플래그 뒤에서만)

Persistent ID:

- 저장
- diff
- reference
- AI command
- editor

용으로 쓴다.

## 7.1 ▶ v2: ID 문법 — TypeID v0.3 / UUIDv7

초안은 `entity:01JDS4J82…`(Crockford base32), `UUID PersistentId`(struct), `entity:103`(정수)을 섞어 썼다. 하나로 고정한다.

```text
Persistent ID = <type>_<26-char Crockford base32 of a UUIDv7>        (TypeID v0.3 grammar)

  예: entity_01j5xq8z3mf0n9k2c7p4rtvw6y
      prefab_01j5…   world_01j5…   asset_01j5…   tx_01j5…   run_01j5…   cs_01j5…   snapshot_01j5…

  - prefix: ^[a-z]([a-z_]{0,61}[a-z])?$   구분자 "_" 하나
  - suffix: 정확히 26자, alphabet 0123456789abcdefghjkmnpqrstvwxyz, 첫 글자 0–7
  - suffix는 유효한 RFC 9562 UUID로 디코드되어야 한다. 새로 발급하는 authoring ID는 UUIDv7
    (48-bit Unix ms + 74-bit random/counter); runtime-spawned ID는 UUIDv8 (아래). 검증기는 v7/v8만 허용
    (TypeID 규격은 *생성* 시 v7을 요구하고, 외부 제공 UUID는 다른 version도 허용한다)
  - 생성: UUIDv7 + 프로세스별 monotonic counter (RFC 9562 §6.2 method 1)
    → 한 transaction에서 대량 생성한 ID가 생성 순서로 정렬된다
  - 출력은 소문자 고정(TypeID 규격). 입력은 CLI가 소문자로 정규화한 뒤 검사한다(규격 확장)
  - 메모리에서는 128-bit binary (RFC 9562 §6.13 권장)
```

왜 UUIDv4/ULID가 아니라 TypeID(UUIDv7)인가: 시간순 정렬 가능(ULID와 같음), RFC 표준(UUIDv7), prefix로 타입이 읽힘(Stripe 스타일), 공개된 grammar가 있어 hallucinated ID를 **로컬에서 형식 검사로 거부**할 수 있다.

**결정적 런타임에서의 ID 생성** (§22): play 중 runtime이 spawn한 entity에 UUIDv7(벽시계 기반)을 주면 두 run이 달라진다. runtime-spawned entity의 persistent id는 `(worldSeed, tick, 순번)`에서 파생한 결정적 UUID(version 8 custom)로 만들고, `game promote`(§88.2)로 authoring에 승격될 때만 UUIDv7로 다시 발급한다.

## 7.2 ▶ v2: ID의 위치 — 파일 안 vs sidecar

- JSON 문서(world, prefab, data)는 `"id"`를 파일 안에 가진다 (Godot `.tscn`의 `uid=` 헤더와 같음).
- 자체 메타데이터를 가질 수 없는 외부 자산(png, glb, wav, ttf, shader)은 **커밋되는 sidecar** `<file>.meta.json`(§37)이 ID를 소유한다. Godot이 4.4에서 script/shader용 `.uid` sidecar를 추가해야 했던 이유와 같다. `.gitignore`에 넣지 않는다.
- `Cache/id_index.json`(id ↔ path)은 파생물이다. 삭제하면 스캔으로 재생성한다.

## 7.3 ▶ v2: 중복 ID 정책

중복은 **파일을 셸 수준에서 복사할 때** 생긴다 — AI 에이전트가 가장 자주 하는 일이다. Godot 4.4는 "UID duplicate detected" 경고만 내고 자동 수정이 없다(issue #102490). Unity는 조용히 한쪽 GUID를 재발급해 참조를 끊는다. 둘 다 피한다.

```text
1. game validate / 프로젝트 스캔은 DUPLICATE_PERSISTENT_ID 를 두 경로와 함께 보고한다.
2. 해결은 절대 조용히 하지 않는다. 고쳐지기 전까지 그 id의 resolve는 ERROR.
3. game id fix --keep <path>
   기본 휴리스틱: reference graph(§19)에서 참조되는 쪽 / git history가 오래된 쪽의 id를 유지,
   다른 쪽에 새 UUIDv7 발급, ChangeSet으로 기록 (undo 가능)
4. Command layer의 복사(game prefab clone, game asset copy, game entity duplicate)는 항상 새 id를 발급한다.
   → Command를 통한 복사는 충돌하지 않는다.
```

## 7.4 ▶ v2: ID는 정확성의 기준, 이름은 검색 정밀도의 기준

Anthropic의 측정(2025-09): 불투명한 UUID를 의미 있는 이름으로 바꾸면 Claude의 검색 정밀도가 "significantly improves"한다. ID-first 원칙을 버리지는 않되 다음을 더한다.

- 모든 결과는 `id`와 `name`, 가능하면 `path`를 함께 돌려준다. **path 형식** = `<worldName>/<ancestors…>/<name>` (예 `TestArena/Encounter_05/Goblin_01`). 선택자 `path:`는 같은 문자열을 받되 world 이름은 모호하지 않으면 생략 가능.
- CLI/MCP 입력은 `entity_01j5…` 외에 `name:Goblin_01`, `path:Encounter_05/Goblin_01`, 그리고 git처럼 **suffix의 고유 prefix**(`entity_01j5xq8z`)를 받는다. 모호하면 `AMBIGUOUS_SELECTOR` 오류에 후보 id 목록을 실어 돌려준다.
- 파일 안의 참조(`"prefab": …`, `"sprite": …`, `"target": …`)는 **항상 id**다. 이름은 참조에 쓰지 않는다 (rename 안정성, reference graph의 기준).
- `@last`, `$goblin` 같은 alias는 transaction 범위에서만 유효하다 (§49).
- `snapshot:<tick>`은 현재(또는 `--run`으로 지정한) run의 해당 tick snapshot으로 해석되는 선택자다 (§26).

BRP(Bevy)는 entity를 runtime id(`4294967298` 같은 packed 정수)로 주소지정한다. 프로세스를 재시작하면 id가 바뀌어 AI가 세션 간에 같은 객체를 가리킬 수 없고, `.scn.ron`이 entity를 runtime 숫자 id로 저장하는 것도 같은 문제를 안고 있다(재저장 시 id가 바뀌면 diff가 흔들린다). 이 문서가 persistent id를 고집하는 근거다.

---

# 8. Command Layer

AI-native framework의 실제 핵심이다.

모든 편집은 Command다.

▶ v2: 초안의 `ICommand`는 `Execute`와 `Undo`를 모두 가상 함수로 요구했고, 예시 목록에 `Run / Pause / Query / CaptureFrame`이 섞여 있었다. 이는 §10·§78·§84-6("모든 Command는 ChangeSet을 남긴다")과 모순된다. 다음으로 고친다.

```cpp
enum class CommandKind
{
    Mutation,        // authoring 데이터를 바꾼다. 반드시 ChangeSet을 남긴다. undo 가능
    Query,           // 읽기 전용. ChangeSet 없음
    RuntimeControl,  // run / pause / step / capture / snapshot. ChangeSet 없음. undo 불가
    Meta             // tx.* / history.* / checkpoint.create — ChangeSet을 "다루지만" 자체 ChangeSet은 없다. undo 대상 아님
};

struct ICommand
{
    virtual CommandKind   Kind() const = 0;
    virtual CommandResult Execute(ProjectContext&) = 0;
    // Undo()는 없다. CommandBus가 inverse(ChangeSet.ops)로 처리한다 (§10)
};
```

Mutation command는 ProjectModel을 직접 고치지 않는다. `ChangeBuilder`(add / remove / replace / move / file.* 헬퍼, reflection으로 `before`를 자동 채움)로 ops를 만들고 CommandBus가 적용한다. 이렇게 해야 "command가 한 일 = ChangeSet"이 **구조적으로** 보장된다. Unreal은 mutation 전에 `Modify()`를, Unity는 `RecordObject`를 먼저 호출해야 undo가 기록된다 — 호출을 빠뜨리거나 순서를 틀리면 조용히 undo가 깨진다. 이 구조는 그 실수를 만들 수 없게 한다.

예시 명령 (▶ v2: kind별로 분리):

```text
Mutation
  entity.create        entity.delete        entity.rename
  entity.reparent      entity.reorder       entity.duplicate
  component.add        component.remove
  property.set
  prefab.create        prefab.instantiate   prefab.flatten
  asset.import         asset.delete         asset.copy
  world.create         world.delete
  project.migrate      project.fmt          project.reload_document
  apply                (batch — §49)
  checkpoint.restore   (restore 자체가 ChangeSet을 남긴다 — §52)

Meta
  tx.begin  tx.commit  tx.rollback
  history.undo         history.redo
  checkpoint.create

Query
  project.info  world.list  entity.list  component.list
  property.get  world.query  explain  refs  schema  describe  capabilities
  validate  lint  diff  history.list

RuntimeControl
  run.start  run.stop  run.pause  run.step  run.status
  snapshot  capture  dump  trace  profile  benchmark  test.run
  input.inject  replay.record  replay.play  replay.verify
```

## 8.1 ▶ v2: 명명 규칙 — BRP 관례를 그대로 쓴다

Bevy는 0.17에서 method 이름을 `bevy/verb`에서 `world.verb_noun`으로 바꿨다(PR #19377). 이미 한 번 겪고 정착한 규칙을 재사용하면 BRP를 본 적 있는 agent에게 즉시 읽힌다.

```text
<namespace>.<verb>_<noun>        (집합 연산은 복수)
  world.query          world.spawn_entity       world.despawn_entity
  world.get_components world.insert_components  world.remove_components
  world.mutate_components (= entity, component, path, value — 우리의 property.set)
  world.reparent_entities  world.list_components
  registry.schema      rpc.discover

본 프레임워크 고유 (BRP에 없음):
  tx.begin / tx.commit / tx.rollback
  history.undo / history.redo / history.list
  project.save / project.diff / checkpoint.*
```

**정식 command id는 §8의 짧은 `<noun>.<verb>` 이름**(`entity.create`, `component.add`, `property.set`, `prefab.create`, `asset.import` …)이다 — envelope의 `command`, batch의 `changes[].op`, ChangeSet의 `intent.op`, fix의 `commands[].op` 모두 이 id를 쓴다. BRP 스타일 이름(`world.spawn_entity` ≡ `entity.create`, `world.insert_components` ≡ `component.add`, `world.mutate_components` ≡ `property.set`, `world.despawn_entity` ≡ `entity.delete`, `world.reparent_entities` ≡ `entity.reparent`, `world.query` ≡ `query`)은 RPC가 **받아들이는 alias**다 — BRP를 본 agent에게 익숙하도록. BRP에 없는 command(prefab, asset, tx, history)는 alias가 없다.

CLI는 `game query --with Health --without Collider`처럼 id의 noun/verb를 서브커맨드로 편다. **MCP tool은 command 1:1이 아니다** — §47의 tools[] 집합(`query`, `apply`, `tx` …)만 tool이고 개별 command는 `apply.changes[].op`로 노출된다; tool 이름은 `.`을 `_`로 바꾼다(`run.status` → `run_status`). bevy_brp_mcp의 1:1 매핑 방식과 다른 이유는 §47.

## 8.2 ▶ v2: Command 적용 시점 — 프레임 안의 고정된 지점

BRP는 초기에 Update 스케줄 안에서 순서가 정의되지 않아 race가 있었고(issue #16042), 전용 system set으로 고쳤다. 같은 실수를 미리 막는다.

```text
원격/CLI/Editor에서 들어온 Mutation·RuntimeControl command는
  프레임 내 고정된 지점 (Tick 시작 직전의 CommandApply 단계) 에서만 적용한다.
Query는 그 직후의 일관된 snapshot에서 수행한다.
play 중에 들어온 command는 tick 번호가 찍혀 replay 로그(§22.3)에 "입력"으로 기록된다.
```

이 규칙 없이는 deterministic test(§22)와 frame snapshot(§26)이 재현되지 않는다.

---

# 9. Transaction

AI는 한 번에 여러 작업을 요청할 가능성이 높다.

예:

```text
Goblin 만들고,
Collider 붙이고,
Health 100 넣고,
Prefab으로 저장해.
```

각 command를 바로 저장하면 중간 실패가 생긴다.

따라서:

```text
BeginTransaction
  CreateEntity
  AddComponent
  AddComponent
  SetProperty
  CreatePrefab
Commit
```

필요하다.

실패하면:

```text
Rollback
```

한다.

CLI 예:

```bash
game tx begin
# → { "ok": true, "result": { "tx": "tx_01j5…", "expiresAt": "…" } }

game entity create --name Goblin --tx tx_01j5…
game component add @last Collider --tx tx_01j5…
game component add @last Health --tx tx_01j5…
game set @last /components/Health/max 100 --tx tx_01j5…
game prefab create @last --name Goblin --tx tx_01j5…

game tx commit tx_01j5…
```

(`GAME_TX` 환경변수로 `--tx`를 생략할 수 있다.)

## 9.1 ▶ v2: Transaction은 명시적 handle이다

초안의 `game tx begin … game tx commit`은 별도의 CLI 호출에 걸쳐 있으므로 **상주 프로세스**(§88.1)와 **명시적 handle**을 전제한다. MCP 2026-07-28은 프로토콜 수준 세션을 제거했고, 상태가 필요한 서버에 "server-minted opaque handle을 일반 인자로 주고받으라"고 명시했다 (SEP-2567). 우리 `tx_…`가 정확히 그것이다.

- `tx.begin`은 opaque handle과 TTL을 돌려준다. 모든 Mutation command는 `tx` 인자를 받는다.
- 만료되거나 모르는 handle은 `TX_UNKNOWN_OR_EXPIRED` 오류(§13, `error.ruleId`)로 돌려주고, AI는 새 tx를 시작한다.
- 연결별 암묵 상태에 절대 의존하지 않는다.
- 기존 AI 조작 경로 중 호출 단위 transaction을 명시적으로 노출하는 것은 Unreal Remote Control의 `generateTransaction`(Editor undo history 기록)이고, Unity MCP와 Godot editor-plugin MCP 서버들은 Editor undo stack에 의존한다(Unreal MCP는 문서화되지 않음). soft-ue-cli는 transaction/undo와 batch atomicity가 없음을 README에 한계로 적고 있고, Bevy BRP는 설계 gist에서 BEGIN/END를 "나중에"로 미뤘으며 2026-08 현재도 없다. **어느 것도 multi-call atomic commit / rollback은 제공하지 않는다.** JSON-RPC 2.0 batch 배열은 전송 단위일 뿐 원자적이지 않다.

## 9.2 ▶ v2: Commit 경로와 원자성

초안은 Begin/Commit/Rollback만 정의하고 "언제 디스크에 쓰는가"를 정의하지 않았다. Windows의 `ReplaceFile` / `MoveFileEx`는 원자성이 문서화되어 있지 않고 `ReplaceFile`은 부분 실패 상태(`ERROR_UNABLE_TO_MOVE_REPLACEMENT` 등)를 명시한다. 여러 파일에 걸친 commit은 어떤 OS에서도 원자적이지 않다. 따라서:

```text
tx.begin
  → 메모리 상 ProjectModel(fork)에만 ops 적용. 파일 미기록.
  → Validate
tx.commit
  0. tx 안에서 command별로 만들어진 ChangeSet들을 순서대로 compose해 **하나의 ChangeSet**으로 만든다
     (ops 이어붙임, intent는 배열, touched/base 합집합). history의 entry 단위 = undo 단위 = tx.
  1. 그 ChangeSet을 Cache/journal/<cs-id>.json 에 먼저 기록 (write-ahead)
  2. touched 문서마다: §5.3 canonical serialize → <file>.tmp 에 write + flush → rename
     (POSIX rename / Windows MoveFileEx(MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
  3. file.* ops: blob store → 대상 경로로 동일하게 temp+rename
  4. journal entry를 'committed'로 표시, history에 append (§10)
tx.rollback / 중간 실패
  = 메모리 모델 폐기. 파일을 아직 안 건드렸으므로 비용 0.
프로세스 시작 시
  'committed' 아닌 journal이 있으면 각 파일의 현재 hash를 보고
  ChangeSet을 끝까지 재적용하거나 inverse로 되돌린다.
```

**Optimistic concurrency**: ChangeSet은 `base`(touched 파일들의 commit 시점 hash)를 가진다. commit 시 현재 파일 hash가 `base`와 다르면(사람이 Editor나 외부 편집기로 건드린 경우, 또는 다른 agent) `BASE_MISMATCH`로 거부하고 AI에게 재조회를 유도한다. RFC 6902의 `test` op / event store의 "reject append if stream changed"와 같은 원리다.

---

# 10. Undo / Redo

Editor만을 위한 기능이라고 생각하면 안 된다.

AI에게도 중요하다.

예:

```text
AI
 ↓
대량 수정
 ↓
validation 실패
 ↓
transaction rollback
```

또는:

```bash
game history
game undo 3
game redo 1
```

AI가 자기 행동을 되돌릴 수 있다.

## 10.1 ▶ v2: Undo / Redo 알고리즘 — command별 Undo 구현은 없다

ChangeSet(§78)의 모든 `remove/replace/move`가 `before` 값을 들고 있으면 ChangeSet은 **source 문서 없이도 역연산 가능**하다. 따라서 Undo는 command마다 구현하지 않고 CommandBus가 기계적으로 처리한다.

```text
inverse(cs.ops) = reverse( ops.map(op ⇒
  add(p, v)              → remove(p, before=v)
  remove(p, before)      → add(p, value=before)
  replace(p, before=b, value=a) → replace(p, before=a, value=b)
  move(from, to)         → move(from=to, to=from)
  test(p, v)             → (삭제)
  file.add(blob)         → file.remove(beforeBlob=blob)
  file.remove(bb)        → file.add(blob=bb)
  file.replace(bb, b)    → file.replace(beforeBlob=b, blob=bb)
))

Undo(cs) = Apply(inverse(cs))
Redo(cs) = Apply(cs.ops)          // intent를 재실행하지 않는다. effect를 재적용한다.
```

- **Redo는 command를 다시 실행하지 않는다.** 재실행하면 새 persistent id가 발급되어 이후 history의 참조가 깨진다. Unity가 `RegisterCreatedObjectUndo`로 생성을 undo 대상으로 등록하고 Godot이 `add_do_reference`로 노드를 살려두는 것과 같은 목적이다.
- Transaction의 undo = commit 시 compose된 단일 ChangeSet의 inverse (= 내부 command들의 inverse를 역순 적용한 것과 같다; Godot `backward_undo_ops=true`). §9.2 참조.
- History는 `Cache/history/<session>.jsonl`에 ChangeSet을 append하고 `undoCursor`를 따로 둔다. text-first라서 **프로세스 재시작 후에도 undo 가능**하다 — Unreal의 `FTransaction`은 "cannot be made persistent"다.
- Godot에서 차용: `MergeMode { Disable, Ends, All }` (Ends = 첫 action의 before + 마지막 action의 after만 유지; Editor 슬라이더 드래그, AI의 반복 set을 하나로 합침), history의 `version` 카운터(dirty/saved 판정).
- History는 단일 스택이 아니라 **context별**로 둔다: world별(`history/<world_01j5…>`) + `global`(project.json, Config/, asset metadata) + `remote`(play 중 runtime live edit, §88.2). action이 어느 history로 가는지는 첫 Change의 문서로 결정한다 (Godot `EditorUndoRedoManager` 방식).

## 10.2 ▶ v2: 누구의 변경을 되돌리는가

이 문서의 시나리오는 AI와 사람(Editor)이 **같은 프로젝트를 동시에** 건드린다. Loro의 `UndoManager`와 Figma의 원칙은 같다: undo는 **자기 변경만** 되돌린다.

- 모든 ChangeSet에 `actor`(`ai:claude#42`, `human:editor`, `system:migrate`)를 태깅한다.
- `game undo --actor ai:*`는 해당 actor의 최신 ChangeSet의 `touched` pointer가 이후 다른 actor의 ops와 겹치지 않을 때만 허용한다. 겹치면 `UNDO_CONFLICT` diagnostic(겹친 cs id 포함)을 돌려준다.
- 일반적인 selective undo(commutation / rebase)는 **비목표**다. jiff 같은 라이브러리도 experimental로 표시한다.
- Redo stack은 새 mutation이 commit되면 버린다(선형 history). Figma식 "undo modifies redo history"는 멀티유저 전용이라 도입하지 않는다.

CRDT(Automerge / Yjs / Loro)와 OT를 **채택하지 않는** 결정도 기록한다: (a) 목적이 오프라인·멀티피어 병합이며 여기엔 없다, (b) Automerge는 공식 undo가 없고 Loro는 op log 영구 보관 + tree move 충돌 해소 비용이 따른다, (c) JSON 파일이 source of truth인데 CRDT는 자체 바이너리 op log가 source of truth가 되어 §5와 충돌한다, (d) Figma조차 OT를 버리고 per-property LWW + 서버 순서화를 택했다. 차용하는 것은 per-property 단위 변경(= ops), parent property + fractional index 계층, cycle 거부, 자기 변경만 undo, 삭제 subtree를 `before`에 보존 — 이다. ChangeSet(ops + actor + base)은 나중에 멀티피어가 필요해지면 그대로 LWW 서버 로그로 승격할 수 있다.

---

# 11. CLI가 1급 인터페이스여야 한다

GUI 없는 상태에서 프로젝트의 거의 모든 작업이 가능해야 한다.

예:

```bash
game project info
game world list
game entity list

game entity create Enemy
game entity delete entity_01j5xq…          # 또는 name:Enemy, path:Arena/Enemy, 고유 prefix

game component list entity_01j5xq…
game component add entity_01j5xq… Health
game component remove entity_01j5xq… Health

game get entity_01j5xq… /components/Transform/position
game set entity_01j5xq… /components/Transform/position "[10,0,5]"
game set name:Enemy Transform.position "[10,0,5]"    # dotted path는 입력 sugar. 출력은 항상 JSON Pointer

game prefab create entity_01j5xq… --name EnemyBasic

game validate
game build
game run
game test
```

▶ v2: Godot CLI에서 차용할 플래그 — `--fixed-fps <n>`(실시간 동기화 해제), `--time-scale`, `--quit-after <ticks>`, `--disable-render-loop`, `--benchmark-file <json>`, `--log-file`, `--check-only`(파싱/검증만). 그리고 Godot이 **못 하는** 것을 계약으로 둔다: **모든 Command와 모든 Editor-facing API는 `--headless`에서 동일하게 동작한다. "Editor 전용" 함수 분류를 만들지 않는다.** Godot은 `EditorScript`/`EditorInterface`가 `--headless --script`에서 안 돌고, `--headless --editor` + tool script로 우회해야 하며 "editor ready" 신호가 없어(proposal #14502, 2026-03) headless 자동화가 취약하다. 이 문서에서 Command layer가 유일한 editor-facing API인 이유다.

---

# 12. 출력은 기본적으로 사람이 아니라 기계가 읽을 수 있어야 한다

사람용:

```bash
game entity create Enemy
```

```text
Created Entity

ID: entity_01j5xq…
Name: Enemy
```

AI용:

```bash
game entity create Enemy --json
```

▶ v2: 초안의 예시를 **규범적 envelope**로 교체한다. 이 하나의 구조가 `--json` 출력이자 MCP `structuredContent`이며, `schema/envelope.schema.json`(`$id`: `game://schema/envelope/1`, JSON Schema 2020-12)으로 공개되어 모든 tool의 `outputSchema`가 이를 참조한다. **CLI와 MCP가 같은 schema로 검증되는 것**이 §48 동등성을 기계적으로 검사할 수 있는 유일한 방법이다.

```json
{
  "ok": true,
  "command": "entity.create",
  "result": { "id": "entity_01j5xq…", "name": "Enemy", "path": "TestArena/Enemy" },
  "changes": [
    { "op": "add", "doc": "Worlds/TestArena.world.json", "path": "/entities/entity_01j5xq…",
      "value": { "name": "Enemy", "parent": null, "order": "a1", "components": {} } }
  ],
  "warnings": [
    { "ruleId": "ENTITY_NAME_DUPLICATE", "level": "warning",
      "message": { "text": "Another entity named 'Enemy' exists." },
      "logical": { "object": "entity_01j5xp…" } }
  ],
  "meta": {
    "schemaVersion": 1,
    "engineVersion": "0.3.0+g1a2b3c",
    "tx": "tx_01j5…",
    "dryRun": false,
    "durationMs": 4,
    "truncated": false,
    "nextCursor": null
  }
}
```

규칙:

1. `ok`가 discriminator다. `result`는 ok일 때만, `error`는 !ok일 때만 존재한다. `warnings`, `changes`, `meta`는 항상 존재한다(비어 있을 수 있음).
2. stdout에는 envelope만 나간다. 로그·진행 표시는 stderr로 (clig.dev).
3. **stdout이 TTY가 아니면 기본 출력이 JSON이다.** `--output text|json|ndjson`으로 바꾼다. 2026년 agent-facing CLI 관례(Arcjet 2026-06, Garbas 2026-02)는 `--json` opt-in이 아니라 TTY 감지다; clig.dev는 `--json` opt-in 플래그와 stderr 분리, 비-TTY에서의 색/애니메이션 해제를 권고한다. `ndjson`은 `run`/`test`/`trace` 스트림용이며 마지막 줄이 최종 envelope다 (Claude Code `stream-json`과 같음).
4. 필드 선택 `--fields id,name`과 `--jq <expr>` (gh CLI 스타일. jq 구현을 내장해 외부 jq 없이 동작).
5. 모든 list 결과는 `--limit`(기본 50)과 `--cursor`를 따른다. 조용히 자르지 않고 `meta.truncated=true` + `meta.nextCursor`.
6. `changes[]`는 §78의 ChangeSet op 구조 그대로다. Undo / Diff / AI 자기검증이 한 모양을 공유한다.
7. **JSON 출력은 API 계약이다.** 필드는 추가만 하고 rename/삭제하지 않는다. 바꿔야 하면 `meta.schemaVersion`을 올린다. (Arcjet: "add a new command, but do not remove an old one")

## 12.1 ▶ v2: 출력 예산

Claude Code는 MCP tool 출력을 기본 **25,000 토큰**에서 자르고 10,000에서 경고하며, tool description은 2KB에서 자른다. 다단계 루프에서 context 비용을 지배하는 것은 tool 수가 아니라 **결과 크기**다.

- 기본 `--limit 50`, `--fields` 투영, `inspect --response-format concise|detailed` (concise 기본, ≈ 1/3 토큰)
- 긴 문자열은 `…`로 자르고 `meta.truncated`
- world 전체 dump는 기본 금지. `game dump world`는 `--all` 또는 파일로 쓰고 경로만 돌려준다
- screenshot은 CLI에서는 파일 경로로 (MCP는 image content를 돌려줄 수 있으나 토큰 상한은 같다)
- 목표: 보통 명령 출력 < 2K 토큰, 상한 25K 훨씬 아래

---

# 13. Error 구조

최악:

```text
Error occurred.
```

좋음 (▶ v2: RFC 9457 Problem Details + MCP `isError` 의미론 + rustc applicability 를 합친 규범적 형태로 교체):

```json
{
  "ok": false,
  "command": "component.add",
  "error": {
    "ruleId": "COMPONENT_DEPENDENCY_MISSING",
    "level": "error",
    "category": "validation",
    "message": { "text": "CharacterMovement requires Transform." },
    "logical": { "object": "entity_01j5xq…", "component": "CharacterMovement", "propertyPath": null },
    "physical": { "uri": "Worlds/TestArena.world.json",
                  "jsonPointer": "/entities/entity_01j5xq…/components/CharacterMovement",
                  "region": { "startLine": 42, "startColumn": 5, "endLine": 48, "endColumn": 6 } },
    "details": { "missing": ["Transform"] },
    "retryable": false,
    "fixes": [
      {
        "description": "Add Transform component",
        "applicability": "MachineApplicable",
        "isPreferred": true,
        "commands": [ { "op": "component.add", "args": { "entity": "entity_01j5xq…", "component": "Transform" } } ],
        "cli": "game component add entity_01j5xq… Transform --json"
      }
    ],
    "fingerprint": "sha256:3c1f…",
    "helpUri": "game://docs/rules/COMPONENT_DEPENDENCY_MISSING"
  },
  "warnings": [], "changes": [], "meta": { "…": "…" }
}
```

AI가 오류를 읽고 바로 고칠 수 있다.

규칙:

- **`error`는 §79 `Diagnostic`에 `category` / `retryable` / `details`를 더한 것이다.** 필드명은 §79와 같다 (`ruleId`, `level`, `message.text`, `logical`, `physical`, `fixes`, `fingerprint`, `helpUri`). `ruleId`는 안정적인 SCREAMING_SNAKE 식별자이며 reflection/diagnostic 시스템이 생성하는 **registry**에서 나온다 (`game rules --json`으로 조회).
- `category` ∈ `usage | validation | not_found | conflict | precondition | crash | timeout | internal | cancelled`.
- `message.text`는 agent에게 **고치는 법**을 말한다. 내부 정보는 `details`로 (RFC 9457의 `detail` 지침).
- `fixes[]`는 항상 Command 형태와 CLI 형태를 함께 준다. `applicability`는 rustc JSON 진단의 4단계(§79)를 따른다 — agent는 `MachineApplicable`만 자동 적용한다.
- `physical`은 SARIF처럼 **파일 + JSON Pointer + 줄 범위**다. text-first 프로젝트에서 AI가 파일을 직접 고칠 수 있으려면 논리 위치(object)만으로는 부족하다. nlohmann v3.12의 `JSON_DIAGNOSTIC_POSITIONS`가 파싱 시 byte 위치를 준다.
- 모르는 필드는 무시한다 (forward compatibility; rustc/RFC 9457 모두 같은 규칙).

**MCP 매핑** — 가장 중요한 규칙: 도메인 오류는 JSON-RPC error가 아니라 **`isError: true`인 tool result**로 돌려준다. 규격 원문: "Otherwise, the LLM would not be able to see that an error occurred and self-correct." 같은 envelope이 `structuredContent`와 `content[0].text`에 들어간다. JSON-RPC error는 모르는 tool / malformed params에만 쓴다.

**Exit code** (작고 고정된 표, `game capabilities`에 포함):

```text
0   ok
1   command failed (도메인 오류, envelope은 stdout)
2   usage / argument error (stderr)
3   validation/test failed with findings (game validate, game test)
4   confirmation required (파괴적 작업을 --yes 없이 호출. error.details.confirmCommand에 재실행 명령)
5   not found (모르는 id / handle)
6   crash (minidump 경로 포함, §88.4)
7   timeout (watchdog)
130 / 143  interrupted
```

exit code는 보조 신호다. envelope은 항상 `error.ruleId`를 들고 있다.

---

# 14. Schema First

모든 component는 schema를 제공한다.

▶ v2: 초안의 schema는 비표준 어휘(`"type": "float"`, `"min"`, `"runtimeOnly"`)였다. MCP 2026-07-28은 tool `inputSchema`/`outputSchema`를 **JSON Schema 2020-12**로 정의한다(`$schema` 없으면 2020-12, SEP-2106으로 전체 어휘 허용). glaze와 reflect-cpp도 2020-12 키워드를 낸다. 같은 문서를 §14(component schema) · §15(`game help --json`) · §44(`game describe`) · §46(MCP tools/list) · Inspector UI 생성 · `game validate`에 **그대로** 쓰려면 표준 어휘여야 한다. 엔진 전용 메타데이터는 `x-` 접두어로 분리한다.

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "game://schema/component/Health/1",
  "title": "Health",
  "description": "Hit points and death state",
  "type": "object",
  "x-component-version": 1,
  "x-requires": ["Transform"],
  "x-lifecycle": { "init": "OnSpawn", "tick": "EnemyAI 이후", "destroy": "OnDespawn" },
  "properties": {
    "max":     { "type": "number", "minimum": 1, "default": 100,
                 "description": "Maximum hit points", "x-cpp": "float", "x-unit": "hp",
                 "x-ui": { "widget": "slider", "uiMin": 1, "uiMax": 1000, "step": 1 } },
    "current": { "type": "number", "readOnly": true, "x-runtimeOnly": true, "x-cpp": "float" }
  },
  "required": ["max"],
  "additionalProperties": false
}
```

그러면 AI는 다음을 query할 수 있다.

```bash
game schema component Health
```

검증기: jsoncons(BSL-1.0, draft 4 ~ 2020-12, JSON Pointer/Patch/JSONPath 동봉). valijson·pboettch는 draft-07까지라 부적합, blaze는 AGPL-3.0. 2020-12에서 `format`은 기본적으로 annotation이다 — `require_format_validation`을 켜거나 의존하지 않는다.

## 14.1 ▶ v2: Type schema만으로는 부족하다 — wire format guide

Bevy는 0.16부터 `registry.schema`를 제공했지만, bevy_brp_mcp 저자는 "schema는 spawn/insert/mutate가 정확히 무엇을 기대하는지 말해주지 않는다. agent가 trial-and-error에 빠진다"고 기록하고 별도의 runtime **type guide**(spawn 예시 + 유효한 mutation path 목록)를 추가해야 했다. 원인: glam 벡터가 배열로 직렬화되는데 schema는 named field로 보이고, enum variant 표현이 모호하며, mutation path가 정적 schema에서 유도되지 않았다.

처음부터 두 층을 둔다.

```text
registry.schema <type>        — 위의 JSON Schema 2020-12
registry.wire_format <type>   — 실제 명령 인자로 보내야 하는 JSON의 정확한 예시 + 유효한 mutation path 목록
```

```json
{
  "type": "Transform",
  "spawnExample": { "position": [0,0,0], "rotation": [0,0,0,1], "scale": [1,1,1] },
  "mutationPaths": [ "/position", "/position/0", "/position/1", "/position/2", "/rotation", "/scale" ],
  "enumFormats": { "/shape": ["box", "sphere", "capsule"] }
}
```

규칙: vec/quat/color는 **항상 고정 길이 배열**이고 mutation path는 `/position/0`이다 (`/position/x` 아님). 모든 reflection-serialized 타입의 wire format은 schema에서 1:1로 파생 가능해야 하며, **실패한 명령의 `error.details`에 해당 타입의 wire_format을 자동 첨부**한다 (§13).

---

# 15. Capability Discovery

AI가 documentation을 전부 읽을 필요가 없어야 한다.

```bash
game capabilities
```

▶ v2: 초안은 command 이름 목록만 돌려줬다. Bevy의 `rpc.discover`가 정확히 그렇게(이름만, params는 빈 배열 — PR #18068) 구현되어 있고, agent에게 불충분하다고 문서화되어 있다. **이름 목록은 capability discovery가 아니다.** `game capabilities --json`은 **완전한 tool descriptor**를 내고, MCP adapter의 `tools/list`는 이것의 pass-through다.

```json
{
  "ok": true,
  "result": {
    "info": { "title": "AI-Native Game Framework Command API", "version": "0.3.0" },
    "tools": [
      {
        "name": "query",
        "title": "Query entities",
        "description": "… (≤ 2KB, 핵심을 앞에) …",
        "inputSchema":  { "type": "object",
                          "properties": { "filter": { "$ref": "#/$defs/QueryFilter" }, "data": { "$ref": "#/$defs/QueryData" },
                                          "expr": { "type": "string" }, "limit": { "type": "integer", "default": 50 }, "cursor": { "type": ["string","null"] },
                                          "tx": { "type": "string", "pattern": "^tx_[0-7][0-9a-hjkmnp-tv-z]{25}$" } },
                          "additionalProperties": false },
        "outputSchema": { "$ref": "game://schema/envelope/1#/$defs/QueryResult" },
        "annotations": { "readOnlyHint": true, "destructiveHint": false, "idempotentHint": true, "openWorldHint": false },
        "cli": "game query [--with T…] [--without T…] [--expr E]",
        "exitCodes": [0, 1, 2]
      }
    ],
    "commands": [
      {
        "id": "entity.create",
        "aliases": ["world.spawn_entity"],
        "kind": "Mutation",
        "argsSchema":   { "type": "object", "properties": { "name": {"type":"string"}, "world": {"type":"string","pattern":"^world_[0-7][0-9a-hjkmnp-tv-z]{25}$"},
                                                            "tx": {"type":"string","pattern":"^tx_[0-7][0-9a-hjkmnp-tv-z]{25}$"} },
                          "required": ["name"], "additionalProperties": false },
        "resultSchema": { "$ref": "game://schema/envelope/1#/$defs/EntityCreateResult" },
        "cli": "game entity create <name> [--world ID] [--tx ID]"
      }
    ],
    "exitCodes": { "0": "ok", "1": "…" },
    "errorCodes": [ "COMPONENT_DEPENDENCY_MISSING", "…" ]
  }
}
```

규칙:

- **두 층**이다: `tools[]`는 MCP에 노출되는 15개 tool(§47), `commands[]`는 전체 Command 목록(`apply.changes[].op`의 `oneOf`이자 `game://schema/commands` resource). MCP `tools/list`는 `tools[]`의 기계적 pass-through다. Bevy의 `rpc.discover`와 달리 **params/result schema를 전부 채운다.** (OpenRPC 1.3.2 문서는 `commands[]`에서 생성해 `game capabilities --format openrpc`로 낼 수 있다.)
- 결정적 순서 (MCP SHOULD; prompt cache hit rate).
- `inputSchema`에 `additionalProperties: false`. component 타입, property path, enum 값은 reflection에서 생성한 `enum` / `pattern`으로 넣어 잘못된 호출을 **호출 전에** 막는다 (Arcjet: hallucinated id를 로컬에서 거부).
- `annotations`는 MCP 2026-07-28 `ToolAnnotations`(기본값 readOnly=false, destructive=true, idempotent=false, openWorld=true; 클라이언트는 untrusted로 취급). 우리 규칙: get/query/dump/explain/refs/schema/validate/capture → `readOnlyHint:true`; create/add/set → `destructiveHint:false`; delete/asset.delete/checkpoint.restore → `destructiveHint:true`; `property.set`/idempotencyKey 있는 `apply` → `idempotentHint:true`; 전부 `openWorldHint:false`.
- **tool 집합은 프로세스 수명 동안 고정이다.** MCP 2026-07-28: tools/list는 "다른 요청의 부작용으로 바뀌면 안 된다". 프로젝트별 어휘(component 타입, enum)는 tool 정의를 재생성하는 게 아니라 `game schema` / `schema` resource와 inputSchema의 enum으로 노출한다.

그리고:

```bash
game help world.spawn_entity --json
```

으로 세부 schema를 얻는다.

---

# 16. Query System

AI-native 시스템에서 굉장히 중요하다.

AI가 Hierarchy를 뒤질 필요가 없어야 한다.

예:

```bash
game query --with Health
```

```bash
game query \
    --with EnemyAI \
    --without Collider
```

▶ v2: 초안의 자체 DSL(`Entity WHERE Has(EnemyAI) AND NOT Has(Collider)`)은 **만들지 않는다.** 두 개의 기성 형태가 이미 있고, 자체 문법은 가르쳐야 할 두 번째 언어가 된다.

**정식 wire 형태 = BRP `world.query`의 구조를 그대로** (Bevy가 실전에서 필요했던 `option`/`has` 투영과 `strict` 정책 포함):

```json
{ "method": "world.query", "params": {
    "data":   { "components": ["Transform"], "option": ["Health"], "has": ["Collider"] },
    "filter": { "with": ["EnemyAI"], "without": ["Collider"] },
    "strict": false,
    "limit": 50, "cursor": null, "fields": ["id", "name", "path"] } }
```

- `strict:false`(기본)는 직렬화 불가 component를 건너뛰고, `strict:true`는 실패한다.
- CLI `--with/--without/--option/--has`는 이 구조의 1:1 sugar다.

**Flecs를 채택하면**(§3.1) 위 구조에 선택적 `expr` 필드를 더해 Flecs Query Language 문자열을 그대로 받는다. 이미 runtime에 파싱되고 REST로 노출되는 언어다.

```bash
game query --expr "EnemyAI, !Collider"
game query --expr "Enemy, ?Collider, (ChildOf, Encounter_05)"
game query --expr "Health, \$this ~= \"Goblin\""
game query --expr "Position(up ChildOf), Sprite"
game query --expr "EnemyAI, !Collider" --explain      # query plan JSON
```

(AND `,` / NOT `!` / OR `||` / optional `?` / pair `(R, T)` / wildcard `*` `_` / traversal `up` `cascade` / 변수 `$var` join / 이름 매칭 `~=`.)

**EnTT를 채택하면** 구조화 형태만 지원한다. 텍스트 문법은 만들지 않는다.

결과 row는 항상 id + name + path를 포함한다 (§7.4):

```json
{
  "ok": true,
  "result": {
    "rows": [
      { "id": "entity_01j5xqd6…", "name": "Goblin_01", "path": "TestArena/Encounter_05/Goblin_01",
        "components": { "Transform": { "…": "…" } }, "has": { "Collider": false } },
      { "id": "entity_01j5xqe8…", "name": "Goblin_03", "path": "TestArena/Encounter_05/Goblin_03",
        "components": { "Transform": { "…": "…" } }, "has": { "Collider": false } }
    ]
  },
  "meta": { "truncated": false, "nextCursor": null, "…": "…" }
}
```

snapshot 안에서의 **선택**(assertion이 아님)에는 JSONPath를 쓴다 (§26.1): `game inspect snapshot:813 --jsonpath '$.entities[?(@.components.EnemyAI.state=="Chasing")].id'`. 목표 문법은 RFC 9535이고 구현 후보 jsoncons는 Goessner 방언을 표방하므로 Phase 5에서 compliance suite로 확인한다.

---

# 17. Semantic Query

초기에는 없어도 된다.

하지만 장기적으로는:

```bash
game query "enemies without collision"
```

같은 자연어를 내부 deterministic query로 변환할 수 있다.

중요한 점은 LLM이 runtime 데이터를 직접 뒤지는 것이 아니라:

```text
Natural Language
 ↓
Structured Query
 ↓
Query Engine
```

으로 내려가는 것이다.

---

# 18. Explain

각 오브젝트의 존재 이유와 관계를 쉽게 볼 수 있어야 한다.

```bash
game explain entity_01j5xqd6…
```

출력:

```text
Goblin_01

Source:
  Prefab: prefab_01j5xq8z… (Goblin)

Spawn:
  World: world_01j5xqb2… (TestArena)
  Parent: Encounter_05

Components:
  Transform
  SpriteRenderer
  Collider
  Health
  Movement
  EnemyAI

References:
  EnemyAI.target → entity_01j5…player (Player)
  EnemyAI.patrolRoute → entity_01j5…route03 (PatrolRoute_03)
```

AI가 Context를 얻기 굉장히 쉬워진다.

▶ v2: GameEngineBench(2026-07, 실제 UE5 C++ 프로젝트 9개에서 agent 평가)에서 **모든 모델이 못 푼 31개 과제**는 authority(어느 머신에서 실행되는가), replication, object lifecycle(초기화/정리 순서), subsystem 등록 시점에 집중됐다. "컴파일되는 코드라도 잘못된 머신에서 실행되거나, 로컬 상태만 갱신하거나, 정리가 너무 늦거나, 다른 시스템이 기대하기 전에 component를 등록하지 않으면 실패한다." 따라서 `game explain`과 `game describe component X`는 requires/provides뿐 아니라 다음을 machine-readable로 노출한다.

```text
Lifecycle:
  init:    OnSpawn  (after Transform, before EnemyAI)
  tick:    Update phase 3 (after Physics, before Render)
  destroy: OnDespawn
Context:
  authoring: editable        runtime: Health.current mutable
  headless:  supported
Depends on systems:
  PhysicsSystem (must be initialized first)
```

Flecs를 채택하면 `game explain`은 `GET /entity/<path>?values=true&inherited=true&refs=*&matches=true&doc=true&type_info=true`(`ecs_entity_to_json`의 serialize_inherited / serialize_refs / serialize_matches / serialize_doc 옵션)의 얇은 포매터다. 남는 일은 텍스트 포맷과 asset-as-entity 규약(§19)뿐이다.

---

# 19. Reference Graph

Project 전체를 그래프로 관리한다.

예:

```text
World
 ↓
Entity
 ↓
Prefab
 ↓
Texture
```

다음 명령이 가능해진다.

```bash
game refs asset_01j5xq9a…
```

```text
Referenced by:

prefab_01j5xq8z… (Goblin)        /components/SpriteRenderer/sprite
prefab_01j5…elite (GoblinElite)  /components/SpriteRenderer/sprite
world_01j5xqb2… (TestArena)      /entities/entity_01j5…/set/~1components~1SpriteRenderer~1sprite   (override 키는 pointer 안에서 ~1 escape)
```

삭제 전에:

```bash
game asset delete asset_01j5xq9a…
```

하면:

```text
ERROR
Asset is referenced by 3 objects.
```

AI가 asset dependency를 안정적으로 처리할 수 있다.

▶ v2:

- **Reference graph는 authoring JSON 문서에서 빌드한다** (파일 안의 모든 `*_01j…` 형태 값 + schema의 `x-ref` 표시). runtime world와 무관하게 `game refs`가 headless에서 동작해야 한다.
- Flecs를 채택하면 runtime 측에서는 asset을 entity로 모델링하고(`asset_01j…` = scope `assets.texture` 아래 entity), 참조를 relationship pair(`(Uses, assets.texture.goblin)`)로 두어 `serialize_refs`, `OnDeleteTarget` trait(Remove | Delete | Panic), `Acyclic` trait(순환 금지)을 그대로 쓸 수 있다. 그러나 이는 authoring 그래프의 **검증용 미러**이지 source가 아니다.
- **Sub-asset 주소** (§88.7): glTF 하나에 mesh/material/animation이 여럿 들어 있다. 참조는 `asset_01j…#<kind>/<name>`(예 `#meshes/Body`, `#animations/Run`, `#sprites/goblin_idle`)이고, sidecar(§37)가 sub-asset의 **안정적 이름 목록**을 가진다. 재import 시 index가 아니라 이름으로 매칭한다 (index·생성 순서에 묶인 sub-asset 참조는 재import 시 깨질 수 있다는 것이 근거 — 리서치에서 특정 버그를 직접 확인한 것은 아님).
- 삭제 guard는 `--force`가 있어도 **조용히 참조를 끊지 않는다.** `asset.delete --force`는 끊긴 참조마다 `REF_DANGLING` diagnostic을 ChangeSet과 함께 돌려준다.

---

# 20. Headless Runtime

이 시스템에서 가장 중요한 기능 중 하나.

```bash
game run --headless
```

이 실행되어야 한다.

렌더링을 완전히 끌 수도 있고:

```text
--no-render
```

offscreen rendering만 켤 수도 있다.

```text
--offscreen
```

▶ v2: 두 모드는 만들 필요가 없다. **SDL3가 이미 두 개의 headless video driver를 제공한다.**

```text
--no-render  ⇒ SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy") + SDL_HINT_AUDIO_DRIVER=dummy
               GPU 없음, simulation만. 가장 빠름. §23 gameplay 테스트 전부 이 모드.
--offscreen  ⇒ SDL_HINT_VIDEO_DRIVER="offscreen"
               SDL_OFFSCREEN=ON 기본 빌드. EGL/GL + 기본 Vulkan.
               capture / golden 테스트(§23 events.Screenshot, assert.capture)는 이 모드에서만 유효.
               dummy에서 capture를 요구하면 TEST_CAPTURE_REQUIRES_RENDERER validation error.
               Windows SDL_GPU/D3D12에는 현재 WARP adapter 선택 옵션이 없다
               (SDL_HINT_RENDER_DIRECT3D11_WARP는 2D renderer 전용, 3.4.0+). CI용 software rasterizer는
               (a) SDL_GPU Vulkan 백엔드 + SwiftShader/lavapipe ICD, 또는 (b) SDL_gpu_d3d12에
               IDXGIFactory4::EnumWarpAdapter 분기 패치 — Phase 5 전에 검증 (§27.1).
```

hint는 쉼표로 구분한 우선순위 목록이므로(SDL 3.2.0+) CI는 `"offscreen,dummy"`를 써서 GL/Vulkan ICD가 없으면 no-render로 강등되게 한다. 결과 JSON에는 `SDL_GetCurrentVideoDriver()`로 **실제 사용된 driver**를 기록한다 — 재현성과 golden 이미지 bucketing(§27.1)에 필요하다.

Bevy의 대응물: `--no-render` ≈ `MinimalPlugins` + `ScheduleRunnerPlugin`, `--offscreen` ≈ `headless_renderer` 예제(40 pre-roll frame 후 capture).

## 20.1 ▶ v2: Headless 루프는 accumulator가 없다

```bash
game run --world TestArena --headless \
    --seed 381251 \
    --ticks 3600          # --frames 는 --ticks 의 alias. render frame이 아니라 simulation tick
    --tick-rate 60 \
    --threads 1|N \
    --hash-every 1 --hash-out hashes.jsonl \
    --replay-out run.replay \
    --timeout 120s        # watchdog (§88.4)
```

```cpp
for (tick = 0; tick < N; ++tick) {
    InputFrame in = replay ? replay.Read(tick) : inputs.Drain(tick);   // CLI command 포함 (§8.2)
    world.Tick(in, kFixedDt);          // physics.Step 포함. wall-clock 접근 없음
    if (tick % hashEvery == 0) hashes.Write(tick, world.Hash());
}
```

Gaffer의 fixed-timestep 모델에서 재현성은 **상수 dt**에서 나온다. headless에서는 accumulator 자체를 제거하고 tick 수를 입력으로 고정한다. 실시간 모드의 accumulator/interpolation은 render 쪽 문제다.

---

# 21. AI 루프

최종적으로 원하는 흐름은 다음이다.

```text
AI
 ↓
Code / Data 수정
 ↓
Build
 ↓
Headless Run
 ↓
Validation
 ↓
State Inspection
 ↓
Fail
 ↓
수정
 ↓
재실행
```

여기서 사람이 Editor를 클릭할 필요가 없다.

---

# 22. Deterministic Simulation

테스트에서는 seed를 명시할 수 있어야 한다.

```bash
game run \
    --world TestArena \
    --seed 381251 \
    --frames 3600 \
    --headless
```

결과:

```json
{
  "seed": 381251,
  "frames": 3600,
  "result": "success"
}
```

같은 코드 + 같은 데이터 + 같은 seed라면 최대한 동일한 결과가 나오는 방향을 권장한다.

물론 physics / floating point / multithreading 때문에 완전한 deterministic이 어려운 영역은 존재한다.

초안은 그 경우 다음을 구분하자고 했다:

- deterministic gameplay simulation
- non-deterministic rendering
- physics tolerance

▶ v2: **위 문장들은 고친다.** Box2D v3.1+는 설정 없이 cross-platform 결정적이고 **thread 수와 무관한 bit-exact 결정성**을 FAQ에 명시하며("2 threads == 8 threads"), CI에서 body transform 전체를 해시해 하드코딩된 기대값과 비교한다(`test_determinism.c`: `EXPECTED_HASH 0xE86690F4`, worker 1~32 sweep, MSVC/Clang/GCC/x64/ARM). Jolt는 "같은 binary + 같은 API 호출 순서"면 결정적이고 cross-platform은 `CROSS_PLATFORM_DETERMINISTIC` 플래그(~8% 느림)다 — 단, **thread 수 독립성은 문서화하지 않으므로**(JobSystem 순서 보장으로 암시만) Jolt 채택 시 `--threads 1` vs N hash 비교를 Phase 1 첫 테스트로 둔다. 초안이 제안한 "physics tolerance"(물리 결과에 허용 오차)는 **불필요하며 오히려 divergence를 숨긴다.** 어려운 것은 physics가 아니라 프레임워크 자체 코드다 — Riot(LoL Chronobreak)과 Factorio(lockstep)의 실제 desync 원인은 전부 gameplay 코드였다(미초기화 메모리, 컨테이너 순회 순서, 저장되지 않은 캐시값). Unity의 ECSGalaxySample 지침도 같은 항목(고정 tick, seeded RNG, 생성·순회 순서)을 규칙으로 둔다.

따라서 §22를 "권장"에서 **계약(contract)** 으로 바꾼다.

## 22.1 Determinism Tier

| Tier | 보장 범위 | PoC 필수 여부 |
|---|---|---|
| **T0 same-binary** | 같은 실행 파일 + 같은 데이터 + 같은 seed + 같은 input log → **모든 tick의 world hash가 bit-identical** | 필수 (모든 headless test) |
| **T1 machine/thread-independent** | T0 + thread 수, CPU vendor, 실행 시각, 작업 디렉터리와 무관 (같은 OS/arch) | 필수 (CI와 로컬이 같은 결과) |
| T2 cross-platform | 다른 OS/compiler/arch에서도 동일 (Jolt: `CROSS_PLATFORM_DETERMINISTIC`, Box2D: 기본) | 선택 (networking 필요 시) |

Windows 단일 플랫폼 PoC라면 T2는 범위 밖이다. 그러면 Jolt의 `CROSS_PLATFORM_DETERMINISTIC`(8% 비용)만 뒤로 미룰 수 있다. **결정적 삼각함수(`det::Sin/Cos`, Box2D `b2ComputeCosSin` 래핑)는 T1에도 필요하다** — Windows UCRT의 sin/cos는 CPU의 FMA3 지원 여부에 따라 시작 시 다른 구현을 고르므로, 같은 binary라도 머신마다 결과가 달라질 수 있다.

## 22.2 Determinism Contract Checklist

**Time**
- [ ] Simulation은 고정 tick만 사용. `tickRate`는 project.json에 정수(예 60), dt = 1/tickRate를 매 tick 같은 float 상수로 (누적 합산 금지)
- [ ] `--ticks N` = simulation tick N회. headless는 accumulator 없음 (§20.1)
- [ ] Sim 코드에서 wall-clock API 금지 (`SDL_GetTicks`, `steady_clock` 등). `SimTime`(tick 카운터)과 `WallTime`을 **타입으로 분리**

**Build flags** (모든 sim TU + Game/ + ThirdParty physics에 동일 적용 — §41의 `det_fp_flags` INTERFACE target 하나로 관리)
- [ ] MSVC: **VS2022 이상**, `/fp:precise`(기본) 또는 `/fp:strict`. `/fp:fast`, `/fp:contract` 금지. (VS2022부터만 `/fp:precise`가 FMA contraction을 끈다. 이전 버전은 precise에서도 contraction 가능)
- [ ] Clang/GCC: `-ffp-contract=off`, `-fno-fast-math`. clang-cl은 `/clang:-ffp-contract=off`. (GCC GNU 모드 기본값은 `-ffp-contract=fast`, Clang 기본값은 `on` — 둘 다 FMA contraction을 허용하므로 반드시 `off`를 명시. Box2D CMake가 정확히 이렇게 한다)
- [ ] x64 only. 모든 sim TU가 같은 ISA 플래그 (`/arch`, `-m*` 혼용 금지)
- [ ] 프로세스/worker thread 시작 시 FPU 환경 assert: round-to-nearest, DAZ/FTZ 동일

**Math / STL** (Jolt 문서의 애플리케이션 측 요구 목록을 포함하고, Box2D 블로그(sqrt 허용)와 Riot/Factorio 사례를 더한 것)
- [ ] sim 코드에서 `std::sin/cos/tan/atan2/exp/pow` 금지 → 엔진 `det::Sin/Cos/Atan2` (Jolt `Sin/Cos` 또는 Box2D `b2ComputeCosSin/b2Atan2` 래핑). `sqrt`는 허용 (IEEE-exact)
- [ ] sim 코드에서 `std::unordered_map/set` 순회 금지, `std::hash` 금지, pointer 값 기반 비교/정렬 금지, 동률 키에 `std::sort` 금지 (total order 보장 or `std::stable_sort`), `std::push_heap/pop_heap` 금지 (Jolt 목록)
- [ ] uninitialized read 금지: CI에 ASan/UBSan, clang `-ftrivial-auto-var-init=pattern` 또는 MSVC `/RTCs`(debug). (Riot의 1/5000 게임 desync는 미초기화 float y 좌표였다)

**RNG**
- [ ] 전역 RNG 없음. `RngStream` = xoshiro256** 을 `SplitMix64(hash64(worldSeed, "system.name"))`로 초기화. 시스템별, 필요 시 entity별(persistentId 기반) 스트림. (PCG/SplitMix의 "stream" 파라미터로 스트림을 나누는 것은 저자 스스로 "아무도 테스트하지 않은 generator"라고 경고 — 대신 seed 해싱 + jump)
- [ ] RNG state는 snapshot에 포함 (§26)

**ECS / ID**
- [ ] Runtime EntityId 할당은 순차 counter + 결정적 free-list. 모든 create/destroy는 tick 내 command log 순서로만
- [ ] 순서 민감 시스템은 ECS pool 순서에 의존하지 않는다 (EnTT: view 순서는 가장 작은 pool의 배치에 따라 달라진다 → `view.use<T>()` 또는 creation index/persistentId 정렬; Flecs 4.1.6: "Query groups are no longer iterated in order by default")
- [ ] 병렬 시스템 출력(이벤트, 데미지 등)은 적용 전 `(entityId, seq)`로 정렬 (Unity ECSGalaxySample: `Entity.Index` 정렬)
- [ ] runtime-spawned entity의 persistent id는 결정적으로 파생 (§7.1)

**Physics**
- [ ] tick당 정확히 1회 `PhysicsWorld::Step(dt, collisionSteps=const)`
- [ ] Body 생성 순서 = entity 생성 순서 (Jolt: `CreateBodyWithID`로 entity runtime id ↔ BodyID 고정, 아니면 SaveState snapshot이 호환되지 않음)
- [ ] Contact/Sensor 이벤트는 버퍼링 후 `(bodyA, bodyB, shape)` 정렬 뒤 gameplay에 전달 (Jolt listener 콜백 순서는 멀티스레드라 비결정)
- [ ] Jolt `GetActiveBodies()`, `BroadPhaseQuery` 결과를 순서 민감 로직에 사용 금지 (문서에 비결정으로 명시)

**Input / Replay**
- [ ] 외부 입력(키보드, CLI command, network)은 tick 단위 `InputFrame`으로만 sim에 진입하고 replay 파일에 기록 (§22.3)

**Verification**
- [ ] 매 tick(또는 `--hash-every K`) `worldHash` = 결정적 순서로 직렬화한 sim component 바이트(render-only 제외, float는 **bit pattern**) + RNG state + physics body transform → xxHash64/FNV-1a. 시스템별 sub-hash(`Physics`, `EnemyAI`, `Rng`)도 함께 → 어느 시스템이 먼저 갈라졌는지 즉시 보임
- [ ] CI: 같은 시나리오를 2회 + `--threads 1` vs `--threads N`으로 실행해 hash 시퀀스 diff. **첫 divergent tick**과 해당 tick의 두 snapshot을 key-value diff해 첫 divergent path 보고 (Riot 'diff-log' 방식)
- [ ] 기준 시나리오는 Box2D `test_determinism.c`처럼 `expectedFinalHash`를 테스트 파일에 고정

## 22.3 Replay / Input Log

Riot(서버 네트워크 기록 재생)과 Factorio(desync report)의 공통점: **입력 로그 + seed만으로 전체를 재현**하고, 재생 결과를 원본 hash와 비교해 첫 divergence를 찾는다. 버그 재현 파일 하나를 AI에게 넘길 수 있다.

```text
run.replay/
├── header.json   { engineGitHash, fpFlagsHash, schemaVersion, world, seed, tickRate, threads, videoDriver, startSnapshot? }
├── inputs.jsonl  { "tick": 0, "actions": [ {"action":"MoveX","value":1.0} ],
                    "commands": [ {"op":"property.set", "…":"…"} ] }
└── hashes.jsonl  { "tick": 0, "world": "0x…", "systems": {"Physics":"0x…","Rng":"0x…"} }
```

```bash
game replay record --out run.replay …     # == run + 기록
game replay play   run.replay --headless  # 입력만 재생
game replay verify run.replay --json      # 재생하며 hashes.jsonl과 비교, 첫 divergent tick 보고
```

규칙: play 중 AI/CLI가 보낸 Command도 tick 번호가 붙어 `inputs.jsonl`에 들어간다 (§8 Command = 입력). header의 `engineGitHash`/`fpFlagsHash`가 다르면 `verify`는 T0 보장 불가를 경고한다. GameCraft-Bench가 제출물로 요구하는 "재생 가능한 입력 trace(JSON)"와 같은 artifact이므로 테스트·벤치마크·judge 검토에 같은 파일을 쓴다 (§72).

---

# 23. Test Scenario

테스트 자체도 데이터화한다.

▶ v2: 조사한 성숙한 게임 테스트 하네스(Unreal Automation/Gauntlet, Unity Test Framework, Godot GUT/gdUnit4)는 테스트를 **코드**로 정의한다. Bevy `ci_testing`은 데이터 파일이지만 이벤트 스크립트일 뿐 assertion이 없다. 데이터화된 assertion은 이 문서의 고유한 선택이며, 그만큼 **실행 의미론을 스스로 못 박아야 한다**: 언제 평가하는가, 어떤 표현식 언어인가, 입력은 어떻게 넣는가, artifact는 어떻게 붙는가. 초안의 `"Goblin.State == Dead"`는 enum 리터럴이 따옴표 없이 쓰여 어떤 표현식 언어도 의도대로 파싱하지 않는다.

```json
{
  "$schema": "game://schema/test/1",
  "name": "GoblinBasicCombat",
  "world": "world_01j5xqb2…",
  "seed": 1024,

  "setup": [
    { "spawn": "prefab_01j5…player", "as": "player", "position": [0, 0, 0] },
    { "spawn": "prefab_01j5…goblin", "as": "goblin", "position": [5, 0, 0] }
  ],

  "inputs": [
    { "tick": 0,   "hold": { "MoveX": 1.0 }, "untilTick": 120 },
    { "tick": 130, "press": "Attack" }
  ],

  "events": [
    { "tick": 100,  "event": "Screenshot", "path": "f0100.png" },
    { "tick": 300,  "event": "NamedEvent", "name": "spawn_wave" }
  ],

  "run": { "ticks": 3600, "tickRate": 60, "videoDriver": "offscreen" },

  "determinism": { "tier": "T1", "runs": 2, "threads": [1, 8], "hashEvery": 60, "expectedFinalHash": null },

  "assert": [
    { "id": "player-alive",  "expr": "player.Health.current > 0",            "always": true },
    { "id": "goblin-dies",   "expr": "goblin.EnemyAI.state == \"Dead\"",      "eventually": { "withinTicks": 3600 } },
    { "id": "no-orphans",    "expr": "world.entities.all(e, !has(e.components.EnemyAI) || has(e.components.Collider))", "at": "end" },
    { "id": "golden-end",    "capture": { "camera": "Gameplay", "width": 512, "height": 512, "golden": "combat_end" }, "at": "end" }
  ]
}
```

실행:

```bash
game test GoblinBasicCombat
```

## 23.1 ▶ v2: Assertion 의미론

**언제**: `{"at": "end"}`(기본), `{"at": 813}`, `{"always": true}`(매 tick 검사, 첫 위반에서 실패), `{"eventually": {"withinTicks": N}}`.

**무엇에 대해**: 표현식은 **frame snapshot JSON(§26.1)에 대해** 평가된다. 그래서 실패한 assertion은 저장된 snapshot 위에서 **오프라인으로 재평가**할 수 있다. `setup`의 `as`가 binding 이름을 만들고, binding 이름은 해당 entity의 `components` 객체에 바인딩된다 — `player.Health.current` ≡ `$.entities[?(@.id==bindings.player)].components.Health.current`. `world.entities`는 snapshot의 entity 배열 그대로다(그래서 `e.components.…`). `videoDriver`는 Screenshot/capture가 있으면 `offscreen`, 없으면 `dummy` (§20).

**어떤 언어로** — 이것이 리서치에서 의견이 갈린 지점이다. 결정:

```text
Phase 5:  고정 비교 문법 (언어가 아니다)
          <binding>.<Component>.<property> <op> <literal>   op ∈ == != < <= > >= in
          && || !  has(<path>)  size(<path>)  abs() dist(a, b)
          bounded quantifier: world.entities.all|exists|exists_one(e, <pred>)
          루프 없음, 대입 없음, 함수 정의 없음. 종료·부작용 없음 보장.
          구문은 CEL의 부분집합과 호환되게 쓴다 (나중에 교체 가능).
          구현: 자체 evaluator ~1–2k LOC.

그 이상이 필요해지면: 자체 문법을 키우지 않고 Luau(sandboxed, typed)를 임베드한다 (§61.1).
채택하지 않는 것:
  - cel-cpp (v0.16.1, Apache-2.0): Bazel/BCR 우선, absl + protobuf ≥ 28.3 의존, CMake 미문서화 → 비용 과다
  - JSONPath (RFC 9535): 산술 없음. 선택 언어이지 assertion 언어가 아님 → §26.1의 inspect 용도로만
  - Flecs query + alerts: runtime world 한정. snapshot 오프라인 재평가가 안 됨 → §29 validation 용도로만
```

근거: 2026-06 연구(arXiv 2606.16827)에서 LLM의 pass@1은 high-resource 언어 59–89%, Lua급 low-resource 27–84%, **새로 만든 "no-resource" 언어 0–1%** 다. 자체 DSL은 AI가 가장 못 쓰는 것이다. 위 고정 문법은 "언어"가 아니라 비교기이며, 그 이상은 기성 언어에 올린다.

**입력** (`inputs`): action map은 `Config/input.json`에 schema로 정의한다 (§88.3). `hold / press / release / axis`를 tick 기준으로 기술하고, 실행 시 §22.3 replay의 `inputs.jsonl`로 변환된다. 따라서 **`game replay record`로 사람이 플레이한 세션을 그대로 테스트 fixture로 저장**할 수 있다.

**이벤트** (`events`): Bevy `bevy_ci_testing`(`CI_TESTING_CONFIG` ron, frame-indexed `AppExit`/`Screenshot`/`NamedEvent`)에서 차용. `GAME_TEST_CONFIG=<file>` 환경변수로 같은 바이너리가 플래그 없이 CI에서 돈다.

**결정성** (`determinism`): §22.2 Verification을 테스트 단위로 선언한다. `runs: 2` + `threads: [1, 8]`이면 T1을 검사한다.

---

# 24. Test Output

▶ v2: 초안은 실패한 assertion 하나만 보여줬다. 조사한 모든 하네스는 **run 단위 보고서**(테스트별 상태·시간·artifact)를 내고, CI 시스템(GitLab/Jenkins)은 JUnit XML과 `[[ATTACHMENT|path]]` 관례만 읽는다. 둘 다 낸다.

`Tests/.results/<run>/results.json`:

```json
{
  "schemaVersion": 1,
  "run": {
    "id": "run_01j5…", "startedAt": "2026-08-21T10:00:00Z", "durationMs": 2410,
    "engineVersion": "0.3.0+g1a2b3c", "projectRev": "git:9f8e7d", "fpFlagsHash": "…",
    "platform": "win-x64", "videoDriver": "offscreen", "gpuBackend": "vulkan/swiftshader", "threads": 8
  },
  "summary": { "total": 12, "passed": 11, "failed": 1, "errored": 0, "skipped": 0 },
  "tests": [
    {
      "name": "GoblinBasicCombat", "file": "Tests/Combat/GoblinBasicCombat.test.json",
      "status": "failed", "durationMs": 812, "ticksRun": 813, "seed": 1024,
      "abortedAt": 813, "abortReason": "determinism divergence (runA vs runB)",
      "failures": [
        {
          "assertId": "goblin-dies", "expr": "goblin.EnemyAI.state == \"Dead\"", "tick": 813,
          "expected": true, "actual": false, "note": "eventually 창(3600) 전에 run이 중단되어 미충족으로 판정",
          "bindings": { "goblin.EnemyAI.state": "Chasing", "goblin.Health.current": 24 },
          "diagnostic": { "ruleId": "TEST_ASSERTION_FAILED", "level": "error", "message": { "text": "…" } }
        }
      ],
      "determinism": {
        "passed": false, "firstDivergentTick": 813,
        "runA": { "threads": 1, "hash": "0x1A…" }, "runB": { "threads": 8, "hash": "0x7C…" },
        "firstDivergentSystem": "EnemyAI",
        "diff": [ { "entity": "entity_01j5xqd6…", "path": "/components/Transform/position/1", "a": "0x3F8CCCCD", "b": "0x3F8CCCCE" } ]
      },
      "artifacts": [
        { "kind": "snapshot",   "path": "artifacts/GoblinBasicCombat/tick_0813.snapshot.json", "tick": 813 },
        { "kind": "snapshot",   "path": "artifacts/GoblinBasicCombat/tick_0813_b.snapshot.json", "tick": 813 },
        { "kind": "screenshot", "path": "artifacts/GoblinBasicCombat/tick_0813.png", "tick": 813 },
        { "kind": "replay",     "path": "artifacts/GoblinBasicCombat/run.replay" },
        { "kind": "log",        "path": "artifacts/GoblinBasicCombat/log.jsonl" },
        { "kind": "trace",      "path": "artifacts/GoblinBasicCombat/trace.json" }
      ]
    }
  ]
}
```

AI는 snapshot을 읽고 원인을 분석할 수 있다. 결정성 실패는 "hash가 다르다"가 아니라 **"몇 tick, 어느 entity의 어느 property가 어떻게 다른가"** 를 준다.

`game test --junit results.xml`: testsuite = 디렉터리, testcase{classname=`Tests.Combat`, name, file, time}, `<failure message="goblin-dies @ tick 813">`에 bindings, artifact마다 `<system-out>[[ATTACHMENT|Tests/.results/…/tick_0813.png]]</system-out>`. Exit code는 §13 표를 따른다 (0 pass / 3 failures). gdUnit4의 0/100/101 관례처럼 CI가 분기할 수 있게.

---

# 25. State Dump

```bash
game dump entity_01j5xqd6…
```

```json
{
  "id": "entity_01j5xqd6…",
  "name": "Goblin_01",
  "path": "TestArena/Encounter_05/Goblin_01",

  "components": {
    "EnemyAI": {
      "state": "Chasing",
      "target": "entity_01j5…player"
    },

    "Health": {
      "max": 100,
      "current": 24
    }
  }
}
```

---

# 26. Frame Snapshot

특정 tick 상태를 저장한다.

```bash
game snapshot --tick 813
```

(▶ v2: `snapshot:N`의 N은 tick이다. `--frame`은 `--tick`의 alias.)

이 snapshot으로:

```bash
game inspect snapshot:813 entity_01j5xqd6…
```

할 수 있다.

AI가 “그 순간 무슨 일이 있었는가”를 분석하기 쉬워진다.

## 26.1 ▶ v2: Snapshot 포맷

§23 assertion과 §24 artifact가 모두 이 문서에 의존하므로 고정한다. Factorio의 desync 사례(저장되지 않은 캐시값이 reload 후 다르게 재계산)처럼 **sim에 영향을 주는 모든 상태**가 들어가야 replay/복원이 동일해진다.

```json
{
  "schemaVersion": 1,
  "run": "run_01j5…", "tick": 813, "simTime": 13.55,
  "seed": 381251, "tickRate": 60,
  "build": { "gitHash": "…", "fpFlagsHash": "…", "schemaVersion": 1 },
  "worldHash": "0x…",
  "systemHashes": { "Physics": "0x…", "EnemyAI": "0x…", "Rng": "0x…" },
  "rng": { "EnemyAI": ["0x…","0x…","0x…","0x…"], "Spawner": ["…"] },
  "physics": { "engine": "box2d-3.1.1",
               "bodies": [ { "entity": "entity_01j5xqd6…", "p": [5.0, 0.0], "q": 0.0, "v": [0,0], "w": 0, "awake": true } ] },
  "entities": [
    { "id": "entity_01j5xqd6…", "name": "Goblin_01", "prefab": "prefab_01j5…",
      "components": { "Transform": { "…": "…" }, "Health": { "max": 100, "current": 24 },
                      "EnemyAI": { "state": "Chasing", "target": "entity_01j5…player" } } }
  ],
  "bindings": { "player": "entity_01j5…", "goblin": "entity_01j5xqd6…" },
  "events": [ "… 마지막 N개 trace event (§64) …" ]
}
```

규칙:
1. float는 JSON에서 shortest-round-trip 문자열, hash에는 bit pattern.
2. 모든 배열은 persistentId 순 정렬 → 두 snapshot을 `game snapshot diff a b`(RFC 6902 출력, §51 기계 재사용)로 비교 가능.
3. Box2D는 SaveState가 없으므로 snapshot에서의 “복원”은 **world 재생성**(body를 같은 순서로 재생성)으로 정의한다. Jolt를 쓰면 `PhysicsSystem::SaveState` blob을 `physics.blob`으로 추가한다.
4. Flecs를 채택하면 `game snapshot` = `ecs_world_to_json`, `game inspect snapshot:N` = scratch world에 `ecs_world_from_json` 로드 후 같은 serializer. **주의**: Flecs v4는 snapshot addon을 제거했고 `ecs_entity_from_json`은 현재 `ids`와 `values`만 복원한다. inherited 값, DontFragment component, pair with data, doc/uuid 메타데이터의 round-trip fidelity를 **Phase 1 테스트로 확인**한다.
5. 선택(query)은 JSONPath: `game inspect snapshot:813 --jsonpath '$.entities[?(@.components.EnemyAI.state=="Chasing")].id'`. 목표 문법은 RFC 9535; 구현 후보 jsoncons는 Goessner 방언이므로 Phase 5에서 jsonpath-compliance-test-suite로 확인하고 미달이면 지원 부분집합을 명시한다. assertion은 §23.1의 문법.
6. **세 가지 직렬화 소비자**(§88.8에 정의): authoring / snapshot / save. reflection 기반 serializer 하나에 `PropFlags` 기반 visibility mask 셋이다.

---

# 27. Screenshot / Capture

AI에게 게임의 시각 결과도 제공해야 한다.

```bash
game capture frame.png
```

또는:

```bash
game capture \
    --camera Gameplay \
    --width 1024 \
    --height 1024
```

이를 통해 AI가:

```text
코드 수정
→ 실행
→ screenshot
→ vision 검사
```

루프를 만들 수 있다.

▶ v2: 이 루프는 부가 기능이 아니라 **성공률을 좌우하는 1급 기능**이다. GameDevBench(ICML 2026)에서 visual feedback만으로 GPT-5.4의 성공률이 41.1% → 52.0%, GameCraft-Bench에서 screenshot 검사 횟수가 많은 agent가 디버깅 성과가 좋았고(tool call 총량과 품질의 상관은 r≈0.016), Play2Code(코딩 agent + 플레이하는 GUI agent)는 agentic coding 단독 대비 +14.6p. `game capture`와 `game test`의 snapshot은 **Phase 5**에 들어간다.

## 27.1 ▶ v2: Vision 검사용 capture vs Golden-image 회귀 테스트

두 용도를 분리한다. (a) **vision 검사**는 위 내용 그대로 — AI가 이미지를 보고 판단한다. (b) **golden-image 회귀 테스트**는 명시적 tolerance 모델 없이는 flaky하고, AI가 "버그가 아닌 것을 고치게" 된다.

```text
Tests/Golden/<test>/<platform>_<backend>_<WxH>.png     (Unreal 식 Platform_RHI_ShaderModel bucketing. 대안 골든 여러 장 허용)

테스트 스텝:
{ "capture": { "camera": "Gameplay", "width": 512, "height": 512, "golden": "combat_end",
               "tolerance": { "perPixel": 0.1, "maxMismatchRatio": 0.002, "ignoreAA": true,
                              "maxLocalError": { "window": 16, "ratio": 0.05 } } } }

비교 알고리즘 = pixelmatch 식 perceptual per-pixel threshold(기본 0.1) + AA-pixel 제외 + mismatch ratio
               (+ 선택: Unity ImageAssert 식 average-error 상한)
출력 artifact: expected.png, actual.png, diff.png, { mismatchedPixels, ratio, maxLocalRatio }
CLI: game capture --compare golden.png --json
```

결정성 규칙: 고정 해상도, TAA/temporal effect 없음, MSAA 없음 또는 고정 sample 수, 파티클 RNG는 seeded, capture 전 pre-roll tick(Bevy headless_renderer는 40), 그리고 **CI에서는 software rasterizer** — Vulkan은 SwiftShader/lavapipe, Windows D3D12는 WARP(Windows 10 1709+ 내장, `IDXGIFactory4::EnumWarpAdapter`로 선택; 최신 WARP는 NuGet `Microsoft.Direct3D.WARP`로 테스트 가능, 재배포 불가) — 단 SDL_GPU에서의 선택 경로는 §20 참조. 골든은 같은 rasterizer로 생성하며 개발자 GPU로 만들지 않는다. Unreal이 기본 tolerance를 Low로 둔 이유가 "TAA 때문에 모든 픽셀이 매번 조금씩 다르다"는 것이다.

---

# 28. Structured Logging

로그 자체에도 schema가 필요하다.

▶ v2: 자체 schema를 만들지 않고 **OpenTelemetry Logs Data Model(Stable)** 의 필드를 그대로 쓴다. SDK(opentelemetry-cpp: absl/protobuf/gRPC)는 가져오지 않는다 — 모델만 채택해 **JSON Lines**(한 줄에 한 record, JSON 배열 아님)로 쓴다. 나중에 어떤 OTel collector로도 변환 없이 보낼 수 있다.

```json
{"ts":1787306400123456789,"sev":13,"level":"WARN",
 "event":"nav.target_invalid",
 "body":"Target entity no longer exists.",
 "scope":"Navigation",
 "attrs":{"game.tick":813,"game.sim_time":13.55,"game.world":"world_01j5…",
          "game.entity":"entity_01j5xqd6…","game.run_id":"run_01j5…","nav.target":"entity_01j5…"},
 "trace_id":"4bf92f3577b34da6a3ce929d0e0e4736","span_id":"00f067aa0ba902b7"}
```

| 필드 | OTel 대응 | 규칙 |
|---|---|---|
| `ts` | Timestamp | uint64 ns since epoch. **sim 로그의 순서 기준은 `attrs.game.tick`** (벽시계가 아니라) |
| `sev` / `level` | SeverityNumber / SeverityText | TRACE 1–4, DEBUG 5–8, INFO 9–12, WARN 13–16, ERROR 17–20, FATAL 21–24 |
| `event` | EventName | 안정적인 기계 코드 (초안의 `code`를 대체). `<scope>.<snake_case>` |
| `body` | Body | 사람용 문장 |
| `scope` | InstrumentationScope | 초안의 `system` |
| `attrs` | Attributes | 게임 전용 키는 전부 `game.` 네임스페이스 아래 |
| `trace_id` / `span_id` | TraceId / SpanId | 활성 span이 있을 때만 |

라이브러리: Quill v12의 `JsonFileSink`는 named args를 JSON key로 네이티브 출력하지만 기본 키 이름(`timestamp`/`log_level`/`message`…)이 OTel 이름과 다르므로 `JsonFileSink`를 상속해 `generate_json_message()`를 override한다. spdlog는 JSON sink가 없고 `set_pattern`의 `%v`가 escape되지 않으므로 쓰려면 custom formatter로 JSON을 직접 만들어야 한다.

`game log query --event nav.* --entity entity_01j5xqd6… --tick 800..820` 으로 JSONL을 질의한다. **MCP Logging(`notifications/message`)에는 싣지 않는다** — 2026-07-28에서 deprecated (§1).

단순 문자열:

```text
LogTemp: Warning...
```

보다 AI 분석 비용이 훨씬 낮다.

---

# 29. Validation System

게임 실행 전 데이터만으로 찾을 수 있는 문제는 미리 잡는다.

```bash
game validate
```

검사 예:

- dangling reference
- 존재하지 않는 asset
- 잘못된 property type
- 범위를 벗어난 값
- 필수 component 누락
- component dependency 누락
- duplicate persistent ID
- invalid prefab inheritance
- circular reference
- invalid world entity
- missing animation
- missing collision layer
- 잘못된 input action

결과:

```text
Validation

ERROR   2
WARNING 5
INFO    3
```

AI용:

```bash
game validate --json
```

▶ v2: `game validate`와 `game lint`(§62)는 **SARIF 2.1.0의 rule/result 분리**를 따르는 rule registry다.

```json
{ "id": "REF_DANGLING", "name": "DanglingReference",
  "shortDescription": { "text": "Reference target does not exist" },
  "defaultConfiguration": { "level": "error" },
  "help": { "text": "…" },
  "fixable": "MachineApplicable" }
```

- `game rules --json`으로 규칙 목록을 조회한다 (§15 discovery와 같은 원리).
- 출력은 §12 envelope이다. error가 없으면 `ok:true`, `result: { diagnostics: [ …§79 Diagnostic… ], summary: { error:0, warning:5, note:3 } }`; error가 하나라도 있으면 `ok:false`, `error: { ruleId: "VALIDATION_FAILED", category: "validation", details: { summary, diagnostics: [ … ] } }`. 사람용 출력의 INFO는 NOTE로 쓴다(§79 Severity).
- `--format sarif`는 진짜 SARIF 2.1.0을 낸다 → GitHub code scanning / VS Code SARIF viewer가 공짜로 붙는다.
- `game validate --fix`는 `MachineApplicable` fix만 **하나의 Transaction 안에서** 적용하고 재검증, 새 오류가 생기면 rollback. (ESLint `--fix`처럼 자동 적용 가능한 fix만 적용한다는 점만 같다 — ESLint는 rollback하지 않는다.) `--fix=maybe`로 `MaybeIncorrect`까지. 내부적으로는 `apply`(Mutation)로 dispatch된다.
- `--baseline .game/lint-baseline.json`은 fingerprint로 기존 finding을 억제한다 (SARIF §3.27.16–17).
- Flecs를 채택하면 runtime 측 규칙은 **alerts addon**(query + message template + severity → per-entity alert instance, Explorer에 표시)으로 구현하고, `game validate --json`이 `EcsAlertInstance`를 §79 Diagnostic으로 감싼다. `EcsMemberRanges`의 warning/error 2단은 §29의 WARNING/ERROR와 1:1이다. 파일 수준 검사(중복 id, 파일 없음, schemaVersion)는 world 밖이므로 자체 validator에 남는다.

리서치에서 드러난 **추가 검사 항목** (일부는 Godot/Unity 이슈 트래커의 실제 실패 모드 — 중복 ID, cache key 입력 누락 — 나머지는 §5/§7/§34/§53 규약에서 파생):

```text
DUPLICATE_PERSISTENT_ID            (§7.3, 해결 명령 포함)
ID_FORMAT_INVALID                  (TypeID grammar)
JSON_NOT_CANONICAL                 (§5.3, fix = game fmt)
PREFAB_OVERRIDE_TARGET_MISSING     (§34: set 경로가 resolved base에 없음)
PREFAB_OVERRIDE_TYPE_MISMATCH
PREFAB_CHAIN_TOO_DEEP / PREFAB_CHAIN_CYCLE
CACHE_KEY_INPUT_MISSING            (§37: importerVersion 없는 importer)
SCHEMA_VERSION_NEWER_THAN_ENGINE   (§53: 최신 버전이 쓴 파일을 구버전이 열 때 — best-effort 로드 금지)
SUBASSET_NOT_FOUND                 (§19: asset_…#name 이 sidecar 목록에 없음)
```

---

# 30. Editor 구조

Editor가 있어야 한다.

AI-native라고 GUI를 없애는 것은 좋은 방향이 아니다.

사람은 공간 / 애니메이션 / 비주얼을 GUI로 보는 것이 훨씬 빠른 경우가 많다.

따라서 Editor는 유지한다.

단:

> Editor는 엔진이 아니다.

---

# 31. 최소 Editor

초기:

```text
┌────────────────────────────────────┐
│ Menu                               │
├──────────┬──────────────┬──────────┤
│Hierarchy │              │Inspector │
│          │   Viewport   │          │
│          │              │          │
├──────────┴──────────────┴──────────┤
│ Asset Browser                      │
├────────────────────────────────────┤
│ Console                            │
└────────────────────────────────────┘
```

딱 이 정도면 된다.

▶ v2: Flecs를 채택하면 Editor를 두 단계로 나눌 수 있다.

```text
Phase 6a — zero-code
  flecs::Rest + flecs::stats 를 켜고 Flecs Explorer(MIT, 브라우저, 127.0.0.1:27750)를 쓴다.
  → Hierarchy / Inspector(drag-to-edit) / Query console / Script editor / pipeline stats 가 바로 생긴다.
  (v4.1.1 autocomplete·drag-to-change, v4.1.6 다중 inspector·entity 생성·component 추가 dialog)

Phase 6b — ImGui viewport만
  공간 편집(gizmo, 배치)만 ImGui로 만들고 Command layer를 호출한다.
```

**단, Explorer의 편집은 REST PUT으로 runtime world를 직접 건드리며 Command/Undo layer를 우회한다.** 이것이 §32와 충돌하는 유일한 지점이다. 규칙: Explorer/REST는 **play world(§88.2)의 read-mostly 디버깅**에만 쓴다. authoring 변경이 필요하면 custom http handler가 CommandBus를 감싸거나, Explorer 편집을 "remote history"로 기록한 뒤 `game promote`로 승격한다.

---

# 32. Editor도 Command API를 사용한다

예를 들어 Inspector에서:

```text
Movement.speed

4.0 → 5.0
```

으로 수정했다면 Editor 내부에서 직접 데이터를 수정하지 않는다.

```cpp
SetPropertyCommand command {
    Entity = SelectedEntity,
    Component = "Movement",
    Path = "/speed",          // ▶ v2: JSON Pointer (component 내부 경로)
    Value = 5.0
};

CommandBus.Execute(command);
```

를 호출한다.

이렇게 해야:

```text
CLI
MCP
Editor
Test
```

가 모두 같은 로직을 공유한다.

---

# 33. Editor 상태와 Project 상태 분리

Editor-only 상태:

```text
선택된 Entity
Viewport Camera
열려 있는 Panel
Grid 표시 여부
Gizmo mode
```

Project 상태:

```text
Entity position
Component
Prefab
World
Asset
Gameplay Data
```

절대로 섞지 않는다.

예:

```text
.editor/
```

에 Editor 개인 설정을 보관할 수 있다.

---

# 34. Prefab

Prefab은 텍스트로 명확히 표현되어야 한다.

▶ v2: 초안의 flat `"override": {"Health.Max": 300}` 맵은 **component 제거, component/entity 추가, 배열 편집을 표현할 수 없다.** Unity(`m_Modifications` + `m_RemovedComponents`/`m_AddedComponents`/…)와 O3DE(RFC 6902 JSON Patch)는 명시적 add/remove가 필요했고, Bevy BSN도 layered field patch로 같은 방향이다. dotted path는 키에 `.`이 들어가면 깨진다. **RFC 6902 부분집합 + JSON Pointer**로 교체한다.

```json
{
  "$schema": "game://schema/prefab/1",
  "schemaVersion": 1,
  "id": "prefab_01j5xq8z3mf0n9k2c7p4rtvw7a",
  "name": "GoblinElite",
  "base": "prefab_01j5xq8z3mf0n9k2c7p4rtvw6y",

  "set":    { "/components/Health/max": 300,
              "/components/Movement/speed": 5.0,
              "/components/SpriteRenderer/sprite": "asset_01j5xq9a7c3d2e1f0g9h8j7k6m#sprites/goblin_elite" },
  "add":    { "/components/Shield": { "amount": 50 } },
  "remove": [ "/components/EnemyAI" ]
}
```

이 구조는 AI에게 매우 쉽다. 규칙:

1. 경로는 **resolved base에 대한 JSON Pointer**(RFC 6901; `~`→`~0`, `/`→`~1`). dotted path는 CLI 입력 sugar로만.
2. **absent = inherit.** `set`에 없는 경로는 항상 base를 따른다. base를 나중에 바꾸면 override하지 않은 모든 필드에 흘러간다 (Unreal CDO delta 직렬화 의미론).
3. "base 값으로 되돌림"은 **키 삭제**로만 표현한다. Editor는 base와 같은 값을 override로 기록하지 않는다. Godot은 기본값만 생략하는 방식이라 "unset"과 "override to same value"를 구분 못 해 확인된 core 버그(#94912: base 수정 시 자식에 암묵 override 생성)가 있다.
4. `set`은 경로가 resolved base에 **존재해야** 한다 (아니면 `PREFAB_OVERRIDE_TARGET_MISSING`, fix = `add`). `add`는 존재하지 않아야 한다.
5. 배열은 기본적으로 **통째로 교체**한다. index 수준 op(`/points/1`)는 고정 길이 수학 배열(vec/quat/color)에만 허용 — O3DE의 per-index patch가 알려진 취약점이다. gameplay 배열(patrol point, loot table)에 부분 override가 필요하면 요소에 `key`를 두고 `/points/{key}`로 주소지정하는 keyed array를 **그때** 설계한다 (현재 비목표).
6. base에서 `remove`한 component를 손자가 다시 `add`하는 것은 합법이며 체인 순서로 해석한다. 체인 깊이 상한 + cycle 검사는 `game validate`.
7. world의 entity 인스턴스도 **같은 세 키**(`prefab` + `set/add/remove`)를 쓴다 (§6). 그래서 §18 Explain이 `Source: prefab → overrides`를 같은 코드로 보여준다.
8. `game prefab flatten <id>`는 resolve 결과를 독립 prefab으로 만든다 (AI가 "복사본"을 원할 때).
9. 3단계 체인(base → elite → instance) 테스트를 §23 시나리오에 포함한다.

Runtime(Flecs)은 이 의미론을 그대로 구현할 필요가 없다 — authoring 단계에서 resolve/flatten한 뒤 인스턴스화하면 된다. Flecs v4 prefab의 기본이 Override(복사)인 것과 충돌하지 않는다.

---

# 35. Prefab Diff

```bash
game prefab diff name:Goblin name:GoblinElite
```

```text
/components/Health/max
  100 → 300

/components/Movement/speed
  4.0 → 5.0

/components/SpriteRenderer/sprite
  asset_…#goblin → asset_…#goblin_elite

/components/Shield
  (absent) → { "amount": 50 }

/components/EnemyAI
  { … } → (removed)
```

AI가 두 asset의 차이를 이해하기 쉽다.

▶ v2: `--json`은 §78 ChangeSet op 형식(RFC 6902 `op/path/value` + `before`)을 낸다 — §51의 diff, §50 dry-run과 **같은 모양**이다. 구현은 `nlohmann::json::diff(resolve(A), resolve(B))` (선형 시간, add/remove/replace만 생성 — `move` 의도는 잃으므로 **검증 오라클로만** 쓰고, 의도 보존이 필요한 ChangeSet은 command가 직접 emit한다. §78).

---

# 36. Asset Pipeline

Source Asset과 Runtime Asset을 분리한다.

```text
Assets/
    goblin.png

Cache/
    goblin.texturebin
```

Source:

```text
git 관리
AI 참조
사람 편집
```

Cache:

```text
빠른 Runtime Load
재생성 가능
git 제외 가능
```

---

# 37. Asset Metadata

예 (▶ v2: Godot `.import`의 `[remap]/[deps]/[params]` 구조와 Unity AssetDatabase v2의 cache key 정의를 합친 형태로 교체):

```text
Assets/Textures/goblin.png               (source, 커밋)
Assets/Textures/goblin.png.meta.json     (sidecar, 커밋 — ID와 설정의 소유자)
Cache/texture/<key>.bin                  (파생, 커밋 안 함, 삭제 가능)
Cache/manifest.json                      (파생 index: id → key, source mtime/size fast-path)
```

```json
{
  "$schema": "game://schema/asset-meta/1",
  "schemaVersion": 1,
  "id": "asset_01j5xq9a7c3d2e1f0g9h8j7k6m",
  "source": "Assets/Textures/goblin.png",
  "importer": "Texture2D",
  "importerVersion": 3,
  "settings": { "filter": "nearest", "mipmaps": false, "compression": "none" },
  "subAssets": [
    { "name": "goblin_idle",  "kind": "sprite", "rect": [0, 0, 32, 32] },
    { "name": "goblin_elite", "kind": "sprite", "rect": [32, 0, 32, 32] }
  ]
}
```

Editor의 Import Settings를 AI가 클릭할 필요가 없다.

**Cache key** (이것이 "Cache/는 삭제해도 된다"를 희망이 아니라 성질로 만든다):

```text
key = sha256( sourceBytesHash
            || importerName || importerVersion || engineDataFormatVersion
            || JCS(settings)           // RFC 8785 canonical JSON
            || targetProfile )         // 예: "win-x64-d3d12"
```

규칙:
- sidecar는 **설정**을 저장하지 해시를 저장하지 않는다. Godot이 커밋되는 `.import`에 source md5를 넣어 VCS churn 불만(#17103)을 겪었다. 해시는 `Cache/manifest.json`에 (Unity가 Library/에 두듯).
- staleness 검사 = mtime+size fast path → 불일치 시 full hash. (AI 도구는 내용이 같은 파일을 자주 다시 쓴다.)
- importer는 **결정적**이어야 한다 (같은 입력 + 의존성 → 같은 바이트; Unity의 정의 그대로) 그리고 `importerVersion`을 선언해야 한다 — 올리면 그 importer의 출력이 전부 무효화된다. 선언 없는 importer는 `CACHE_KEY_INPUT_MISSING`.
- `subAssets[]`는 **이름이 안정 키**다 (§19). 재import 시 index가 아니라 이름으로 매칭한다.
- Unreal DDC("disposable, 언제든 재생성"), Bazel(action digest = inputs Merkle tree + command + env) 모두 같은 입력 집합을 키에 넣는다.
- §54의 "JSON → Binary Cache"(world/prefab cook)도 **같은 키 규칙**을 쓴다: JSON의 JCS hash + schema/component version. cooked world가 source에서 drift하지 않는다.

---

# 38. Asset Import CLI

```bash
game asset import Assets/Textures/goblin.png \
    --importer Texture2D \
    --filter nearest
```

결과:

```json
{
  "ok": true,
  "command": "asset.import",
  "result": { "id": "asset_01j5xq9a…", "status": "imported", "cacheKey": "sha256:…", "cacheHit": false,
              "subAssets": ["goblin_idle", "goblin_elite"] },
  "changes": [ { "op": "file.add", "doc": "Assets/Textures/goblin.png", "blob": "sha256:…" },
               { "op": "add", "doc": "Assets/Textures/goblin.png.meta.json", "path": "", "value": { "…": "…" } } ],
  "warnings": [], "meta": { "…": "…" }
}
```

▶ v2: `game asset import --all --json`은 CI/headless에서 동작해야 한다 (Godot `--import` 대응). `game cache gc`(manifest에 없는 key 삭제)와 `game cache verify`(key 재계산 — Unity `-consistencyCheck` 대응)를 둔다. Cache/ 산출물은 ChangeSet 대상이 아니다 (재생성 가능).

---

# 39. Hot Reload

두 종류를 구분한다.

## Data Hot Reload

우선 구현.

```text
JSON 수정
 ↓
File Watcher
 ↓
Reload
```

쉬우며 가치가 높다.

▶ v2: **SDL3에는 파일 감시 API가 없다** (Filesystem category: CopyFile / EnumerateDirectory / GlobDirectory … 뿐). 구체 설계:

```text
efsw (MIT, ReadDirectoryChangesW / inotify / FSEvents / kqueue, 2026-08 활성)
  → Worlds/, Prefabs/, Data/, Config/ 에 recursive watch
  → 이벤트를 메인 스레드 큐로
  → debounce 100–250ms (Editor·AI가 파일을 여러 번 쓴다)
  → 변경 파일 schema validate (§29)
  → 성공: Command layer의 project.reload_document command로 적용 → ChangeSet 남김 (actor=system:watcher), undo 가능
  → 실패: structured diagnostic만 내고 이전 상태 유지
headless/CI: watcher 대신 `game reload <path>` + std::filesystem::last_write_time 폴링(1s)
```

**핵심 규칙: hot reload도 Command를 통해 들어온다.** 숨은 mutation 경로가 있으면 §84-5/6이 깨진다. 외부 편집으로 바뀐 파일의 `base` hash가 달라지므로 진행 중이던 AI transaction은 commit 시 `BASE_MISMATCH`를 받는다 (§9.2).

## Code Hot Reload

나중.

C++ hot reload는 복잡도가 높다.

초기에는:

```text
Compile
 ↓
Process restart
 ↓
State reload
```

도 충분하다.

AI는 사람이 아니므로 재시작 비용에 덜 민감하다.

오히려 복잡한 C++ hot reload 시스템을 만들지 않는 것이 유지보수에 유리할 수 있다.

▶ v2: 결론은 유지하되 근거를 "복잡도"에서 **비용**으로 바꾼다. 2026-08 선택지:

| 선택지 | 비용 | 비고 |
|---|---|---|
| (a) 재시작 | 0 | 기본값. snapshot(§26) 복원으로 "state reload" |
| (b) cr.h (MIT, single header, 2026-06 활성) | Game/를 DLL로 빌드 | `CR_STATE` 태깅 상태 보존 + crash 시 이전 버전 자동 롤백. static 상태는 태깅한 것만 보존, Game/ 경계를 C-ABI에 가깝게 |
| (c) Live++ (상용, 개인 €119/년, Windows 10 x64 PC — 콘솔은 별도 라이선스, 30일 체험) | 돈 + Windows 한정 | Unreal Live Coding의 기반. 한계 그대로: .cpp 생성자 기본값 미반영, 구조 변경 시 불안정. 사람 개발자용 가치는 크지만 AI 루프에 필수 아님 |

**결정**: Phase 0–5는 (a). AI 루프 1회(수정 → 빌드 → headless run → 결과)가 **측정상 60초를 넘으면** (b)를 Game/ DLL에 한정해 도입한다. 그 전에 확인할 것: DLL 경계에서 reflection registry(static initializer)와 EnTT meta context / Flecs world가 어떻게 공유되는지 — 어떤 리서치도 이 상호작용을 검증하지 않았다 (§88.5).

빌드 시간 자체는 Game/를 별도 타깃으로, PCH, unity build, ccache/sccache, 그리고 §59의 좁은 헤더(Engine 내부 헤더를 Game/에 노출하지 않음)로 줄인다. `game build --json`의 설계는 §88.5.

---

# 40. Runtime / Editor / Tool 실행 파일 분리

권장:

```text
GameRuntime.exe
GameEditor.exe
GameCLI.exe
```

또는 초기에:

```text
Game.exe --editor
Game.exe --headless
Game.exe --command ...
```

로 시작해도 된다.

중요한 것은 코드 레이어 분리다.

---

# 41. Build 구조

디렉터리 구조는 §75의 트리가 유일한 정본이다 (▶ v2: 초안의 §41 트리는 §75와 어긋나 제거).

의존 방향:

```text
Core
 ↑
Runtime
 ↑
Game

Command → Runtime
Editor → Command
CLI    → Command
MCP    → Command
```

Editor에서 Runtime을 직접 마구 건드리지 않는다.

▶ v2: FP 플래그는 **한 곳**에서 정의한다 (§22.2). Box2D CMake가 정확히 이 방식(모든 GCC/Clang에 `-ffp-contract=off` 강제)이고, Jolt 문서도 애플리케이션 측에 `/fp:precise` + `-ffp-contract=off`를 요구한다. TU마다 플래그가 다르면 같은 소스라도 결과가 달라진다.

```cmake
add_library(det_fp_flags INTERFACE)
if (MSVC)
  target_compile_options(det_fp_flags INTERFACE /fp:precise)            # VS2022+: contraction off by default
else()
  target_compile_options(det_fp_flags INTERFACE -ffp-contract=off -fno-fast-math)
endif()
# Engine/Runtime, Engine/Physics, Game/*, ThirdParty/box2d|Jolt 전부 link
# Jolt: set(CROSS_PLATFORM_DETERMINISTIC ON) 는 T2가 필요할 때만
```

`game project info --json`은 `fpFlagsHash`(적용된 플래그 문자열의 hash)를 노출해 replay header(§22.3)와 비교할 수 있게 한다.

기타 빌드 결정: C++ 표준 floor는 **C++20**(EnTT v4 요구) — glaze를 쓰면 C++23. CMake ≥ 3.28(EnTT), 4.x 사용 시 구형 3rd-party에 `CMAKE_POLICY_VERSION_MINIMUM=3.5`. 의존성은 CPM.cmake(버전 + SHA 고정) 또는 vcpkg manifest **중 하나만**. 컴파일러는 §88.5.

---

# 42. Reflection / Property System

AI가 component property를 generic하게 조작하려면 reflection이 필요하다.

예:

```cpp
// 초안 스케치 — 확정 형태는 §42.2 (REFLECT_COMPONENT / PROP / PropertyMeta)
REFLECT_COMPONENT(Health)
{
    PROPERTY(max)
        .Min(1.0f)
        .Default(100.0f);

    PROPERTY(current)
        .RuntimeOnly();
}
```

이 정보로:

- Inspector
- JSON serialization
- schema generation
- validation
- MCP schema
- CLI help

를 모두 생성할 수 있다.

이 부분은 높은 투자 가치가 있다.

## 42.1 ▶ v2: 2026-08 기준 선택지

초안은 매크로 설계를 유일한 길처럼 보여줬고 C++26 reflection을 언급하지 않았다. 현황:

| 접근 | 상태 (2026-08) | 속성 (min/max/default/runtimeOnly) | 빌드 복잡도 | 판단 |
|---|---|---|---|---|
| **C++26 static reflection** (P2996 + P3394 annotations) | 표준 확정 (Sofia 2025-06 채택, London 2026-03-28 기술 작업 완료). **GCC 16.1/16.2만** `-std=c++26 -freflection`으로 지원, `<meta>`는 partial. Clang upstream: No (Bloomberg fork는 "DO NOT use … for production"). **MSVC: 미지원, ETA 없음** | `[[=Range{1,100}]] float max;` — 이상적 | MSVC/Windows에서 불가 | Phase 1 불가. **최종 목표 backend로 예약** |
| **매크로 registry + EnTT meta / Flecs meta** (runtime) | EnTT v4.0.0 (2026-07, C++20) / Flecs v4.1.6 (2026-06) | EnTT: `traits` 16-bit flag + `custom<T>` payload **1개**(반복 호출은 덮어씀) / Flecs: `ecs_member_t`에 range·warning_range·error_range·unit 내장 | 없음 (이미 의존) | **Phase 1 채택** |
| glaze (compile-time, aggregate) | v8.1.0 (2026-08-18, C++23, MIT). MSVC 14.50 OK | `glz::schema{minimum, maximum, defaultValue, description, readOnly, deprecated, enumeration, ExtUnits, ExtAdvanced}` | header-only, `/Zc:preprocessor` | JSON + JSON Schema 생성기 후보. aggregate ≤128 members |
| reflect-cpp (compile-time) | v0.25.0 (2026-05-16, C++20, MIT) | `rfl::Validator<Minimum, Maximum>`, `rfl::Description`, `rfl::json::to_schema`, **`rfl::cli::read`** | header/cmake | Config / CLI 인자 파싱 후보 |
| RTTR / refl-cpp | 마지막 commit 2021-08 / 2022-11 | 있음 | 낮음 | **신규 의존 금지** (사실상 unmaintained) |
| libclang codegen (Refureku/Kodgen, UHT 모방) | 2024-09 / 2023-09 이후 정체 | 자유로움 | libclang 배포 필요, 빌드 단계 추가 | 1인 프로젝트 비권장 |
| Boost.Describe / PFR | 활발 (BSL-1.0) | **없음** (name + pointer만) | 낮음 | 속성 요구 미충족 |
| magic_enum | v0.9.8 (2026-05-03, C++17, MIT) | enum 전용 | header-only | enum 이름/값용 채택 (C++26 `enumerators_of`가 모든 컴파일러에 올 때까지) |

## 42.2 ▶ v2: Phase 1 설계 — 단일 진실 원천은 런타임 `PropertyMeta` 테이블

**소비자(Serializer, Inspector, Schema, CLI, MCP, Validation)는 `PropertyMeta` 테이블만 본다. 테이블을 채우는 front-end는 교체 가능하다.** 이것이 매크로 접근을 "버릴 코드"가 아니게 만든다.

```cpp
enum class PropFlags : uint16_t {
    None = 0, RuntimeOnly = 1<<0, ReadOnly = 1<<1, Hidden = 1<<2,
    Advanced = 1<<3, Transient = 1<<4, Required = 1<<5, Save = 1<<6, Ref = 1<<7
};

struct PropertyMeta {
    std::string_view name;                      // JSON key (camelCase)
    std::string_view description;               // tooltip / schema description / CLI help
    Variant          defaultValue;              // JSON Schema 'default'
    std::optional<double> minimum, maximum, multipleOf;   // hard clamp → validation ERROR
    std::optional<double> warnMin, warnMax;               // soft range → validation WARNING (Flecs warning_range)
    std::optional<double> uiMin, uiMax, step;             // slider only (Inspector)
    std::string_view unit;                      // "m", "m/s", "deg", "hp"
    std::string_view category;                  // Inspector group
    std::string_view refType;                   // Ref 일 때: "entity" | "prefab" | "asset:texture" …
    std::span<const std::string_view> enumOptions;        // magic_enum
    PropFlags flags;
};

// 등록 (한 곳)
REFLECT_COMPONENT(Health, "Hit points and death state")
    REQUIRES(Transform)
    PROP(max,     "Maximum hit points").Min(1).Default(100).Unit("hp").Ui(1, 1000, 1)
    PROP(current, "Current hit points").RuntimeOnly().ReadOnly()
END_REFLECT()
```

구현: 매크로는 (a) `static const std::array<PropertyMeta, N>` 테이블을 만들고, (b) 같은 자리에서 runtime registry에 등록한다 — EnTT면 `entt::meta_factory<Health>{}.type("Health"_hs).data<&Health::max>("max"_hs).custom<const PropertyMeta*>(&table[0]).traits(flags)`, Flecs면 `.member<float>("max")` + `EcsMemberRanges` + doc brief. EnTT는 element당 `custom` payload가 **하나**뿐이므로 모든 속성을 `PropertyMeta` 하나로 묶는다.

**Backend 교체 계획**:

```text
2026 (Phase 1–7):   매크로 REFLECT_COMPONENT (MSVC / Clang / GCC 모두)
MSVC가 P2996 + P3394를 출시하면:
                    struct Health { [[=Range{1,100}]] [[=Default{100}]] float max; [[=RuntimeOnly]] float current; };
                    를 consteval로 순회해 (std::meta::nonstatic_data_members_of, annotations_of_with_type)
                    동일한 PropertyMeta 테이블 생성. 소비자 코드 변경 없음.
libclang codegen은 중간 단계로 두지 않는다 (도구 유지비 > 이득).
검증용으로 WSL + GCC 16.2 에서 annotation front-end 실험 branch는 허용.
review trigger: MSVC가 __cpp_impl_reflection 을 정의하면 §42 재검토 (날짜가 아니라 조건).
```

**컴포넌트 작성 규칙** (§59에 추가): 가능한 한 **aggregate struct**(생성자·virtual·private 멤버 없음, ≤128 멤버)로 유지한다. 그러면 glaze의 무매크로 경로가 열려 있어 serializer 코드를 0줄로 만들 수 있다. 단 glaze/reflect-cpp는 compile-time/type-static이므로 "문자열로 지정된 component 이름 → runtime get/set"(§8 SetProperty, §11 CLI)은 대체하지 못한다 → runtime registry는 여전히 필요하다.

**중첩 경로**: `Transform.position.x`는 `Vec3`를 별도 PropertyMeta를 가진 struct로 reflect하지 않는다. vec2/3/4, quat, color는 **고정된 leaf 타입**으로 두고 CLI/JSON Pointer는 `/position/0`처럼 index로 접근한다 (§14.1 wire format 규칙과 일치).

---

# 43. 하나의 Metadata에서 여러 기능 생성

이상적인 구조:

```text
C++ Component Metadata
      │
      ├── Serialization
      ├── Inspector
      ├── JSON Schema
      ├── CLI Property
      ├── MCP Tool Schema
      ├── Validation
      └── Documentation
```

즉:

```text
Health.max
```

의 정의를 6군데 반복하지 않는다.

## 43.1 ▶ v2: 속성 어휘 표준화

업계는 대략 같은 열 개 남짓의 속성으로 수렴했다. 적어 두면 자체 어휘를 발명하지 않게 되고 AI에게 익숙한 멘탈 모델을 준다.

| 이 문서 | JSON Schema | Flecs `ecs_member_t` | Bevy | Godot | Unity | Unreal |
|---|---|---|---|---|---|---|
| minimum / maximum (hard) | minimum / maximum | error_range | `@RangeInclusive` attr | `@export_range` min,max | `[Min]`, `[Range]` | ClampMin / ClampMax |
| warnMin / warnMax (soft) | `x-warn` | warning_range | — | — | — | — |
| uiMin / uiMax / step | `x-ui` | range | — | `@export_range` step, or_greater | `[Range]` | UIMin / UIMax / Delta |
| default | default | — | ReflectDefault | initializer | initializer | CDO |
| description | description | EcsDocBrief | doc comment | doc comment → tooltip | `[Tooltip]` | ToolTip |
| unit | `x-unit` | unit (units module) | — | `suffix:xxx` | — | Units / ForceUnits |
| runtimeOnly | readOnly + `x-runtimeOnly` | — | `#[reflect(skip_serializing)]` | usage에서 `PROPERTY_USAGE_STORAGE` 제거 | `[NonSerialized]` | Transient |
| hidden / advanced | `x-advanced` | — | `#[reflect(ignore)]` | `@export_storage` | `[HideInInspector]` | AdvancedDisplay |
| enum options | enum | enum entity | Enum TypeInfo | `@export_enum` | enum | UENUM |
| category / group | `x-category` | — | — | `@export_group` | `[Header]` | Category |
| ref type filter | `x-ref` | — | — | `@export_file(filter)`, `@export_node_path(types)` | — | AllowedClasses |

Phase 1에서는 좌측 열 전부를 구현한다. Flecs 식 `warning_range` / `error_range` 2단 분리는 §29 Validation의 WARNING / ERROR와 1:1로 대응되므로 채택한다.

## 43.2 ▶ v2: 기성 라이브러리로 대체 가능한 부분

정직하게: "한 struct → JSON + JSON Schema + validation + CLI"는 이미 **reflect-cpp(v0.25.0)와 glaze(v8.1.0)가 제공**한다. Epic(UFUNCTION metadata → MCP tool), Unity([attribute] 등록 tool), IvanMurzak([AiTool]), soft-ue-cli(argparse → MCP schema)도 "metadata → tool schema"를 한다. **이것은 차별점이 아니라 table stakes다.**

- **JSON Schema 생성기**: 직접 쓰지 말고 glaze `glz::write_json_schema<T>()` 또는 reflect-cpp `rfl::json::to_schema<T>()`를 검토한다. 단 둘 다 type-static이므로 PropertyMeta 테이블의 `x-` 확장은 후처리로 합친다.
- **Config / CLI 인자 파싱**: `project.json`, `Config/*.json`, `game run --seed --frames` 류는 reflect-cpp `rfl::cli::read<Args>`로 struct 하나에서 JSON + CLI 동시 처리.
- **남는 고유 요구**: 문자열 주소 지정 runtime registry(SetProperty / CLI / MCP의 동적 경로). 이것이 만들어야 할 것이다.

---

# 44. AI용 Documentation

AI에게 수백 페이지 매뉴얼을 던지지 않는다.

API 자체가 self-describing 해야 한다.

예:

```bash
game describe component EnemyAI
```

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "game://schema/component/EnemyAI/1",
  "title": "EnemyAI",
  "description": "Controls target selection and combat state.",
  "type": "object",
  "x-requires": ["Transform"],
  "x-lifecycle": { "init": "OnSpawn", "tick": "after Physics", "destroy": "OnDespawn" },
  "properties": {
    "detectionRange": { "type": "number", "default": 10, "x-cpp": "float", "x-unit": "m" }
  },
  "additionalProperties": false
}
```

(▶ v2: `game describe`의 출력은 §14의 schema 그대로다 — 별도 형식이 아니다.)

---

# 45. Docs Index

긴 문서가 필요한 경우:

```bash
game docs search "prefab override"
```

처럼 엔진 문서 자체를 검색할 수 있게 만들 수 있다.

하지만 핵심 데이터 조작은 documentation search 없이 가능해야 한다.

---

# 46. MCP Adapter

MCP 서버는 얇아야 한다.

예 (▶ v2: 정확한 tool 집합은 §47):

```text
query  inspect  explain  refs
apply            ← 모든 mutation은 여기로 (changes[].op = §8 command id)
validate
run  run_status  test  capture
tx  history
project_info  schema_describe  capabilities
```

MCP Tool implementation 내부는:

```cpp
return CommandBus.Execute(...);
```

수준이어야 한다.

MCP 서버 안에 게임 로직을 넣지 않는다.

## 46.1 ▶ v2: 토폴로지 결정 — 공식 SDK로 만든 sidecar

초안은 MCP adapter를 C++ 코드가 `CommandBus.Execute`를 직접 호출하는 것으로 암시했다. 그러나 **Tier-1 C++ MCP SDK는 없고**(TS / Python / Go / C#, Rust beta), 유지보수자는 2026-07-28 breaking revision을 앞두고 "직접 구현은 상당한 uplift"를 경고했다. 더 얇고 안전한 실현:

```text
Command Core 호스트 (C++)  ──  game serve --rpc 127.0.0.1:<port>     (JSON-RPC 2.0 over HTTP, BRP 스타일; §88.1)
                           ──  game <cmd> --json                      (CLI)
                                    ▲
MCP sidecar (TypeScript 또는 Python 공식 SDK, < 300 lines)
  tools/list  = `game capabilities --json` 의 tools[] pass-through (15개, §47)
  tools/call  = RPC 호출(또는 spawn) → envelope 그대로 structuredContent
  resources   = game://schema/commands (전체 command + argsSchema), game://schema/component/*, game://snapshot/*
```

- §48(CLI/MCP 동등성)이 **구조적으로** 성립한다: sidecar에는 schema도 로직도 없다.
- in-process C++ MCP는 규격이 안정된 뒤에만 고려한다.
- Flecs REST를 MCP backend로 직접 쓰지 않는다 (Undo/Transaction 없음). Bevy식 in-engine JSON-RPC는 `game serve`가 그 역할이고, MCP는 그 위의 어댑터다.

## 46.2 ▶ v2: Command API ↔ MCP 2026-07-28 매핑

| Command API 개념 | MCP 매핑 |
|---|---|
| §47 tool (`query`, `apply`, `tx` …) | `tools/call`; name = tool 이름 (`.`→`_`), arguments = request struct. 개별 command는 `apply.changes[].op` |
| CommandResult (ok) | `result.structuredContent` = envelope(§12), `content[0].text` = 같은 JSON, `isError:false` |
| CommandResult (도메인 오류) | **같은 envelope, `isError:true`** (JSON-RPC error 아님) |
| Reflection schema | `inputSchema` / `outputSchema` (JSON Schema 2020-12, §14) |
| 읽기 / 쓰기 / 파괴 구분 | `annotations.readOnlyHint / destructiveHint / idempotentHint` (§15) |
| Transaction / Snapshot / Run 세션 | server-minted opaque handle(`tx_…`, `run_…`, `snapshot_…`)을 인자로. 수명 문서화, "unknown/expired handle" 오류 (§9.1) |
| 장시간 run / test / benchmark | 우선 동기 + `progressToken` / `notifications/progress`. client가 지원하면 Tasks extension(`resultType:"task"`, `tasks/get`). **항상** `run.start` / `run.status(run_id)` handle 쌍을 portable fallback으로 제공 ("Host support varies by client") |
| Cancel | `notifications/cancelled` → Command cancel token |
| 문서 / 스키마 / 스냅샷 읽기 | `resources` (`game://schema/component/Health`, `game://snapshot/813`) + `ttlMs` / `cacheScope:"private"` |
| Recipe (§49) | `prompts` (선택) |
| 구조화 로그 (§28) | stderr / 파일 / OTel. **MCP Logging은 deprecated** |
| 위험 작업 확인 | CLI 호환인 exit-code-4 + `confirmCommand` envelope을 먼저 (§50). client가 확실히 지원할 때만 MRTR elicitation |

**전송·보안**: Streamable HTTP 또는 stdio만 (legacy HTTP+SSE는 Deprecated — 최소 12개월 유예 후 제거 대상). loopback 바인딩 + **프로세스 시작 시 생성되는 per-session token**(환경변수/파일로 전달), `--bind 0.0.0.0`은 명시적 opt-in, `--read-only`로 mutating tool 비활성화. BRP(127.0.0.1:15702, 인증 없음, CORS `*` 예제)와 Unreal MCP(loopback, "not safe to expose beyond the local machine"), Unity(IPC named pipe + relay)의 현재 기본값을 감안하면 최소한 token은 둬야 브라우저 탭이나 떠도는 로컬 프로세스가 프로젝트 파일을 바꾸지 못한다. Unity처럼 **Ask(read-only tools) / Agent(mutating tools, 승인)** 두 집합을 annotations로 표시한다. 위협 모델은 §88.6.

---

# 47. MCP Tool 수를 무한히 늘리지 않는다

나쁜 방향:

```text
set_goblin_health
set_goblin_speed
move_player
create_enemy
create_player
...
```

좋은 방향 (▶ v2: 초안의 `create_entity / add_component / set_property / run_command` 대신):

```text
query
inspect
apply           ← changes[].op ∈ { entity.create, component.add, property.set, … } (schema로 검증)
validate
```

Generic operation 중심으로 간다.

▶ v2: **수치와 근거.**

- 2026년 community MCP 서버는 tool 수 경쟁 중이다: sam-david/unreal-mcp 127, tugcantopaloglu/godot-mcp 157, mkdevkit/godot-mcp 173, AnkleBreaker unity-mcp-server 268, soft-ue-cli 120+ commands. Godot MCP Pro(162 tools) 포럼 스레드의 첫 불만은 "토큰을 너무 쓴다". 반면 Coplay(13.6k★)는 "47 focused entrypoints", Summer Engine은 "fewer tools, sharper tools", Roblox 공식 reference 서버는 6개(run_code 중심), **Bevy BRP는 ~20개의 generic method로 충분히 동작한다.**
- Anthropic 측정(2025-11): tool 58개 ≈ 55K 토큰이 대화 시작 전에 소비됨; on-demand tool search로 85% 절감 + 정확도 49%→74%. OpenAI: "한 턴에 20개 미만" soft limit. Cloudflare Code Mode: tool 2개(search, execute)로 2,500 endpoint를 ~1,000 토큰에.
- **그러나 "적은 tool"이 "schema 없는 tool"을 뜻하지 않는다.** OpenAI: "enum과 object 구조로 invalid state를 표현 불가능하게 하라." Anthropic: "valid JSON이 correct usage를 뜻하지 않을 때" tool-use example이 필요하다. 초안의 untyped `run_command(op, args)`는 제거하고, `apply` 하나를 두되 `changes[]`의 각 항목을 op별 `oneOf` JSON Schema로 기술한다 (§49).

**결정**: 1st-party tool 10–15개, 상한 20개(BRP 수준). 확정 집합 (MCP tool 이름; command id의 `.`은 `_`):

```text
project_info   schema_describe   capabilities
query          inspect (entity|asset|snapshot, response_format concise|detailed)   explain   refs
apply (batch, atomic, dryRun, idempotencyKey, tx)
validate       run   run_status   test   capture
tx (begin|commit|rollback)          ← multi-call transaction을 MCP에서도 쓸 수 있게
history (undo|redo|list|checkpoint)
                                                         → 15개
```

개별 command(`entity.create`, `prefab.create`, `asset.import` …)는 tool이 아니라 `apply.changes[].op`의 `oneOf`이며 `game://schema/commands` resource로 조회한다 (§15 `commands[]`).

component 타입 · property path · enum 값은 inputSchema의 `enum` / `pattern`으로 넣어 잘못된 호출을 호출 전에 막는다. 읽기 tool에는 `response_format` 파라미터. description ≤ 2KB, 핵심을 앞에 (Claude Code truncation).

---

# 48. MCP와 CLI 동등성

가능하면 모든 MCP 작업은 CLI에서도 가능해야 한다.

예:

```text
MCP
set_property(...)
```

와

```bash
game set ...
```

가 같은 Command를 호출한다.

이러면 CLI 테스트만으로 MCP backend의 핵심 기능을 검증할 수 있다.

▶ v2: 동등성은 의도가 아니라 **테스트**여야 한다. `Tests/Contract/`:

```text
game capabilities --json 의 tools[] 모든 항목에 대해 (commands[]는 apply를 통해 간접 검증):
  1. 같은 project fixture에 CLI `--json`과 MCP `tools/call`을 동일 인자로 실행
  2. 둘 다 tool의 outputSchema(같은 2020-12 validator)로 검증
  3. structuredContent == CLI envelope (meta.durationMs 제외) 를 deep-equal assert
  4. isError == !ok, content[0].text 가 같은 envelope으로 파싱됨을 assert
  5. golden file 을 Tests/Contract/ 에 커밋
규칙: CLI JSON은 API 계약이다. field 삭제/rename은 meta.schemaVersion bump 없이는 금지.
```

---

# 49. AI 작업의 원자성

AI에게 너무 저수준 명령만 주는 것도 비효율적이다.

예:

```text
create entity
add transform
add sprite
add collider
...
```

를 매번 수십 번 호출하면 context / latency가 커진다.

따라서 두 레벨을 제공한다.

## Primitive Command

```text
component.add
property.set
```

## Batch / Recipe Command

```text
entity.create_from_template
prefab.instantiate
apply
```

예 (▶ v2: 규범적 형태로 교체):

```json
{
  "atomic": true,
  "dryRun": false,
  "idempotencyKey": "8f2c…",
  "tx": "tx_01j5…",
  "changes": [
    { "op": "entity.create",  "name": "Goblin", "world": "world_01j5…", "as": "$goblin" },
    { "op": "component.add",  "entity": "$goblin", "type": "Health" },
    { "op": "property.set",   "entity": "$goblin", "path": "/components/Health/max", "value": 100 }
  ]
}
```

응답: envelope의 `result.items[]`에 op마다 `{ok, result|error}`, 그리고 합쳐진 `changes[]`(§78).

규칙:
- `atomic:true`(기본) → §9 transaction으로 all-or-nothing. JSON-RPC 2.0 batch 배열과 달리 원자적이다.
- 위치 기반 `$0` 대신 **이름 있는 참조**(`as` / `$name`). CommandBus가 해석하고 ChangeSet의 `intent.resolved`에 남긴다. **ops에는 항상 실제 id만** 들어간다 — placeholder가 기록되면 replay/redo가 비결정적이 된다.
- `idempotencyKey`: 같은 key로 재시도한 동일 batch는 no-op이며 원래 결과를 돌려준다 → `idempotentHint:true`가 정직해진다.
- 호출당 op 상한(예 500). 초과하면 `meta.truncated` 안내 또는 `run.start` 식 job handle / MCP Tasks.
- `changes[]`의 각 항목은 op별 `oneOf` schema로 검증된다 (§47).

OpenAI("항상 순서대로 호출되는 함수는 합쳐라"), Anthropic(batching / 토큰 권고), MCP 2026-07-28의 stateless handle 지침과 일치하며 CLI(`game apply changes.json`)와 MCP 의미론이 같다.

---

# 50. Dry Run

AI가 대량 수정 전에 결과를 확인할 수 있어야 한다.

```bash
game apply changes.json --dry-run
```

출력 (사람용):

```text
Would modify:

Entities: 32
Components: 32
Assets: 0

Validation:
0 errors
2 warnings
```

▶ v2: Dry-run은 "시뮬레이션"이 아니라 **fork된 메모리 모델에 실제 command를 실행하고 commit만 생략**하는 것이다. 별도 simulate 코드를 만들면 실제 경로와 어긋난다.

```text
game apply changes.json --dry-run
  = ProjectModel fork = model.Clone()          // 개인 프로젝트 규모의 JSON이면 복사 비용 무시 가능
    CommandBus.Execute(fork, commands) → ChangeSet
    Validate(fork) → diagnostics
    출력: 실제 실행과 **같은 envelope** — ok, changes[] (동일 Change 구조), warnings[], meta.dryRun:true
    fork 폐기
```

- 출력의 핵심은 summary가 아니라 **ops 자체**다 (AI가 적용 전 diff를 읽는다). `--summary-only`로 축약.
- `nlohmann::json::patch()`(복사본 반환, strong exception guarantee)가 문서 단위 dry-run 원시 연산이고 `patch_inplace()`가 commit 경로다.
- kubectl처럼 `--dry-run=client`(schema/인자 검증만, 프로젝트 로드 없음)와 `--dry-run=server`(위의 전체 실행)를 구분한다.
- dry-run 결과의 ChangeSet id(`cs_…`; 그 안의 `base` hash를 포함)를 `game apply --if-match <cs-id>`에 넘기면 "dry-run 이후 아무것도 안 바뀌었음"을 보장한다.
- 파괴적 op를 `--yes` 없이 호출하면 exit 4 + `error.ruleId = CONFIRMATION_REQUIRED` + `details.confirmCommand`(정확한 재실행 명령). 비-TTY agent를 깨뜨리는 interactive prompt는 없다 (Arcjet 패턴).

그다음 실제 적용:

```bash
game apply changes.json --if-match cs_01j5…
```

---

# 51. Diff

모든 command 이후 machine-readable diff를 얻을 수 있으면 좋다.

▶ v2: 초안의 `{path, property, before, after}`는 구조 변경을 표현 못 하고, AI용 diff와 git diff의 관계가 정의되지 않았다. **두 종류의 diff**를 명시한다.

**Semantic diff = ChangeSet.ops** (§78). `game diff --format changeset`, 또는 `--format json-patch`(doc/before를 뺀 순수 RFC 6902).

```json
{
  "changes": [
    { "op": "replace", "doc": "Prefabs/Goblin.prefab.json", "path": "/components/Movement/speed",
      "before": 4.0, "value": 4.8 }
  ]
}
```

**File diff = `git diff`** (`--format unified`). 이것이 의미 있으려면 §5.3 canonical serialization이 필요하다 — Editor / CLI / AI 어느 경로로 저장해도 **같은 바이트**가 나와야 한다 (Unity Force Text + UnityYAMLMerge, Godot tscn이 VCS 친화성을 위해 하는 일).

`game diff <checkpoint|cs-id> [<cs-id>]`: 두 시점 사이 ChangeSet들을 compose해 보여주고, 검증용으로 `json::diff(before_doc, after_doc)`를 돌려 `patch(before, ops) == after`를 assert한다.

AI 자기검증에 매우 유용하다.

---

# 52. Checkpoint

AI 작업 시작 전:

```bash
game checkpoint create before_enemy_balance
```

수정 후 문제가 있으면:

```bash
game checkpoint restore before_enemy_balance
```

Git과 별도로 runtime/editor 수준 checkpoint를 둘 수도 있다.

초기에는 Git commit을 이용해도 된다.

▶ v2: "초기에는 Git commit"은 동작하지만 AI가 수십 번 checkpoint를 만들면 사용자 git history가 오염되고, 바이너리 asset이 undo blob store와 이중 저장된다. **파일 해시 기반 content-addressed snapshot**이 수십 줄이면 되고 undo blob store와 objects 디렉터리를 공유한다.

```text
game checkpoint create <name>
  → Source 파일(Worlds/, Prefabs/, Data/, Assets/, Config/, project.json — Cache/ 제외)을 순회
  → 각 파일 sha256 → Cache/objects/<sha256> 에 없으면 복사 (dedupe; §78 undo blob store와 같은 디렉터리)
  → Cache/checkpoints/<name>.json = { createdAt, historyCursor: "cs_…", files: { path: sha256 } }
game checkpoint restore <name>
  → manifest와 현재 hash 비교 → 달라진 파일만 temp+rename 복원, 없는 파일 삭제
  → 복원 자체를 하나의 ChangeSet(actor=system:checkpoint, ops=file.replace/…)으로 history에 기록 → restore도 undo 가능
```

- 이것은 git의 blob/tree를 최소 구현한 것이다. 나중에 libgit2(`git_blob_create_from_buffer` → `git_treebuilder_insert/write` → `git_commit_create(ref="refs/game/checkpoints/<name>")` → `git_checkout_tree`)로 교체 가능하되, 사용자 git history를 오염시키지 않도록 **별도 ref namespace 또는 별도 bare repo**(`Cache/.gitcheck`)를 쓴다.
- Checkpoint = event sourcing의 snapshot. checkpoint 이전 history는 `game history compact`로 잘라낼 수 있다. Blender처럼 "완전 상대적" undo(목표 step까지 중간 step을 전부 로드)가 되지 않도록 N개 ChangeSet마다 자동 checkpoint를 찍어 임의 시점 복원을 O(변경 파일 수)로 만든다.
- Godot의 `.godot/` 캐시 손상 사례(2025-03 블로그)처럼, checkpoint는 **파생 캐시를 포함하지 않는다.**

---

# 53. Versioning / Migration

텍스트 데이터는 schema version이 필요하다.

```json
{
  "schemaVersion": 3
}
```

엔진 업데이트 후:

```bash
game migrate --to latest --dry-run [paths…]
game migrate --to latest
```

가능하게 한다.

AI가 migration log를 볼 수 있어야 한다.

▶ v2: 초안의 `--from 2 --to 3`은 **하나의 전역 버전 번호**를 전제한다. Unreal이 GUID 기반 per-system custom version을 만든 이유가 정확히 전역 카운터를 피하기 위해서였고, 실전에서 가장 흔한 마이그레이션은 **rename**(Unity `FormerlySerializedAs`, Unreal Core Redirects)인데 초안에는 없었다. 네 부분으로 나눈다.

```text
1. 파일 헤더 "schemaVersion": N  — 문서 레이아웃. 드물게 올림.
2. component별 버전 — schema의 "x-component-version". component JSON은 현재와 다를 때만 "_v": N 을 가진다 (파일을 깨끗하게).
3. rename 표 Migrations/renames.json  (component별로 중첩 — dotted path 없음)
   { "components": { "Hitpoints": "Health" },
     "properties": { "Health": { "maxHp": "max" } },
     "enums":      { "Movement": { "mode": { "Walk": "walk" } } } }
4. 순서 있는 migration step (C++ 등록):  MIGRATION(Health, 1→2, [](json& c){ … });
```

동작:
- **로드 시에는 항상 메모리로 migrate**한다 (옛 프로젝트가 돌아야 한다). 디스크에 쓰는 것은 `game migrate`(명시적, ChangeSet `actor=system:migrate` + `Migrations/log/<timestamp>.json`) 또는 Command가 어차피 그 파일을 저장할 때만.
- `--dry-run`은 §50과 같은 경로로 ops를 낸다 (Godot `--validate-conversion-3to4` 패턴). 실제 실행 전에 자동 checkpoint(`pre-migrate-<ts>`).
- 손실 없는 변환(rename, default 채움)은 `before`가 있으므로 undo 가능. 손실 있는 변환은 ChangeSet에 `"lossy": true`를 표시해 undo 대신 checkpoint restore를 안내.
- **Unreal 규칙 차용**: 최신 schemaVersion이 쓴 파일을 구버전 바이너리가 열면 `SCHEMA_VERSION_NEWER_THAN_ENGINE` 구조화 오류로 거부한다. best-effort 로드 금지.
- ChangeSet 자체도 `changeSetVersion`을 갖고, 오래된 history/journal은 upcaster로 최신 형태로 변환한다 (event sourcing의 upcasting).

---

# 54. Performance 관점

AI 친화성을 위해 런타임 성능을 희생할 필요는 없다.

Source Data:

```text
JSON
```

Runtime:

```text
Binary
```

로 분리하면 된다.

예:

```text
JSON
 ↓ Import / Cook
Binary Cache
 ↓
Runtime
```

Editor / AI:

```text
JSON / Metadata
```

Runtime hot path:

```text
packed memory
```

를 쓴다.

즉:

> **AI-friendly authoring format과 runtime layout은 별개의 문제다.**

---

# 55. ECS 후보

EnTT를 쓴다면:

```text
Entity
 ↓
Component Storage
```

를 빠르게 구축할 수 있다.

하지만 AI-native 설계가 ECS와 동일한 개념은 아니다.

다음도 가능하다.

```text
Actor / Component
```

핵심은 API가 명시적이고 introspectable한가다.

따라서:

> ECS를 쓰면 AI-native가 되는 것이 아니다.

▶ v2: 맞는 말이지만, **Flecs의 "everything is an entity" 모델**(component, system, observer, reflection type, alert, 심지어 query 자체가 entity)은 §55가 요구하는 introspectable API와 정확히 일치한다. 결정은 §3.1 spike로. 채택 시 저장소 정책: archetype 특성상 per-entity add/remove가 EnTT보다 느리므로(FAQ, abeimler benchmark), 매 프레임 붙였다 떼는 상태는 (1) component field로, (2) `CanToggle`로 enable/disable, (3) `Sparse` / `DontFragment` trait(4.1.0+; 4.1.5는 non-fragmenting *hierarchy* storage 추가)으로. 단일 component query와 add/remove는 EnTT가, 다중 component query와 bulk create/destroy는 Flecs가 빠르다 — 첫 게임이 2D/데이터 중심이면 어느 쪽도 병목이 아니다.

---

# 56. Renderer 범위

처음부터 “좋은 renderer”를 목표로 하지 않는다.

예를 들어 첫 게임이 2D라면:

```text
Sprite
Camera
Render Layer
Render Target
Basic Postprocess
```

만 만든다.

첫 게임이 low-poly 3D라면:

```text
Static Mesh
Skinned Mesh
PBR Material
Directional Light
Point Light
Shadow
Camera
```

정도에서 시작한다.

Renderer 요구사항은 **게임이 결정한다.**

---

# 57. 첫 타겟을 2D / 2.5D로 잡을 때 장점

PoC 검증에는 특히 좋다.

줄어드는 문제:

- 복잡한 Skeletal Animation
- 복잡한 Material
- Retarget
- IK
- Advanced Lighting
- Terrain
- LOD
- Nanite류
- 대형 World Streaming

즉, AI authoring 구조가 정말 좋은지를 훨씬 빨리 검증할 수 있다.

▶ v2: 초안은 §57에서 2D-first를 권하면서 §3에는 3D 엔진인 Jolt만 적었다. **2D-first PoC의 physics는 Box2D v3.1.1**(MIT, C17)로 시작한다 — 설정 없이 cross-platform + thread-count 독립 결정성, C API라 reflection/command 래핑이 쉽고, 2D 충돌·sensor·character mover를 제공한다. `PhysicsWorld` 인터페이스(`Step / CreateBody / Query / DrainContactEvents`)를 엔진 쪽에 두어 Jolt로 교체 가능하게 하되, **PoC 단계에서 두 엔진을 동시에 지원하지 않는다.** Box2D v3.2는 미출시(v3.1.1은 2025-06)이므로 태그를 고정한다. 목표 게임이 3D로 확정되면 처음부터 Jolt.

**어느 쪽이든 목표 게임을 이름 붙여야 이 결정이 닫힌다** (§88.0).

---

# 58. Networking

처음부터 엔진 공통 레이어에 넣지 않는 것을 권장한다.

게임에서 실제로 multiplayer가 필요할 때:

```text
Gameplay Simulation
        ↓
Network Replication
```

을 추가한다.

다만 처음부터 다음 정도는 고려한다 (▶ v2: 구체화).

- Persistent Entity ID + **결정적 runtime ID 할당 순서** (§7.1, §22.2)
- **T1 determinism (§22)** — "deterministic-ish"가 아니라 같은 binary에서 bit-identical
- Command = tick-stamped input (replay 포맷 §22.3)
- State snapshot + world hash (§26.1)

이는 나중에 networking에도 도움이 된다.

▶ v2 주의: **Box2D v3에는 rollback용 SaveState/RestoreState가 없다** (FAQ: "Box2D does not have rollback determinism"). lockstep(입력 동기화)이라면 Box2D로 충분하지만, rollback netcode가 목표가 되면 Jolt(`SaveState/RestoreState` + `CreateBodyWithID`로 BodyID 고정) 또는 자체 2D 충돌로 교체해야 한다. 어떤 netcode 모델을 쓸지에 따라 physics 선택이 달라지므로 지금 기록해 둔다.

---

# 59. AI와 Runtime Code

AI가 C++ 코드를 직접 생성할 경우에도 engine API가 좁아야 한다.

좋음:

```cpp
class EnemyMovementSystem
{
public:
    void Update(World& world, float dt);
};
```

그리고 Component 접근은 정형화한다.

AI가 엔진 내부 private implementation을 자주 만질수록 framework 유지보수가 어려워진다.

---

# 60. Engine Internal과 Game Code 경계

```text
Engine/
    변경 빈도 낮음

Game/
    AI가 자주 수정
```

을 목표로 한다.

AI에게 권장되는 수정 범위:

```text
Game/
Data/
Prefabs/
Worlds/
Tests/
```

Engine 내부 수정은 필요할 때만 한다.

---

# 61. Guardrail

AI에게 API 제약을 줄 수 있다.

예:

```text
Engine/Core/**
```

는 기본 read-only.

```text
Game/**
```

만 자동 수정 허용.

대규모 엔진 파괴를 막을 수 있다.

▶ v2: 경로 규칙은 **강제 지점**이 있어야 한다: (1) CLI/Command layer의 path policy(`project.json`의 `writable: ["Game/**", "Data/**", …]`), (2) pre-commit hook / CODEOWNERS + branch protection, (3) agent 측 hook(Claude Code의 PreToolUse 등). 셋 중 하나만으로는 안 된다. 경로 규칙이 못 막는 것(AI가 쓴 Game/ 코드가 런타임에 엔진을 망가뜨리는 것)은 **런타임 sandbox**(§61.1 Luau)가 보완한다. 위협 모델 전체는 §88.6.

## 61.1 ▶ v2: Game/ 로직의 언어 — C++ + 제한적 Luau

초안은 §59·§83에서 C++만 가정했고, 동시에 §23이 표현식 언어를 암시했다. 결정:

> **Game/ 로직은 C++가 1차 언어다. 스크립트 레이어는 Luau 하나만, 아래 경계 안에서만 쓴다. 자체 DSL은 만들지 않는다.**

근거:
1. **LLM은 C++를 잘 쓴다.** aider polyglot 벤치마크는 C++/Go/Java/JS/Python/Rust 6개 언어를 측정하며 Lua는 없다. 2026-06 연구(arXiv 2606.16827): high-resource 언어 pass@1 59–89%, Lua급 low-resource 27–84%, no-resource 언어(Gleam, MoonBit — 공개 언어인데도) 0–1%이고 그 둘에서는 실패 대부분이 문법 오류. 자체 DSL은 이보다 나을 이유가 없다.
2. C++ 컴파일 오류는 이미 구조화되어 있고(clang `-fdiagnostics-format=sarif`, MSVC `/experimental:log` SARIF — §88.5), 타입 오류를 빌드 단계에서 잡는 것이 AI 자기수정 루프에 유리하다.
3. **sandbox가 필요한 곳**(AI가 자주 바꾸고, 잘못돼도 엔진이 죽으면 안 되는 곳)만 Luau로 격리한다. Luau(0.734, 2026-08-14, 주간 릴리스, MIT): gradual typing + `luau-analyze` CLI 타입체커, `io/package/debug/load/dofile` 제거, 읽기 전용 builtin, 스크립트별 global table, host interrupt handler로 CPU 제한. VM은 C++11.
4. 대안 탈락: plain Lua + sol2(sol2는 2025-03 이후 commit 없음, Lua 5.5 미지원), Wren(0.4.0, 2021), wasm3(maintenance mode), wasmtime(강력하나 Cranelift JIT 포함 heavy), AngelScript(1인 maintainer), .NET hosting(NativeAOT single-file에 hostfxr 번들 불가).

경계:

| 레이어 | 언어 | 예 |
|---|---|---|
| Engine/** | C++ | 읽기 전용 (§61) |
| Game/Source/Systems, Components | **C++** | `EnemyMovementSystem::Update(World&, float)`, aggregate component struct + PropertyMeta |
| Tests/*.json의 assert.expr | §23.1 고정 문법 → 부족하면 Luau (sandboxed) | `"player.Health.current > 0"` |
| Data/에 붙는 행동 파라미터·작은 규칙 | JSON 우선 → 필요 시 Luau 스니펫 | `damage = base * (1 + crit)` |
| Editor / CLI / MCP sidecar | C++ / TS·Python (§46.1) | |

금지: Luau에서 ECS storage 직접 접근, 렌더/물리 호출, 파일 I/O. Luau에는 Command Layer의 Query/Get/Set만 바인딩한다. 바인딩은 sol2 같은 대형 래퍼가 아니라 얇은 수동 바인딩(Luau C API).

도입 시점: **Phase 5 이후**, 그리고 §23.1 고정 문법이 부족하다고 측정될 때만. Luau를 넣는 순간 `game lint`에 `luau-analyze` 타입체크를 포함한다. Luau 특유의 타입 문법을 현재 모델이 얼마나 잘 쓰는지는 공개 벤치마크가 없으므로 도입 전 20개 과제짜리 자체 eval을 한다.

---

# 62. AI-specific Lint

예:

```bash
game lint
```

검사:

- 직접 file path 참조 금지
- unstable entity name reference
- runtime-only property serialize
- component circular dependency
- prefab override 과다
- magic string
- invalid layer
- invalid asset reference

▶ v2 추가 — **결정성 lint** (Game/, Engine/Runtime 대상, clang-tidy/regex 기반; Jolt 문서의 애플리케이션 측 금지 목록 + Riot/Factorio의 실제 원인):

- sim 코드의 `std::sin|cos|tan|atan2|exp|pow(` → `det::` 사용 권고
- sim 코드의 `std::unordered_map|unordered_set` range-for 순회
- `std::hash`, `rand()`, `std::random_device`, `std::mt19937` 전역 사용
- sim 코드의 `SDL_GetTicks|steady_clock|system_clock`
- total order 없는 comparator의 `std::sort` (휴리스틱: float `<`만, 또는 pointer 비교)
- sim target에 `#pragma float_control` / `-ffast-math`
- Engine 내부 헤더를 Game/에서 include (§59/§61 경계)
- component struct가 aggregate가 아님 (§42.2 규칙)

`game lint`도 §29의 rule registry / SARIF / `--fix` / `--baseline` 규약을 그대로 따른다.

---

# 63. Observability

일반 게임 엔진보다 더 중요하게 봐야 한다.

AI가 관찰할 수 없는 문제는 고치기 어렵다.

필수:

```text
Logs
Metrics
State Dump
Reference Graph
Frame Capture
Profiler
Event Trace
Crash dump / Watchdog   ← ▶ v2 (§88.4)
```

---

# 64. Event Trace

예:

```bash
game trace entity_01j5xqd6… --ticks 300
```

출력:

```text
T100 State Idle → Chase
T102 Target Player
T180 AttackBegin
T194 DamageEvent 15
T230 AttackEnd
```

AI debugging에 매우 강력하다.

▶ v2: 자체 trace 포맷을 만들지 않는다. gameplay 이벤트는 **Chrome Trace Event JSON**(`trace.json`)으로 낸다 — ui.perfetto.dev에서 바로 열리고(legacy/best-effort지만 지원), `tracy-import-chrome`으로 Tracy에도 들어간다.

```json
{"displayTimeUnit":"ms","traceEvents":[
 {"ph":"M","pid":1,"tid":1,"name":"process_name","args":{"name":"game --headless TestArena"}},
 {"ph":"M","pid":1,"tid":1,"name":"thread_name","args":{"name":"Sim"}},
 {"ph":"X","pid":1,"tid":1,"ts":1650000.0,"dur":1800.0,"name":"EnemyAI::Update","cat":"system","args":{"tick":813}},
 {"ph":"i","s":"t","pid":1,"tid":1,"ts":1650120.0,"name":"StateChange","cat":"entity",
  "args":{"entity":"entity_01j5xqd6…","component":"EnemyAI","from":"Idle","to":"Chase","tick":100}},
 {"ph":"C","pid":1,"tid":1,"ts":1650000.0,"name":"frame_ms","args":{"sim":4.1,"render":9.2}}
]}
```

제약: `ts`/`dur`는 마이크로초, `X`/`B-E` 이벤트는 중첩되어야 함(겹치되 중첩 안 되면 Perfetto가 overflow track으로 보냄), thread당 `tid` 하나. entity 이벤트는 `ph:"i"` + `cat:"entity"`라서 `game trace entity_… --ticks 300`은 `args.entity` 필터일 뿐이다. 크래시 시 마지막 N개 이벤트를 flush한다 (§88.4).

---

# 65. Profiler 출력도 구조화

```bash
game profile --ticks 1000 --json
```

▶ v2: **Profiler = Tracy** (v0.14.0, 2026-08-09, BSD-3). 자체 profiler 포맷은 §70 규칙("현재 게임에 필요한가?")에 걸린다. 엔진은 `ZoneScopedN("EnemyAI::Update")`, `FrameMark`, `TracyPlot("entities", n)`, 0.14의 `TracySectionEnter/Leave`(run phase)만 심는다.

```text
headless: tracy-capture -o run.tracy -s 10   또는 tracy-capture-daemon (클라이언트 자동 발견)
export:   tracy-csvexport run.tracy     → name, src_file, src_line, total_ns, total_perc, counts, mean_ns, min_ns, max_ns, std_ns
          (-u/--unwrap 는 per-event 행: ns_since_start, exec_time_ns, thread)
AI 분석:  Tracy MCP server (PR #1347, 2026-05 merge — zones / frames / plots / messages / locks / summary stats)
          ※ Python sidecar(FastMCP + pybind11). Python 의존이 싫으면 game profile --json 이 csvexport만 감싼다.
```

`game profile --ticks 1000 --json`은 csvexport를 감싼 래퍼다. 초안의 `{name, avg_ms, max_ms}`는 **평균이 아니라 백분위**로:

```json
{
  "systems": [
    { "name": "EnemyAI", "calls": 1000, "total_ms": 1800, "mean_ms": 1.8, "p50_ms": 1.6, "p95_ms": 3.2, "p99_ms": 3.9, "max_ms": 4.1, "std_ms": 0.4 },
    { "name": "Physics", "calls": 1000, "total_ms": 900,  "mean_ms": 0.9, "p50_ms": 0.9, "p95_ms": 1.4, "p99_ms": 1.6, "max_ms": 1.7, "std_ms": 0.2 }
  ]
}
```

AI에게:

```text
프레임 16.6ms 아래로 줄여
```

같은 작업을 시킬 수 있다.

---

# 66. AI 최적화 루프

```text
Benchmark
 ↓
Profile
 ↓
Hotspot
 ↓
Code 수정
 ↓
Benchmark
 ↓
Compare
```

명령:

```bash
game benchmark Combat100
```

▶ v2: 초안의 단일 `delta` 퍼센트는 **노이즈를 쫓게 만든다.** Google Benchmark의 `compare.py`는 Mann–Whitney U 검정에 **≥ 9회 반복**을 요구하고("requires LARGE (no less than 9) number of repetitions to be meaningful"), Criterion.rs는 bootstrap t-test(α 0.05)에 `noise_threshold`(예 ±1%)를 더해 "유의하지만 무시할 만한" 변화를 걸러낸다.

`game benchmark Combat100 --repetitions 10 --out bench.json`은 **Google Benchmark JSON 호환** 형식(`context{date, host_name, num_cpus, cpu_scaling_enabled, library_version}`, `benchmarks[]{name, run_type:"iteration"|"aggregate", repetition_index, iterations, real_time, cpu_time, time_unit, aggregate_name:"mean"|"median"|"stddev"|"cv"|"p95"}` — p95는 `ComputeStatistics` 식 custom statistic)으로 써서 `compare.py benchmarks baseline.json candidate.json --dump_to_json diff.json`이 그대로 돈다. 자체 출력:

```json
{
  "benchmark": "Combat100", "metric": "frame_ms", "repetitions": 10,
  "baseline":  { "median": 13.2, "p95": 15.9, "cv": 0.021 },
  "candidate": { "median": 10.8, "p95": 12.4, "cv": 0.019 },
  "delta": { "median_pct": -18.2, "p95_pct": -22.0 },
  "test": { "method": "mann-whitney-u", "pValue": 0.0003, "alpha": 0.05 },
  "noiseThreshold": 0.02,
  "verdict": "improved"
}
```

판정 규칙: `regressed`는 **p < α 이고 |delta| > noiseThreshold일 때만**. 반복 < 9면 `inconclusive`. 측정 위생: `--no-render` headless + 고정 seed, warm-up tick 제외, `cpu_scaling_enabled` 경고 노출, frame-time은 평균이 아니라 p50/p95/p99/max. 두 `.tracy` 파일의 csvexport diff로도 같은 판정을 낼 수 있다.

이런 기능은 AI 개발에 굉장히 잘 맞는다.

---

# 67. Editor MCP 방식과 비교

## Unreal / Unity + MCP

장점:

- 엄청난 기존 기능
- 성숙한 Renderer
- 성숙한 Animation
- 성숙한 Asset Pipeline
- 다양한 Plugin
- 검증된 플랫폼 빌드
- 자료 많음

단점:

- AI가 엔진 특유의 object model을 알아야 함
- Editor 상태 의존
- binary / proprietary asset이 많음
- 작업이 GUI automation 성격을 가짐
- MCP 서버가 노출한 기능에 영향을 받음
- 외부 agent가 world state를 이해하는 비용이 큼

---

## AI-native Framework

장점:

- AI가 데이터 직접 수정
- CLI-first
- headless-first
- diff-friendly
- inspectable
- deterministic test에 유리
- protocol independent
- 모든 기능을 AI 관점으로 설계 가능
- 본인이 필요한 기능만 구현

단점:

- 엔진 기능을 직접 유지해야 함
- rendering / animation / asset pipeline 기능 부족
- 플랫폼 대응 직접 해야 함
- 디버깅 책임이 모두 자신에게 있음
- Editor productivity를 직접 만들어야 함
- 예상하지 못한 엔진 개발 시간이 생김

## 67.1 ▶ v2: 비교 대상은 둘이 아니라 여덟이다

초안은 "Unreal/Unity + MCP" 하나만 비교했다. 이 문서의 논지에 가장 강한 대안은 Godot(이미 text-first + headless)과 Bevy BRP(이미 generic reflection 기반 command API)다. 독자는 "왜 Godot/Bevy가 아닌가"를 물을 것이다 — §3.1이 그 답이고, 아래가 근거 표다.

| 시스템 | 진실의 원천 | Headless | 명령 API 위치 | Undo / Tx | MCP |
|---|---|---|---|---|---|
| Unreal 5.8 + Unreal MCP | .uasset(binary) / Editor 상태 | 제한 (PIE automation; cooked build에서 MCP 호스팅 가능) | Toolset Registry (`UFUNCTION(meta=AICallable)` / Python) | 문서화 안 됨 | 1st-party, HTTP+SSE loopback |
| Unreal Remote Control | Editor 메모리 UObject | Editor 전용 | `/remote/object/call`, `/property`, `/batch` | `generateTransaction` 지원 | 없음 |
| Unity 6.3 + Unity MCP | .unity/.prefab(YAML) + AssetDatabase | `-batchmode -nographics -executeMethod` | attribute 등록 tool, IPC relay | Editor Undo 의존 | 1st-party + Coplay(13.6k★, 47 tools) 등 |
| Godot 4.7 | .tscn/.gd (text) | `--headless --script --check-only --quit-after` | 없음 (스크립트/CLI) | Editor UndoRedo만 | community (157~173 tools) |
| Bevy 0.19 + BRP | Rust 코드 (+BSN, .bsn 로더 미출시) | 가능 | `world.*`, `registry.schema`, `rpc.discover` (JSON-RPC) | 없음 | 별도 crate (bevy_brp_mcp) |
| Flecs 4.1.6 (라이브러리) | 코드 / Flecs Script | 가능 | REST `/entity /component /query /type_info` | 없음 | 없음 (REST만) |
| soft-ue-cli (UE) | .uasset | PIE / dev build | CLI argparse → MCP 자동 생성, JSON stdout | **명시적으로 없음** | CLI 기반 |
| Summer Engine (2026) | .tscn/.gd | play / verification probe | 62 tools over HTTP :6550 | ? | proprietary editor + MIT bridge |
| **본 문서 (PoC)** | **JSON Project Data** | **1급** | **Command Core + ChangeSet** | **Transaction / Undo / Dry-run** | Phase 7 adapter |

> 차별점은 "명령 API가 있다"가 아니라(Bevy BRP, Unreal MCP가 이미 있음) **ChangeSet / Transaction / Validation / Diagnostic(fix) / 데이터화된 Test Scenario가 Command Core에 내장**되어 있다는 점이다.

Godot 기능 대응표 (Godot이 "이미 증명한 설계"와 "핵심 격차"):

| 문서 기능 | Godot 4.7 대응물 | 격차 |
|---|---|---|
| Text-first data (§5) | .tscn/.tres format=3, 기본값 생략 | 있음. 단 merge-unfriendly 이력 (load_steps, id churn) |
| Stable ID (§7) | 헤더 `uid://`, 4.4+ `.uid` sidecar, 4.6+ node `unique_id` | 있음. 복사 시 중복 UID 위험 (#91097) |
| Prefab override (§34) | inherited scene / `instance=` + 비기본값만 저장 | "unset vs override" 표현 불가 (#94912) |
| Command / Undo (§8, §10) | UndoRedo (do/undo callable + property), EditorUndoRedoManager | callable 기반. 데이터 diff(ChangeSet) 없음 |
| Transaction / Dry-run (§9, §50) | 없음 | **핵심 격차** |
| Structured diagnostics (§13, §79) | 텍스트 에러, `--log-file` | **핵심 격차** |
| Query (§16) | 없음 (`get_nodes_in_group` 수준) | **핵심 격차** |
| Headless run (§20) | `--headless --script --fixed-fps --quit-after` | 있음. 단 Editor API는 headless 1급 아님 (#8664, #14502) |
| Import sidecar (§37) | `.import` [remap]/[deps]/[params] + `.godot/imported/<name>-<md5>` | 있음 (거의 동일) |
| Test runner (§23–24) | gdUnit4 CLI (JUnit XML, exit 0/100/101), Godot Doctor | 있음. 단 scenario-as-data / snapshot 없음 |

---

# 68. 자체 프레임워크를 하면 안 되는 경우

다음 게임을 만들고 싶다면 Unreal/Unity를 유지하는 것이 낫다.

- 고품질 3D Character Action
- 복잡한 Animation
- Motion Matching
- 대규모 3D World
- 고품질 Lighting
- Niagara급 VFX
- 복잡한 Terrain
- Console 출시
- Mobile multi-platform
- 많은 외부 Plugin 의존
- 대형 Asset Marketplace 의존

이 경우 엔진이 제공하는 가치가 너무 크다.

---

# 69. 자체 프레임워크가 유리할 수 있는 경우

- 2D
- 2.5D
- low-poly 3D
- data-driven game
- simulation game
- roguelike
- strategy
- autobattler
- procedural world
- sandbox
- deterministic simulation이 중요한 게임
- AI가 대량 데이터 생성/테스트하는 게임

특히:

```text
규칙 / 시스템 / 데이터
```

가 게임의 핵심일수록 적합하다.

---

# 70. “엔진을 만들다 게임을 못 만드는” 문제 방지

모든 엔진 기능은 다음 질문을 통과해야 한다.

> **현재 게임에 필요한가?**

아니면 만들지 않는다.

예:

```text
“나중에 쓸 수도 있는 Terrain Editor”
```

→ 만들지 않는다.

```text
“현재 게임에서 필요한 Tilemap Brush”
```

→ 만든다.

이 Framework는 Game-driven이어야 한다.

---

# 71. PoC 성공 기준

PoC는 “화면이 나온다”가 성공이 아니다.

다음 시나리오를 테스트한다.

AI에게:

```text
1. 새 테스트 월드를 만들어라.

2. Player entity를 만들고
   WASD 이동을 추가해라.

3. Enemy prefab을 만들어라.

4. Enemy 10개를 배치해라.

5. 충돌을 추가해라.

6. 테스트를 실행해라.

7. 문제가 있으면 로그를 읽고 수정해라.

8. 최종 화면을 capture해라.
```

를 요청한다.

사람이 Editor를 만지지 않고 완료되어야 한다.

---

# 72. 비교 실험

동일한 작업을 다음 환경에서 시킨다 (▶ v2: 2개 → 5개 arm):

```text
A1. Unreal 5.8 + 공식 Unreal MCP
A2. Unity 6.3 + 공식 Unity MCP
A3. Godot 4.x headless + Claude Code (파일 직접 편집 + --headless --script)   ← 필수. 2026 학계 벤치마크의 표준 baseline
B.  AI-native PoC
C.  Bevy 0.19 + bevy_brp_mcp                                                ← generic command API는 있으나 tx/undo/persistent id 없음
```

A3가 필수인 이유: GameDevBench(ICML 2026, 333 Godot 과제), GameCraft-Bench(140 과제, headless Godot 4.6.2)가 정확히 이 설정을 쓴다. C가 필요한 이유: Unity/Unreal+MCP는 "generic command protocol이 핵심"이라는 주장에 대해 허수아비다 — Bevy는 그것을 이미 갖고 있으므로 정직한 baseline이다. **B가 A3와 C를 이기지 못하면 프레임워크를 만들 이유가 없다.**

측정 (▶ v2: 우선순위 재배치):

| 항목 | 측정 | 근거 |
|---|---|---|
| **Rubric pass-rate** (기능 / 콘텐츠 / 시각 3축, agent가 보지 못한 rubric으로 별도 judge) | 높을수록 좋음 — **1순위** | GameCraft-Bench / GameDevBench의 평가 방식 |
| **사람이 개입한 횟수** | 0에 가까울수록 좋음 | |
| **실패 후 자가 복구율** | 높을수록 좋음 | §13 fix 설계의 직접 검증 |
| **screenshot / visual 검증 호출 수** | 높을수록 디버깅 성공과 상관 | GameCraft-Bench |
| AI context 소비 (입력 누적 토큰) | 적을수록 좋음 | Anthropic tool-eval 권고 지표 |
| Tool error 수 (isError / non-zero exit) | 적을수록 좋음 | Anthropic 권고 |
| Schema violation 수 (outputSchema 실패) | 0 | §48 |
| Tool call 수 | **보조 지표** | GameCraft-Bench: tool 사용량과 품질 상관 r = +0.016 |
| 완료 시간 / 호출당 wall-clock | 짧을수록 좋음 | |
| 재현성 | 높을수록 좋음 | §22 hash |
| 엔진 내부 지식 요구량 | 낮을수록 좋음 | |

과제 형식: GameCraft-Bench 식으로 (a) 산출물 + (b) **재생 가능한 입력 trace**(§22.3 replay)를 제출하고, judge는 agent가 보지 못한 rubric으로 채점한다. 지표는 `meta.durationMs`와 §28 로그의 `event: "tool.call"` 레코드로 자동 수집한다.

**기대 규모**: 현 세대 최상위 agent의 게임 제작 과제 성공률은 41–55%다 (GameDevBench 53.8%, GameCraft-Bench 41.46%, GameEngineBench 55.5%). PoC 성공 기준은 "절대 성공"이 아니라 **A3 대비 상대 향상**으로 둔다.

이 결과가 별 차이 없으면 PoC를 중단한다.

---

# 73. PoC 중단 기준

다음 중 여러 개가 발생하면 과감하게 중단한다.

- §72 1순위 지표(rubric pass-rate, 인간 개입, 자가 복구율)에서 A3/C 및 1st-party MCP 대비 유의미한 차이가 없음
- Command API 관리 비용이 너무 큼
- Editor 제작 비용이 게임 제작비를 잡아먹음
- Asset Pipeline 구현이 계속 발목을 잡음
- Renderer 요구사항이 빠르게 증가
- Game feature보다 Engine feature 구현이 많아짐
- Framework 변경 때문에 Game code가 계속 깨짐

▶ v2 추가:

- **Phase -1 Substrate Spike(§3.1)에서 2주 안에 Flecs/EnTT 위에 §71 시나리오 1~5번의 Command 경로가 동작하지 않음**
- **Godot headless(A3) 또는 Bevy+BRP(C)가 §72 지표에서 PoC 대비 20% 이내** → 자체 프레임워크 중단, 그 위에 Command/Transaction/Diagnostics 레이어를 얹는 방향 검토
- §88의 미결 설계 결정이 Phase 1 시작 시점까지 내려지지 않음 (결정 없이 구현하면 Phase 3에서 다시 짠다)

---

# 74. 단계별 구현 순서

시간 기준이 아니라 **의존성 기준**으로 진행한다.

## Phase -1 — ▶ v2: Substrate Spike + 설계 결정 (2주)

```text
§3.1  Flecs vs EnTT spike, Godot-headless / Bevy+BRP 비교 arm
§88   미결 설계 결정: 목표 게임 이름, 프로세스 모델, authoring/play world, 입력 schema,
      크래시 진단, build 설계, 컴파일러, 보안 경계, sub-asset, save/load, 파일 granularity
```

성공:

```text
ECS 결정됨
§88 항목마다 "결정" 또는 "Phase N에서 결정 + 그때까지의 가정" 이 적혀 있음
§73 중단 기준 1차 통과
```

## Phase 0 — 최소 Runtime

```text
Application (fixed tick; headless 경로는 accumulator 없음 §20.1)
Window / Input (SDL3; dummy·offscreen driver 선택 §20)
Time (SimTime / WallTime 타입 분리 §22.2)
Logging (OTel JSONL §28)
Filesystem
det_fp_flags INTERFACE target + FPU env assert (§41)   ← ▶ v2: 첫 TU부터 적용되어야 한다
크래시 핸들러: minidump + 마지막 N trace flush + watchdog + exit 6/7 (§88.4)   ← ▶ v2
```

성공:

```text
창 열림 / --headless 로 창 없이 tick 루프 작동
키 입력 가능
game run --headless --ticks 60 --json 이 envelope을 내고 exit 0
강제 crash 시 minidump 경로가 envelope에 실리고 exit 6
```

---

## Phase 1 — World + Reflection + 데이터 모델 (▶ v2: 대폭 확장)

초안의 Phase 1은 리서치가 Phase 1에서 결정되길 원하는 것에 비해 크게 부족했다. Reflection은 Phase 1 Serialization, Phase 3 SetProperty/Validation, Phase 4 CLI help, Phase 6 Inspector, Phase 7 MCP schema의 **하드 의존성**인데 초안의 어느 Phase에도 없었고 §86 체크리스트에서는 Physics 뒤에 있었다. Reflection 없이 Phase 1 serializer를 만들면 component별 수작업 serializer가 생겨 §43이 금지하는 중복이 바로 시작된다.

```text
Entity / Component / World
Persistent ID            ← §7.1 TypeID grammar, §7.2 sidecar 규칙, §7.3 중복 정책
Reflection Registry      ← §42.2 REFLECT_COMPONENT / PropertyMeta / runtime registry 등록
Transform                ← 첫 reflected component
Serialization            ← PropertyMeta 테이블에서 JSON read/write 생성. §5.3 canonical 규약
Data model 결정          ← entities = id-keyed object, 계층 = parent + order, 파일 granularity (§88.9)
JSON Schema 생성         ← §14 (2020-12 + x-*), §14.1 wire_format
Box2D 통합 (PhysicsWorld 인터페이스 뒤; 목표 게임이 3D면 Jolt — §57/§88.0)
RngStream / 결정적 EntityId allocator (§22.2)
```

성공:

```text
game schema component Transform --json  → JSON Schema 2020-12 (reflection에서 생성)
game registry wire_format Transform     → spawn 예시 + mutation path 목록
JSON World Load / Save 가 PropertyMeta 테이블만으로 동작 (component별 수작업 serializer 0개)
x-runtimeOnly 속성이 저장 파일에 나타나지 않음
Round-trip: project JSON → world → serialize → byte-identical (§5.3)
Flecs 채택 시: project JSON → Flecs world → ecs_world_to_json → ecs_world_from_json → diff = 0
```

이후 Phase와의 의존 관계:
- Phase 3 Command: `SetProperty`는 registry의 set으로 구현, Validation은 minimum/maximum/required로 구현
- Phase 4 CLI: `game help <cmd> --json`, `game describe component X`는 같은 테이블을 출력
- Phase 6 Editor: Inspector는 PropertyMeta를 순회해 위젯 선택 (uiMin/uiMax → slider, enumOptions → combo, refType → picker)
- Phase 7 MCP: tools/list inputSchema = §14 schema 그대로

---

## Phase 2 — Render

2D 기준:

```text
Texture
Sprite
Camera
Render Layer
```

3D 기준이면 최소 mesh renderer.

---

## Phase 3 — Command

```text
Command Bus (CommandKind 분리, ChangeBuilder, CommandApply 단계 §8)
ChangeSet (§78)  /  write-ahead journal + temp+rename commit (§9.2)
Transaction (명시적 handle §9.1) — 이 Phase에서는 in-process(test/Editor) 범위. CLI 다중 호출 tx는 Phase 4 `game serve` 이후
Undo = inverse(ops), actor 태깅, context별 history (§10)
Validation (rule registry, SARIF, --fix §29)
Query (BRP 구조 + Flecs expr §16)
Dry-run = fork + execute (§50)
```

여기부터 AI-native 구조가 시작된다.

---

## Phase 4 — CLI

```text
create / delete / add / remove / get / set / query / validate / run
envelope (§12) · error + exit code (§13) · capabilities descriptor (§15)
non-TTY → JSON 기본, --fields / --jq / --limit / --cursor
game serve --rpc (Command Core 호스트, §88.1)
game build --json (§88.5)
```

Editor 없이 프로젝트 수정 가능.

---

## Phase 5 — Headless + Test + Capture

```text
--headless (dummy / offscreen) · --ticks · --seed · --threads · --hash-every  (§20.1)
Determinism T0/T1 검증 (run-twice, threads 1 vs N) (§22.2)
replay record / play / verify (§22.3)
Test Scenario + assertion 고정 문법 + inputs + events (§23)
results.json + JUnit XML (§24)
dump / snapshot 포맷 (§26.1)
capture + golden-image 비교 + software rasterizer (§27.1)
Chrome trace (§64) · Tracy (§65)   (OTel 로그·크래시 핸들러는 Phase 0)
```

▶ v2: capture와 test가 Phase 5에 **함께** 들어가는 이유는 §27 — visual feedback이 성공률을 좌우한다는 측정 결과.

---

## Phase 6 — Editor

```text
Hierarchy
Viewport
Inspector
Asset Browser
Console
```

전부 Command Layer 사용.

---

## Phase 7 — AI Adapter

```text
MCP sidecar (공식 TS/Python SDK, < 300 lines, §46.1)
  tools/list = game capabilities --json
  tools/call = game serve RPC → envelope
Contract test (§48)
§72 비교 실험 (A1 / A2 / A3 / B / C)
```

를 추가한다.

이 순서가 중요한 이유:

> MCP를 먼저 만들면 MCP 서버가 엔진이 되어버린다.

▶ v2: 2026년의 Godot MCP 서버들(157~173 tools, 편집기 GUI가 떠 있어야 동작하거나 `godot --headless --script` shell-out)이 이 경고의 실물이다.

---

# 75. 권장 Repository 구조

```text
AIEngine/

├── Engine/
│   ├── Core/
│   ├── Reflection/
│   ├── Serialization/
│   ├── Runtime/
│   ├── ECS/
│   ├── Render/
│   ├── Physics/
│   ├── Assets/
│   ├── Commands/
│   ├── Validation/
│   └── Testing/
│
├── Tools/
│   ├── CLI/
│   ├── Editor/
│   └── MCP/
│
├── Game/
│   ├── Source/
│   ├── Data/
│   ├── Prefabs/
│   ├── Worlds/
│   └── Tests/
│
├── ThirdParty/
│
└── CMakeLists.txt
```

---

# 76. 중요한 Dependency Rule

가능하면:

```text
Engine Core
    ↑
Engine Runtime
    ↑
Game
```

을 유지한다.

금지:

```text
Engine Core
 ↓
Game
```

Tool:

```text
Editor
 ↓
Commands
 ↓
Runtime
```

MCP:

```text
MCP
 ↓
Commands
```

CLI:

```text
CLI
 ↓
Commands
```

---

# 77. Command 예제

```cpp
struct SetPropertyRequest
{
    UUID Entity;
    TypeId Component;
    PropertyPath Property;
    Variant Value;
};

CommandResult Execute(
    const SetPropertyRequest& request
);
```

결과 (▶ v2: `CommandKind`와 정합):

```cpp
struct CommandResult
{
    bool Success;

    ErrorCode Error;

    std::optional<ChangeSet> Changes;        // Mutation 만. Query / RuntimeControl 은 nullopt

    std::vector<Diagnostic> Diagnostics;
};
```

---

# 78. ChangeSet

모든 수정 작업이 ChangeSet을 만들도록 한다.

▶ v2: 초안의 `Change{Object, Property, Before, After}`는 **property 변경만** 표현한다. entity create/delete, component add/remove, reparent/reorder, asset import/delete, prefab/world 문서 생성은 표현할 수 없었고, 따라서 §84-6 "모든 수정은 ChangeSet을 남긴다"가 성립하지 않았다. Unity 문서(`RegisterCompleteObjectUndo`)가 명시하듯 "Transform parent change, AddComponent, and object destruction can not be restored with this function" — Unity · Unreal · Godot은 구조 변경용 별도 undo 메커니즘을 추가해야 했고, Figma · Loro는 parent를 property로 두어 구조 변경을 property write로 환원했다 — 후자를 따른다 (§78.1).

리서치가 수렴한 표현: **RFC 6902 JSON Patch의 superset** — property와 structural 변경을 같은 op 집합으로, `before`를 강제해 self-inverting하게, `file.*` op로 바이너리 asset까지.

```json
{
  "changeSetVersion": 1,
  "id": "cs_01j5…",
  "tx": "tx_01j5…",
  "actor": "ai:claude#42",
  "createdAt": "2026-08-21T10:00:00Z",

  "intent": {
    "op": "entity.create",
    "args": { "name": "Goblin", "world": "world_01j5…" },
    "resolved": { "$goblin": "entity_01j5xqd6…" }
  },

  "base": { "Worlds/TestArena.world.json": "sha256:ab12…" },

  "ops": [
    { "op": "add",     "doc": "Worlds/TestArena.world.json", "path": "/entities/entity_01j5xqd6…",
      "value": { "name": "Goblin", "parent": null, "order": "a0", "components": {} } },
    { "op": "replace", "doc": "Prefabs/Goblin.prefab.json", "path": "/components/Movement/speed",
      "before": 4.0, "value": 4.8 },
    { "op": "remove",  "doc": "Worlds/TestArena.world.json", "path": "/entities/entity_01j5…/components/Collider",
      "before": { "shape": "capsule", "radius": 0.4 } },
    { "op": "move",    "doc": "Worlds/TestArena.world.json",
      "from": "/entities/entity_01j5…/components/OldName", "path": "/entities/entity_01j5…/components/NewName" },
    { "op": "test",    "doc": "Worlds/TestArena.world.json", "path": "/schemaVersion", "value": 1 },
    { "op": "file.add",     "doc": "Assets/Textures/goblin.png", "blob": "sha256:…" },
    { "op": "file.remove",  "doc": "Assets/Textures/old.png",    "beforeBlob": "sha256:…" },
    { "op": "file.replace", "doc": "Assets/Textures/goblin.png", "beforeBlob": "sha256:…", "blob": "sha256:…" }
  ],

  "touched": ["Worlds/TestArena.world.json", "Prefabs/Goblin.prefab.json"],
  "summary": { "entities": { "created": 1, "deleted": 0, "modified": 1 },
               "components": { "added": 0, "removed": 1 }, "assets": { "added": 1, "removed": 1 } },
  "lossy": false,
  "diagnostics": []
}
```

규칙:

1. `doc` = 프로젝트 상대 파일 경로. `path` = **RFC 6901 JSON Pointer**(`~`→`~0`, `/`→`~1`). dotted path 금지. `path: ""`(빈 pointer)는 문서 전체 = 문서 생성/삭제.
2. `remove / replace / move / file.remove / file.replace`는 **반드시 `before`(또는 `beforeBlob`)** 를 싣는다 → source 문서 없이 역연산 가능 (jiff의 invertibility 조건을 test op 대신 필드로 내장).
3. `copy`는 **금지**(비가역). command는 `add` + value를 emit한다.
4. 바이너리 asset은 `file.*` op로, 내용은 content-addressed blob store(`Cache/objects/<sha256>`; §52 checkpoint와 같은 디렉터리)에.
5. `replace`의 새 값은 RFC 6902와 같이 `value`다(`after`가 아님). 그래서 `ops`에서 `doc` / `before`를 제거하면 그대로 RFC 6902 patch → `nlohmann::json::patch()`에 넣을 수 있다.
6. **`intent`는 audit/log용이다. redo에는 쓰지 않는다** (§10.1). placeholder는 `resolved`에만 남고 `ops`에는 실제 id만.
7. `base`는 optimistic concurrency (§9.2).
8. ChangeSet은 command가 `ChangeBuilder`로 **직접 emit**한다. `nlohmann::json::diff()`(add/remove/replace만, 비최소)로 사후에 만들지 않는다 — diff는 §51의 검증 오라클로만.

## 78.1 Command → ops 매핑

| Command | ops |
|---|---|
| entity.create | `add /entities/{id}` value = entity 전체 (persistent id 포함) |
| entity.delete | `remove /entities/{id}` before = entity 전체 + 자식 entity들도 각각 `remove` (삭제 subtree 전체를 `before`에 보존 — Figma 방식) |
| component.add | `add /entities/{id}/components/{Type}` value = reflection default로 채운 component (→ redo/replay가 reflection 버전에 의존하지 않음) |
| component.remove | `remove /entities/{id}/components/{Type}` before = component 전체 |
| property.set | `replace /entities/{id}/components/{Type}/{field…}` before/value |
| entity.reparent | `replace /entities/{id}/parent` + `replace /entities/{id}/order` (cycle이면 command가 diagnostic으로 거부 — Figma/Kleppmann) |
| entity.reorder | `replace /entities/{id}/order` (fractional index 문자열. 형제 배열 index 사용 금지) |
| entity.rename | `replace /entities/{id}/name` |
| prefab.create | `add ""` (Prefabs/X.prefab.json 문서 전체) + 원본 entity를 인스턴스로 전환: `add /entities/{id}/prefab` + `remove /entities/{id}/components`(before = 전체) + 필요 시 `add /entities/{id}/set` |
| prefab.instantiate | `add /entities/{newId}` (prefab = id, set/add/remove만 포함) |
| asset.import | `file.add Assets/…` + `add ""` Assets/….meta.json (Cache 산출물은 대상 아님) |
| asset.delete | `file.remove`(beforeBlob) + `remove ""` meta 문서 (참조 있으면 §19 규칙으로 거부) |
| world.create | `add ""` Worlds/X.world.json |
| project.migrate | `replace /schemaVersion` + 변환 ops, actor = system:migrate |
| checkpoint.restore | `file.replace` / `replace ""` …, actor = system:checkpoint |
| **(prefab 인스턴스 entity)** | 위 property.set / component.add / component.remove는 `/components/…` 대신 **override 맵**을 고친다: property.set = `replace` 또는 `add /entities/{id}/set/~1components~1Transform~1position`, component.add = `add /entities/{id}/add/~1components~1Shield`, component.remove = `add /entities/{id}/remove/-` (RFC 6901 escape `/`→`~1`). 비인스턴스 entity만 `/components/…` 경로 |

**전제 데이터 모델 제약** (§5.3에 반영): entities는 id-keyed object, 계층은 `parent` + `order`, 배열은 값 성격의 리스트(vec3, tags)에만 쓰고 통째로 replace. 그러면 **구조 경로에 배열 index가 등장하지 않으므로** undo 순서 역전 시 index shift 문제가 없고, 모든 structural op가 keyed object에 대한 add/remove/replace로 닫힌다. JSON Patch의 알려진 약점(배열 index 경로가 앞선 op에 밀림)을 데이터 모델 수준에서 피하는 것이다.

이것으로:

- Undo (§10.1 inverse)
- Diff (§51 semantic diff)
- Dry-run (§50)
- Logging / Audit (`intent` + `actor`)
- AI verification (envelope `changes[]`)
- Transaction (§9)
- Checkpoint / Migration (§52, §53)

을 처리한다.

---

# 79. Diagnostic

▶ v2: 초안의 `Diagnostic{Level, Code, Object, Message, Fix}`에는 두 가지가 빠져 있었다. (1) **물리 위치** — SARIF 2.1.0이 `physicalLocation`(파일/region)과 `logicalLocations`를 의도적으로 분리하듯, text-first 파일을 편집하는 AI는 논리 위치(object)만으로는 고칠 파일과 줄을 모른다. (2) **fix의 적용 가능성** — rustc JSON 진단의 `suggestion_applicability`(MachineApplicable / MaybeIncorrect / HasPlaceholders / Unspecified)처럼 "자동 적용해도 되는가"의 신호가 없으면 agent가 review 없이 적용할지 결정할 수 없다. LSP 3.17 `Diagnostic`/`CodeAction`(`isPreferred`, `relatedInformation`, `data`)과 SARIF(`fingerprints`, `fixes[].artifactChanges`)를 합친다.

```cpp
enum class Severity      { Note, Warning, Error };
enum class Applicability { MachineApplicable, MaybeIncorrect, HasPlaceholders, Unspecified };

struct LogicalLocation  { ObjectId object; std::optional<std::string> component; std::optional<std::string> propertyPath; };
struct PhysicalLocation { std::string uri; std::string jsonPointer; std::optional<Region> region; };   // region: startLine..endColumn
struct RelatedLocation  { Message message; std::optional<PhysicalLocation> physical; std::optional<LogicalLocation> logical; };

struct Fix
{
    std::string                    description;
    Applicability                  applicability;
    bool                           isPreferred;
    std::vector<CommandInvocation> commands;          // Command-layer ops (선호)
    std::vector<JsonPatchOp>       artifactChanges;   // RFC 6902 ops on the source file (선택)
};

struct Diagnostic
{
    Severity                        level;
    DiagnosticCode                  ruleId;        // §29 rule registry. SCREAMING_SNAKE
    Message                         message;
    std::optional<LogicalLocation>  logical;
    std::optional<PhysicalLocation> physical;
    std::vector<RelatedLocation>    related;
    std::vector<Fix>                fixes;
    std::string                     fingerprint;   // 실행 간 매칭 / baseline (SARIF §3.27.16–17)
    std::optional<std::string>      helpUri;
};
```

```json
{
  "ruleId": "COMPONENT_DEPENDENCY_MISSING", "level": "error",
  "message": { "text": "CharacterMovement requires Transform." },
  "logical":  { "object": "entity_01j5xq…", "component": "CharacterMovement" },
  "physical": { "uri": "Worlds/TestArena.world.json",
                "jsonPointer": "/entities/entity_01j5xq…/components/CharacterMovement",
                "region": { "startLine": 42, "startColumn": 5, "endLine": 48, "endColumn": 6 } },
  "related": [ { "message": { "text": "Transform is declared required here" },
                 "physical": { "uri": "Schema/CharacterMovement.schema.json", "region": { "startLine": 7 } } } ],
  "fixes": [ { "description": "Add Transform component", "applicability": "MachineApplicable", "isPreferred": true,
               "commands": [ { "op": "component.add", "args": { "entity": "entity_01j5xq…", "component": "Transform" } } ] } ],
  "fingerprint": "sha256:3c1f…",
  "helpUri": "game://docs/rules/COMPONENT_DEPENDENCY_MISSING"
}
```

AI 루프 정책: `game validate --fix`는 `MachineApplicable`만 적용, `MaybeIncorrect`는 `--fix=maybe`, `HasPlaceholders`는 절대 자동 적용 안 함. `--format sarif`로 진짜 SARIF 2.1.0을 낸다 (§29). 컴파일러 진단(§88.5)도 같은 구조로 변환한다.

AI 친화성에 직접 영향을 준다.

---

# 80. 최종 이상형

결국 다음 작업이 가능해지는 것이 목표다.

▶ v2: 예시를 §9.1 tx handle / §50 dry-run·`--if-match` / §10.2 actor undo에 맞게 갱신했다.

사용자:

```text
이 프로젝트에서
Collider 없는 Enemy를 전부 찾아서
적절한 Collider를 추가하고
충돌 검증 테스트 돌려.
```

AI:

```bash
game query --with EnemyAI --without Collider --fields id,name,path --json
```

결과 확인.

```bash
game tx begin --json            # → tx_01j5…
```

수정 (한 번에, dry-run 먼저).

```bash
game apply fixes.json --tx tx_01j5… --dry-run --json     # changes[] 를 읽고 확인
game apply fixes.json --tx tx_01j5… --if-match cs_01j5… --json
```

검증.

```bash
game validate --tx tx_01j5… --json
```

테스트.

```bash
game test CollisionSuite --json
```

성공.

```bash
game tx commit tx_01j5… --json
```

실패했다면:

```bash
game tx rollback tx_01j5…
# 또는 commit 후 발견했다면
game undo --actor ai:* --json      # 다른 actor와 겹치면 UNDO_CONFLICT
```

사람은 Editor를 한 번도 클릭하지 않는다.

---

# 81. 그런데 사람이 Editor를 열면

동일한 프로젝트가 그대로 보인다.

```text
Hierarchy
 ├─ Player
 ├─ Enemy_01
 ├─ Enemy_02
 └─ Enemy_03
```

Inspector에서 Collider를 누르면 AI가 추가한 값이 그대로 보인다.

즉:

> AI용 프로젝트와 인간용 프로젝트가 따로 존재하지 않는다.

이게 핵심이다.

---

# 82. Unreal / Unity를 즉시 버리지 않는 이유

현재 엔진들은 여전히 엄청난 기능을 제공한다.

▶ v2: Unreal의 경우 공식 Python API(5.8에서도 Experimental), Remote Control API(HTTP, **Editor 전용, 인증 없음**, `generateTransaction`으로 undo 가능), 그리고 **5.8(2026-06-17)부터 Editor 내장 공식 MCP 서버**(Unreal MCP — Experimental, loopback HTTP+SSE, `UFUNCTION(meta=(AICallable))`로 tool 노출, cooked build에서도 `IModelContextProtocolModule::StartServer()`로 호스팅 가능)를 제공한다. UEFN에도 2026-08-20에 Beta로 들어갔다. Unity는 Unity AI(6.3+, 2026-05 open beta)에 공식 MCP Server(IPC named pipe + relay, attribute 기반 tool 등록, startup 자동 발견)와 **승인 기반 Agent mode**("All modifying actions require your approval")를 포함하며, 기존 `-batchmode -nographics -executeMethod`로 GPU 없는 headless 자동화가 가능하다. Roblox도 Studio 내장 MCP와 plan→build→test Assistant(2026-04)를 제공한다.

즉 기존 엔진을 버려야만 AI 자동화를 할 수 있는 것은 아니다. 오히려 초안이 §67에서 쓴 "작업이 GUI automation 성격을 가짐"보다 **기존 엔진은 더 자동화 가능하다.**

따라서 가장 안전한 전략은:

```text
현재 게임
 → Unreal / Unity 계속 사용 (1st-party MCP 포함)

별도 PoC
 → AI-native framework 실험 — 단, 비교 대상은 community MCP 서버가 아니라 위의 1st-party 기능과 Godot headless (§72)
```

이다.

PoC가 실제로 더 낫다는 것이 측정됐을 때 다음 게임에 적용한다.

---

# 83. 현실적인 최종 방향

목표를 다음처럼 잡는 것을 권장한다.

> **개인용 AI-first game runtime/framework**

특성:

```text
C++

Text-first Project

Schema-first Components

Command-driven Authoring

CLI-first Automation

Headless-first Testing

Structured Diagnostics

Deterministic Simulation

Thin Visual Editor

MCP Optional

Game-specific Feature Set
```

---

# 84. 핵심 설계 원칙 요약

## 반드시 지킬 것

1. **Project Data가 Source of Truth다.**
2. **Editor는 frontend다.**
3. **MCP는 adapter다.**
4. **CLI가 1급 인터페이스다.**
5. **모든 수정은 Command를 통한다.**
6. **Command는 ChangeSet을 남긴다.**
7. **Headless 실행이 가능해야 한다.**
8. **오류는 structured diagnostic으로 반환한다.**
9. **AI가 state를 query할 수 있어야 한다.**
10. **Runtime data와 authoring data를 분리한다.**
11. **Engine feature는 Game 요구에 의해 추가한다.**
12. **Renderer 자체가 프로젝트의 목적이 되어서는 안 된다.**

▶ v2 추가:

13. **ChangeSet은 self-inverting이다.** 모든 mutation op는 `before`를 들고 있고, Undo는 command별 구현이 아니라 `inverse(ops)`다 (§10.1, §78).
14. **"텍스트"가 아니라 "canonical 직렬화"가 diff-friendly를 만든다.** 어느 경로로 저장해도 같은 바이트 (§5.3).
15. **Determinism은 권장이 아니라 계약이다.** same-binary bit-identical(T0/T1)을 CI가 검사한다 (§22).
16. **Diagnostic은 파일 위치와 적용 가능성을 들고 온다.** agent는 `MachineApplicable`만 자동 적용한다 (§13, §79).
17. **Command API의 wire format은 schema에서 1:1로 파생된다.** type schema만으로는 부족하다 (§14.1).
18. **Runtime id는 외부로 새지 않는다.** 참조는 항상 persistent id, 결과는 항상 id + name + path (§7).
19. **Authoring world와 Play world는 다르다.** play 변경은 `promote` 없이는 authoring에 닿지 않는다 (§88.2).
20. **기성으로 살 수 있는 것은 산다.** Flecs(기본 후보, §3.1 spike 후 확정) / Tracy / SDL3 driver / OTel model / RFC 6902 / JSON Schema 2020-12. 만드는 것은 §0.1의 5개다.

---

# 85. 최종 판단

현재 AI 개발 흐름에서:

```text
AI
 → 인간용 Editor
 → MCP
 → Engine Object Model
```

이라는 구조가 최종 형태일 가능성은 낮다고 본다.

▶ v2: 실제로 2026년에 엔진 수준 command API를 가진 시도들은 같은 방향을 택했다. Bevy는 엔진 내부에 reflection 기반 JSON-RPC(BRP)를 두었고 MCP는 커뮤니티 crate(bevy_brp_mcp)가 그 위에 얹혔으며, Summer Engine은 proprietary editor 위에 MIT CLI/MCP bridge를 얹되 "fewer tools, sharper tools"와 script-error → screenshot → probe 검증 루프를 중심에 두었으며, soft-ue-cli는 CLI의 argparse에서 MCP schema를 자동 생성한다. Epic조차 Unreal MCP의 tool을 UFUNCTION metadata에서 생성한다. **즉 "명령 API를 먼저 두고 MCP는 어댑터"라는 원칙은 더 이상 가설이 아니라 업계 합의에 가깝다.** 이 문서의 남은 차별점은 그 명령 API에 ChangeSet / Transaction / Validation / Diagnostic을 1급으로 넣는 것과, 프로젝트 데이터 자체를 canonical text schema로 두는 것이다 (§0.1).

장기적으로는 오히려:

```text
             Human
               ↓
AI → Command / Data Model ← Editor
               ↓
            Runtime
```

같은 구조가 자연스럽다.

여기서 중요한 것은 “에디터를 없애는 것”도 아니고 “MCP를 없애는 것”도 아니다.

**인간용 Editor를 게임 제작 시스템의 중심에서 내리는 것**이다.

그 결과:

```text
Editor
CLI
AI Agent
Build Server
Test Runner
```

모두 같은 프로젝트 모델과 같은 명령을 사용한다.

현재 단계에서는 Unreal / Unity를 폐기할 이유는 없다.

하지만 별도의 작은 PoC로 이 구조를 검증하는 것은 충분히 가치가 있다.

PoC에서 확인해야 하는 질문은 단 하나다.

> **같은 게임 작업을 시켰을 때 AI가 Godot headless(A3) · Bevy+BRP(C) · 1st-party MCP(A1/A2)보다 더 높은 rubric pass-rate, 더 적은 인간 개입, 더 높은 자가 복구율로 끝내는가?** (tool call 수와 context는 보조 지표 — §72. ▶ v2에서 질문을 고쳤다.)

YES면 계속 만든다.

NO면 버린다.

이 기준을 벗어나 “엔진 자체를 만드는 재미”로 기능을 추가하기 시작하면 프로젝트의 목적이 바뀐 것이다.

---

# 86. 첫 PoC 체크리스트

▶ v2: Phase 순서(§74)와 "사서 쓰는 것 vs 만드는 것"이 드러나게 재구성했다.

**Phase -1 — Spike + 결정**
- [ ] 목표 게임 이름 붙이기 (2D/3D, 장르, 규모 — §88.0)
- [ ] Flecs vs EnTT spike (§3.1) — ChangeSet observer + undo replay 테스트 포함
- [ ] Godot headless + Claude Code / Bevy + bevy_brp_mcp 로 §71 시나리오 1~5 측정 (§72 A3, C)
- [ ] §88 각 항목에 결정 또는 "Phase N까지의 가정" 기록
- [ ] 컴파일러 결정 (MSVC vs clang-cl, §88.5)
- [ ] 의존성 관리 하나 선택 (CPM + SHA 고정 / vcpkg manifest)

**Phase 0 — Runtime**
- [ ] CMake ≥ 3.28 (4.x면 `CMAKE_POLICY_VERSION_MINIMUM=3.5`), C++20 (glaze면 C++23)
- [ ] Application / Main Loop — **fixed tick, headless 경로는 accumulator 없음** (§20.1)
- [ ] SDL3 3.4.x Window / Input / `dummy` · `offscreen` driver 선택 (§20)
- [ ] `det_fp_flags` INTERFACE target + FPU env assert (§41)
- [ ] Logging: OTel JSONL (§28)
- [ ] 크래시 핸들러: minidump + 마지막 N trace 이벤트 flush + exit code 6/7 + watchdog (§88.4)

**Phase 1 — World + Reflection + 데이터 모델**
- [ ] Persistent ID: TypeID/UUIDv7 grammar, 형식 검사, 중복 정책, `game id fix` (§7)
- [ ] Reflection registry: `REFLECT_COMPONENT` / `PropertyMeta` / runtime 등록 (§42.2)
- [ ] Transform (첫 reflected component, aggregate struct)
- [ ] World, entities = id-keyed object, 계층 = parent + order (§5.3)
- [ ] JSON load / save — reflection 기반 serializer 하나 + visibility mask 3종 (§26.1)
- [ ] Canonical serialization + `game fmt` + round-trip byte-identical 테스트 (§5.3)
- [ ] JSON Schema 2020-12 생성 + `registry.wire_format` (§14, §14.1)
- [ ] Box2D v3.1.1 통합 (PhysicsWorld 인터페이스 뒤) — 또는 목표 게임이 3D면 Jolt (§57)
- [ ] `RngStream` (xoshiro256** + SplitMix64 seeding, per-system) + 결정적 EntityId allocator (§22.2)
- [ ] (Flecs) project JSON → world → `ecs_world_to_json` → `ecs_world_from_json` → diff = 0

**Phase 2 — Render**
- [ ] Sprite 또는 Simple Mesh rendering (SDL_GPU)
- [ ] Camera

**Phase 3 — Command**
- [ ] Command Bus: `CommandKind`, `ChangeBuilder`, CommandApply 단계 (§8)
- [ ] ChangeSet (§78) + Command → ops 매핑 전부 (§78.1)
- [ ] write-ahead journal + temp+rename commit + 시작 시 복구 (§9.2)
- [ ] Transaction handle + TTL + `BASE_MISMATCH` (§9.1)
- [ ] Undo = inverse(ops), actor 태깅, `UNDO_CONFLICT`, context별 history, MergeMode (§10)
- [ ] Validation rule registry + Diagnostic (§79) + `--fix` + `--format sarif` (§29)
- [ ] Query (BRP 구조; Flecs면 `--expr`) (§16)
- [ ] Dry-run = fork + execute, `--if-match` (§50)
- [ ] Reference graph (authoring JSON 기반) + sub-asset 주소 (§19)
- [ ] Prefab set/add/remove + absent=inherit + 3단계 체인 테스트 (§34)
- [ ] Checkpoint CAS (§52)
- [ ] Migration: per-component version + rename table + migrate-on-load (§53)

**Phase 4 — CLI**
- [ ] envelope + `--output` + non-TTY JSON 기본 + `--fields` / `--jq` / `--limit` / `--cursor` (§12)
- [ ] 오류 envelope + exit code 표 (§13)
- [ ] `game capabilities --json` = 완전한 tool descriptor (§15)
- [ ] `game serve --rpc` (Command Core 호스트, loopback + token, §88.1)
- [ ] `game build --json` → 컴파일러 진단을 §79 Diagnostic으로 (§88.5)
- [ ] Data hot reload via efsw → `project.reload_document` command (§39)
- [ ] Asset import sidecar + cache key + `game cache gc/verify` (§37–§38)

**Phase 5 — Headless + Test + Capture**
- [ ] `--headless --ticks --seed --threads --hash-every` (§20.1)
- [ ] `world.Hash()` + system sub-hash (§22.2)
- [ ] CI: run-twice + threads 1 vs N hash diff, 첫 divergent tick/path 보고 (§22.2, §24)
- [ ] `game replay record / play / verify` (§22.3)
- [ ] Test Scenario: setup/as, inputs, events, determinism, assertion 고정 문법 + 시점 (§23)
- [ ] `results.json` + `--junit` (§24)
- [ ] Snapshot 포맷 + `game snapshot diff` + JSONPath inspect (§26.1)
- [ ] Screenshot capture + golden 비교 + software rasterizer 설정 (§27.1)
- [ ] Tracy 계측 + `tracy-capture` headless + `game profile --json` (§65)
- [ ] Chrome trace `trace.json` + `game trace` (§64)
- [ ] `game benchmark` Google Benchmark JSON + Mann–Whitney + noise threshold (§66)
- [ ] `game lint` 결정성 규칙 (§62)

**Phase 6 — Editor**
- [ ] (Flecs) `flecs::Rest` + Explorer 확인, **Explorer 편집은 play world 한정** (§31)
- [ ] Hierarchy / Viewport / Inspector(PropertyMeta 순회) / Asset Browser / Console — 전부 Command 호출 (§32)

**Phase 7 — AI Adapter + 실험**
- [ ] MCP sidecar (공식 SDK, < 300 lines) (§46.1)
- [ ] Contract test: CLI envelope == MCP structuredContent (§48)
- [ ] §72 비교 실험 A1 / A2 / A3 / B / C, rubric + replay trace 제출

**언제든 (옵션, 측정 조건부)**
- [ ] cr.h Game/ DLL reload — AI 루프 1회 > 60초일 때 (§39)
- [ ] Luau — §23.1 고정 문법이 부족하다고 측정될 때 (§61.1)
- [ ] C++26 annotation front-end — MSVC가 `__cpp_impl_reflection`을 정의할 때 (§42.2)

---

# 87. 참고 자료

▶ v2: 초안의 6개 출처(7개 URL)를 리서치에서 실제로 확인한 1차 출처로 확장했다. (접근일 2026-08-21. 괄호 안은 확인한 버전/날짜.)

## 엔진 / 선행 사례

**Unreal Engine**
- Unreal MCP in Unreal Editor (UE 5.8, Experimental, 2026-06) — https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor
- Unreal Engine 5.8 Release Notes — https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-5-8-release-notes
- Unreal Python API — https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/introduction
- Remote Control API HTTP Reference (Editor 전용, 인증 없음, `generateTransaction`) — https://dev.epicgames.com/documentation/en-us/unreal-engine/remote-control-api-http-reference-for-unreal-engine
- UPROPERTY specifiers (속성 어휘) — https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-uproperty-specifiers
- Core Redirects / Versioning of Assets / DDC / Screenshot Comparison / FScopedTransaction — dev.epicgames.com 해당 페이지 (5.8)

**Unity**
- Unity MCP overview (com.unity.ai.assistant 2.x, 2026-05 open beta) — https://docs.unity3d.com/Packages/com.unity.ai.assistant@2.0/manual/unity-mcp-overview.html
- CoplayDev/unity-mcp (MIT, 13.6k★, "47 focused entrypoints") — https://github.com/CoplayDev/unity-mcp
- IvanMurzak/Unity-MCP (Apache-2.0, `[AiTool]`) — https://github.com/IvanMurzak/Unity-MCP
- Undo.RegisterCompleteObjectUndo (구조 변경 undo 불가 명시) — https://docs.unity3d.com/ScriptReference/Undo.RegisterCompleteObjectUndo.html
- YAML prefab serialization (m_Modifications / m_RemovedComponents) — https://docs.unity3d.com/6000.6/Documentation/Manual/yaml-prefab-serialization.html
- Importer Consistency / AssetDatabase v2 — https://docs.unity3d.com/Manual/ImporterConsistency.html
- ImageComparisonSettings (graphics test framework) — docs.unity3d.com com.unity.testframework.graphics@7.8

**Godot** (4.7.2, 2026-08-18)
- Command line tutorial (`--headless --script --check-only --quit-after --fixed-fps`) — https://docs.godotengine.org/en/stable/tutorials/editor/command_line_tutorial.html
- TSCN file format — https://github.com/godotengine/godot-docs/blob/master/engine_details/file_formats/tscn.rst
- UID changes in 4.4 (`.uid` sidecar) — https://godotengine.org/article/uid-changes-coming-to-godot-4-4/
- 4.6 release (Jolt 기본, node `unique_id`, LibGodot, `load_steps` 제거) — https://godotengine.org/releases/4.6/
- UndoRedo / EditorUndoRedoManager 클래스 문서 — docs.godotengine.org
- Import process (`.import` sidecar, `.godot/imported`) — https://docs.godotengine.org/en/stable/tutorials/assets_pipeline/import_process.html
- Issues: #91097 (중복 UID race), #94912 (inherited scene 암묵 override), #102490 (UID duplicate detected), proposals #1281 (merge driver, closed), #8664 / #14502 (headless editor API / ready signal)
- gdUnit4 CLI (JUnit XML, exit 0/100/101) — https://godot-gdunit-labs.github.io/gdUnit4/latest/advanced_testing/cmd/
- Godot MCP 서버: Coding-Solo/godot-mcp, tugcantopaloglu/godot-mcp (157 tools), mkdevkit/godot-mcp (173 tools), hi-godot/godot-ai — GitHub
- godot-jolt determinism discussion #548 — https://github.com/godot-jolt/godot-jolt/discussions/548

**Bevy** (0.19.1, 2026-08-13)
- bevy::remote (BRP method list) — https://docs.rs/bevy/latest/bevy/remote/index.html
- BRP 원 설계 gist (transaction/batch 미룸) — https://gist.github.com/coreh/1baf6f255d7e86e4be29874d00137d1d
- Bevy 0.15 release (BRP 도입) — https://bevy.org/news/bevy-0-15/#bevy-remote-protocol-brp
- Bevy 0.19 release (BSN, .bsn 로더 미출시) — https://bevy.org/news/bevy-0-19/
- PR #16882 (registry.schema), PR #18068 (rpc.discover, params 비어 있음), PR #19377 (method 이름 변경), issue #16042 (BRP race), issue #23637 (Jackdaw editor write-back 경험) — github.com/bevyengine/bevy
- bevy_brp_mcp 0.22.3 (type guide 필요성) — https://docs.rs/crate/bevy_brp_mcp/latest , archived README: https://github.com/natepiano/bevy_brp_mcp-ARCHIVED
- bevy_reflect attributes — https://docs.rs/bevy_reflect/latest/bevy_reflect/
- ci_testing / headless_renderer — docs.rs/bevy dev_tools, examples/app/headless_renderer.rs
- Skein (Blender ↔ BRP) — https://bevyskein.dev/docs/fetching-the-bevy-type-registry

**Flecs** (v4.1.6, 2026-06-29, MIT)
- Queries.md / FlecsRemoteApi.md / FlecsScript.md / PrefabsManual.md / ObserversManual.md / ComponentTraits.md / EntitiesComponents.md / MigrationGuide.md — https://github.com/SanderMertens/flecs/tree/master/docs
- meta.h (`ecs_member_t` range/warning/error) — https://github.com/SanderMertens/flecs/blob/master/include/flecs/addons/meta.h
- JSON / alerts addon — https://www.flecs.dev/flecs/
- abeimler/ecs_benchmark (Flecs vs EnTT) — https://github.com/abeimler/ecs_benchmark

**기타 선행 사례**
- Roblox/studio-rust-mcp-server (archived 2026-04-03) — https://github.com/Roblox/studio-rust-mcp-server
- softdaddy-o/soft-ue-cli (CLI → MCP, "No transactions/undo") — https://github.com/softdaddy-o/soft-ue-cli
- SummerEngine/summer ("fewer tools, sharper tools") — https://github.com/SummerEngine/summer
- O3DE prefab overrides (RFC 6902) — O3DE wiki / sig-content RFC #112
- Figma multiplayer (per-property LWW, parent + fractional index) — https://www.figma.com/blog/how-figmas-multiplayer-technology-works/
- Kleppmann, CRDT tree move — https://martin.kleppmann.com/2021/10/07/crdt-tree-move-operation.html
- Loro UndoManager (자기 변경만 undo) — https://docs.rs/loro/latest/loro/struct.UndoManager.html
- Blender undo system — https://projects.blender.org/blender/blender-developer-docs/ (features/core/undo.md)

## 벤치마크 / 측정

- GameDevBench (ICML 2026, 333 Godot 과제, best 53.8%, visual feedback 41.1→52.0%) — https://arxiv.org/abs/2602.11103
- GameCraft-Bench (140 과제, headless Godot, tool usage vs quality r=0.016) — https://arxiv.org/abs/2606.17861
- GameEngineBench (UE5 C++, 실패 = lifecycle/authority/등록 순서) — https://arxiv.org/abs/2607.03525
- Play2Code / PlaytestArena (+14.6p with play-testing agent) — https://arxiv.org/abs/2605.28258
- LLM code generation for low-resource languages (no-resource 0–1%) — https://arxiv.org/abs/2606.16827
- aider polyglot leaderboard — https://aider.chat/docs/leaderboards/

## Model Context Protocol (2026-07-28)

- Specification blog post — https://blog.modelcontextprotocol.io/posts/2026-07-28/
- Changelog (stateless core, `_meta`, `server/discover`, `resultType`, MRTR, deprecations) — https://modelcontextprotocol.io/specification/2026-07-28/changelog
- Server tools (outputSchema, `isError`, Stateful Tools handle 지침, annotations) — https://modelcontextprotocol.io/specification/2026-07-28/server/tools
- Tasks extension — https://modelcontextprotocol.io/extensions/tasks/overview
- schema.ts — https://github.com/modelcontextprotocol/modelcontextprotocol/blob/main/schema/2026-07-28/schema.ts
- Claude Code MCP (25k 토큰 상한, 2KB description, tool search) — https://code.claude.com/docs/en/mcp
- Claude Code headless (`--output-format`) — https://code.claude.com/docs/en/headless

## Tool / CLI 설계 지침

- Anthropic, Writing tools for agents (2025-09) — https://www.anthropic.com/engineering/writing-tools-for-agents
- Anthropic, Advanced tool use (tool search 85% 절감) — https://www.anthropic.com/engineering/advanced-tool-use
- OpenAI function calling guide ("fewer than 20") — https://developers.openai.com/api/docs/guides/function-calling
- Cloudflare Code Mode — https://blog.cloudflare.com/code-mode-mcp/
- clig.dev — https://clig.dev/
- gh CLI formatting (`--json --jq --template`) — https://cli.github.com/manual/gh_help_formatting
- kubectl output / `--dry-run=client|server` — https://kubernetes.io/docs/reference/kubectl/
- Arcjet, Designing a CLI for AI agents (2026-06) — https://blog.arcjet.com/designing-a-cli-for-ai-agents/
- RFC 9457 Problem Details — https://www.rfc-editor.org/rfc/rfc9457.html

## 표준 / 포맷

- RFC 6901 JSON Pointer, RFC 6902 JSON Patch — https://www.rfc-editor.org/rfc/rfc6902
- RFC 7386/7396 JSON Merge Patch (부적합 판단) — https://www.rfc-editor.org/rfc/rfc7386
- RFC 8785 JCS (canonical JSON for hashing) — https://datatracker.ietf.org/doc/html/rfc8785
- RFC 9535 JSONPath — https://www.rfc-editor.org/rfc/rfc9535.html
- RFC 9562 UUIDv7 — https://www.rfc-editor.org/rfc/rfc9562.html
- TypeID spec v0.3 — https://github.com/jetify-com/typeid/tree/main/spec
- JSON Schema 2020-12 — https://json-schema.org/specification
- OpenTelemetry Logs Data Model — https://opentelemetry.io/docs/specs/otel/logs/data-model/
- Chrome Trace Event format (Perfetto 호환) — https://perfetto.dev/docs/getting-started/other-formats
- LSP 3.17 Diagnostic / CodeAction — https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/
- SARIF 2.1.0 — https://docs.oasis-open.org/sarif/sarif/v2.1.0/sarif-v2.1.0.html
- rustc JSON diagnostics (applicability) — https://doc.rust-lang.org/rustc/json.html
- CEL language definition — https://github.com/cel-expr/cel-spec/blob/master/doc/langdef.md
- JUnit XML in GitLab (`[[ATTACHMENT|…]]`) — https://docs.gitlab.com/ci/testing/unit_test_reports/
- Microsoft, Event Sourcing pattern — https://learn.microsoft.com/en-us/azure/architecture/patterns/event-sourcing
- ReplaceFileW (원자성 미보장, 부분 실패 상태) — https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-replacefilew

## 결정론

- Jolt Architecture.md, Deterministic Simulation — https://github.com/jrouwe/JoltPhysics/blob/master/Docs/Architecture.md
- Box2D FAQ (determinism) — https://box2d.org/documentation/md_faq.html
- Box2D, Determinism blog (2024-08) — https://box2d.org/posts/2024/08/determinism/
- Box2D test_determinism.c — https://github.com/erincatto/box2d/blob/main/test/test_determinism.c
- Box3D 발표 (2026-06, alpha) — https://box2d.org/posts/2026/06/announcing-box3d/
- MSVC `/fp` — https://learn.microsoft.com/en-us/cpp/build/reference/fp-specify-floating-point-behavior
- GCC `-ffp-contract` — https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html
- Gaffer On Games: Fix Your Timestep / Floating Point Determinism — https://gafferongames.com/
- Riot, Determinism in League of Legends (2018) — https://www.riotgames.com/en/news/determinism-league-legends-fixing-divergences
- Factorio FFF-188 / FFF-340 (desync) — https://factorio.com/blog/post/fff-340
- Unity ECSGalaxySample determinism.md — https://github.com/Unity-Technologies/ECSGalaxySample/blob/main/_Documentation/determinism.md
- xoshiro / SplitMix64 — https://prng.di.unimi.it/ ; PCG streams 경고 — https://www.pcg-random.org/

## C++ Reflection

- P2996 / P3394 (annotations) — https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2025/p3394r4.html
- GCC C++ status (GCC 16 `-freflection`) — https://gcc.gnu.org/projects/cxx-status.html
- Clang C++ status (P2996: No) — https://clang.llvm.org/cxx_status.html
- bloomberg/clang-p2996 ("not for production") — https://github.com/bloomberg/clang-p2996
- EnTT v4.0.0 release / meta.md — https://github.com/skypjack/entt/releases/tag/v4.0.0
- glaze v8.1.0 — https://github.com/stephenberry/glaze
- reflect-cpp v0.25.0 — https://github.com/getml/reflect-cpp
- magic_enum v0.9.8 — https://github.com/Neargye/magic_enum
- Boost.Describe (속성 없음) — https://www.boost.org/doc/libs/latest/libs/describe/
- RTTR (2021 정체) / refl-cpp (2022 정체) / Refureku (2024 정체) — GitHub

## 라이브러리 (§3 표)

- SDL3 3.4.14 — https://github.com/libsdl-org/SDL/releases ; Filesystem category (파일 감시 없음) — https://wiki.libsdl.org/SDL3/CategoryFilesystem ; 라이선스 — https://wiki.libsdl.org/SDL3/FAQLicensing
- Jolt v5.6.0 — https://github.com/jrouwe/JoltPhysics
- Box2D v3.1.1 — https://github.com/erincatto/box2d ; Box3D — https://github.com/erincatto/box3d
- Dear ImGui v1.92.9b / Docking — https://github.com/ocornut/imgui/wiki/Docking
- nlohmann/json v3.12.0 (patch/diff/comments/object_order) — https://json.nlohmann.me/
- jsoncons (JSON Schema 2020-12, JSONPath) — https://github.com/danielaparker/jsoncons
- Tracy v0.14.0 / MCP server PR #1347 — https://github.com/wolfpld/tracy
- efsw — https://github.com/SpartanJ/efsw
- cr.h — https://github.com/fungos/cr
- Live++ pricing — https://liveplusplus.tech/pricing.html
- Luau 0.734 / sandbox — https://luau.org/sandbox
- sol2 Lua 5.5 미지원 issue #1721 — https://github.com/ThePhD/sol2/issues/1721
- CMake 4.0 release notes — https://cmake.org/cmake/help/latest/release/4.0.html
- Quill JSON logging — https://quillcpp.readthedocs.io/en/latest/json_logging.html
- pixelmatch — https://github.com/mapbox/pixelmatch
- Google Benchmark tools (compare.py) — https://google.github.io/benchmark/tools.html ; Criterion.rs analysis — https://bheisler.github.io/criterion.rs/book/analysis.html
- cel-cpp — https://github.com/cel-expr/cel-cpp
- libgit2 101 samples — https://libgit2.org/docs/guides/101-samples/
- Vulkan docs CI/CD (SwiftShader) — https://docs.vulkan.org/tutorial/latest/Building_a_Simple_Engine/Tooling/02_cicd.html

---

# 88. ▶ v2: 미결 설계 결정 — Phase 1 전에 주인을 정해야 하는 것들

리서치를 종합한 critic의 결론: 이 문서를 막는 것은 라이브러리 선택이 아니라 **아무도 맡지 않은 아키텍처 결정**이다. 아래 항목은 §71 PoC 시나리오를 headless로 실행하려면 반드시 답이 있어야 한다. 각 항목에 **권장 기본값**을 적되, 최종 결정은 Phase -1(§74)에서 한다.

## 88.0 목표 게임의 이름

§57(2D vs 3D), §3(Box2D vs Jolt), §69(적합 장르), §88.9(파일 규모) 모두 "첫 게임이 무엇인가"에 달려 있다. 문서는 게임을 이름 붙이지 않았다.

> **권장**: Phase -1 첫날에 한 문장으로 적는다 — 장르, 2D/2.5D/3D, entity 규모(수백 / 수천), 멀티플레이 여부(§58), 플랫폼(Windows만?). 이 문장이 §3 표의 Physics/Rendering 행을 확정한다.

## 88.1 Command Bus의 프로세스 / 동시성 모델

문서는 암묵적으로 **상주 프로세스**를 요구한다: `game tx begin … commit`이 별도 CLI 호출에 걸치고(§9), `game dump` / `game snapshot --tick 813` / `game set`이 실행 중인 시뮬레이션을 대상으로 하며(§20, §25, §26), Editor는 in-process로 CommandBus를 호출하고(§32), HTTP/RPC와 MCP가 peer로 나열된다(§1). 그러나 이들이 어떻게 공존하는지는 정의되지 않았다.

> **권장 기본값**:
> ```text
> game serve            — Command Core 호스트 (데몬). 프로젝트 lock 보유. loopback JSON-RPC + per-session token (§46.2)
> game <cmd>            — 얇은 클라이언트. 데몬이 있으면 RPC, 없으면 one-shot in-process (파일 lock 획득)
> GameEditor            — 데몬 + ImGui UI 한 프로세스
> game run --headless   — 데몬의 자식 또는 데몬 자신이 runtime을 host (play world §88.2)
> ```
> - **단일 writer**: 데몬이 프로젝트의 유일한 writer. Editor / CLI / MCP / file watcher 모두 데몬의 CommandBus로 들어온다.
> - 명령은 프레임 경계(CommandApply 단계, §8.2)에서만 적용 — 결정론 계약과 양립하는 유일한 선택.
> - 외부 편집(사람이 VS Code로 JSON 수정)은 watcher → `project.reload_document` command로 들어오고, 진행 중 transaction의 `base`를 무효화한다 (§9.2, §39).
> - 전송: stdio(로컬 CLI) + localhost HTTP JSON-RPC(BRP 스타일, Editor/MCP sidecar/외부 도구). named pipe(Unity 방식)는 선택.
> 이 결정이 journal/lock 전략, stateless MCP 위의 transaction 가능 여부, Editor가 외부 ChangeSet을 알게 되는 방법(bus 구독 vs watcher)을 정한다.

## 88.2 Authoring world vs Play world

§11 `game set`은 authoring 데이터를 바꾸고, §25 `game dump`는 runtime 값(`Health.current: 24`)을 보여주며, §26은 실행 중 프레임을 snapshot하고, §23은 테스트 run 안에서 entity를 spawn한다. world가 하나인지 둘인지, play 중 `game set`이 무엇을 바꾸는지(파일? live sim? 둘 다?), play 변경이 stop 시 버려지는지(Unity 모델) — 초안에는 정의되지 않았다. (runtime-spawned entity의 persistent id는 v2 §7.1에서 결정적 UUIDv8 → `promote` 시 UUIDv7로 정했다.)

> **권장 기본값**:
> - **둘이다.** Authoring document model(JSON, ChangeSet, history)과 Play world(ECS runtime). `game run`은 authoring에서 play world를 빌드한다.
> - play 중 mutation은 **`remote` history**(§10.1)에 기록되고 stop 시 버려진다. 남기고 싶으면 `game promote <entity|changes>`가 authoring ChangeSet을 만든다 (이때 runtime-spawned entity는 UUIDv7을 새로 받는다, §7.1).
> - deterministic run 중의 live edit은 tick-stamped command로 replay에 기록된다 (§22.3). 아니면 replay가 깨진다.
> - reflection flag는 `runtimeOnly` 하나가 아니라 **visibility matrix**다 — 정의는 §88.8 한 곳에.
> - Flecs Explorer / REST는 play world만 본다 (§31).

## 88.3 입력 — action map schema, 주입, 기록/재생

§71 과제 2("WASD 이동")를 headless 테스트(§21–§24)로 검증하려면 입력이 데이터여야 한다. 초안의 §23에는 입력 step이 없었고, `Config/input.json`(§5)과 "invalid input action"(§29)은 action map 포맷을 암시만 했다.

> **권장 기본값**:
> ```json
> // Config/input.json  ($schema: game://schema/input/1)
> { "actions": { "MoveX": { "type": "axis",   "bindings": [ { "keys": ["A", "D"], "scale": [-1, 1] }, { "gamepad": "leftStickX" } ] },
>                "Attack": { "type": "button", "bindings": [ { "key": "Space" }, { "mouse": "left" } ] } } }
> ```
> - sim은 **device 입력을 절대 직접 읽지 않는다.** `InputFrame { tick, actions: {MoveX: 1.0, Attack: pressed} }`만 받는다. device → action 변환은 sim 밖(platform layer)이다.
> - §23 `inputs` 블록과 `game input inject --tick N --action Attack`이 `InputFrame`을 만든다.
> - `game replay record`로 사람이 플레이한 세션을 `inputs.jsonl`로 저장 → 그대로 테스트 fixture (§22.3). bevy_brp_extras의 `send_keys` / mouse 주입과 GameCraft-Bench의 demo trace가 같은 개념이다.

## 88.4 크래시 / 행 / UB 진단

§21은 루프를 "run → 구조화 진단 읽기 → 고치기"로 가정하지만, AI가 쓴 C++는 validation 오류 외에 segfault / deadlock / 무한루프도 낸다 (빈도는 §72에서 측정). 초안 §63의 관측 목록에 크래시가 없었다 (v2에서 추가).

> **권장 기본값**:
> - Windows: `SetUnhandledExceptionFilter` + `MiniDumpWriteDump` → `Cache/crash/<run>.dmp` + 심볼화된 스택을 §13 envelope(`category: "crash"`, exit 6)에. 마지막 N개 trace 이벤트(§64)와 마지막 tick snapshot을 flush.
> - `--timeout <dur>` watchdog: 초과 시 스택 dump + exit 7 (`category: "timeout"`).
> - 빌드 flavor: `game build --sanitize asan,ubsan` (clang-cl), CI에서 주기 실행. MSVC는 `/fsanitize=address`.
> - exit code 표(§13)가 crash(6) / timeout(7) / findings(3: validation·test) / 도메인 오류(1)를 구분한다 — §72의 "오류 복구율" 측정의 전제.

## 88.5 `game build` — AI 루프의 일부

Build는 §11/§21에 있지만 설계가 없다. 세 리서처가 "MSVC vs clang-cl"을 각각 다른 이유로 미결로 남겼다: C++26 reflection 실험(GCC 16 / Bloomberg clang-p2996 fork만), glaze C++23 호환, Live++/cr.h, `/fp:precise` vs `-ffp-contract`.

> **권장 기본값**:
> - **MSVC(VS2022+)를 기본**, clang-cl을 CI의 두 번째 컴파일러로 (sanitizer, SARIF 진단, reflection 실험). 둘 다 `det_fp_flags`를 통과해야 한다.
> - `game build --json`은 CMake/ninja를 감싸고 컴파일러 진단을 **§79 Diagnostic으로 변환**한다: clang `-fdiagnostics-format=sarif`, MSVC `/diagnostics:column` + SARIF log (`/experimental:log`). 파일·줄·컬럼·fix-it이 같은 모양으로 agent에게 간다.
> - **루프 latency 예산**: 수정 → 빌드 → headless run → 진단까지 **60초**. 넘으면 §39 cr.h DLL reload를 고려. 측정값을 §72에 "seconds per loop"로 기록.
> - Game/는 처음엔 **static lib**. DLL로 바꾸는 것은 cr.h 도입 시점에, reflection registry(static initializer)와 EnTT meta context / Flecs world가 DLL 경계를 넘어 공유되는지 확인한 뒤.

## 88.6 노출된 command surface의 보안과 신뢰 경계

문서는 mutating API를 HTTP/RPC와 MCP로 노출하고(§1, §46) AI에게 파괴적 op(§19 asset delete)를 허용한다. BRP와 Unreal Remote Control은 인증이 없고 loopback만이 보호막이다.

> **권장 기본값** (단일 사용자 PoC도 adapter 계약에는 적는다):
> - loopback 바인딩 + per-session token (§46.2). 브라우저 탭의 DNS-rebinding/CSRF 대비 `Origin` 검사.
> - 파괴적 op는 `--yes` 없으면 exit 4 + `confirmCommand` (§50).
> - **프로젝트 데이터 필드는 agent에게 untrusted 입력이다.** `description`, `notes`, 테스트 이름 등에 들어 있는 텍스트는 데이터이지 지시가 아니다 — prompt injection 경로. Claude Code 등 agent 측 규칙과 일치.
> - `Engine/**` read-only는 세 곳에서 강제 (§61).

## 88.7 Composite asset과 sub-asset 주소

§3은 glTF를 택하고 §2.1은 Animation·Audio를 목표로 두지만, §7/§37은 파일 단위 ID만 정의했다. glTF 하나에 mesh/material/animation이 여럿이고, sprite atlas 하나에 sprite가 여럿이다.

> **권장 기본값**: `asset_01j…#<kind>/<name>` (§19; `kind` = sidecar `subAssets[].kind`). sidecar의 `subAssets[]`가 이름을 소유하고, importer는 재import 시 **이름으로** 매칭한다. reference graph는 sub-asset 단위 edge를 가진다. cache key는 파일 단위(sub-asset은 같은 산출물 안의 offset). Unity fileID / Godot sub-resource id가 index·생성 순서에 묶여 있는 설계가 재import 시 참조를 깨뜨릴 수 있다는 것이 "index가 아니라 이름"의 근거다 (리서치에서 직접 확인된 버그는 아님).

## 88.8 세 가지 직렬화 소비자 — authoring / snapshot / save

초안에서는 Save/Load가 §2.1 이후 한 번도 등장하지 않았다. v2는 §26.1·§42.2에 소비자/플래그만 추가했고, 포맷 결정은 여기서 한다. save 파일은 runtimeOnly 상태와 RNG/tick을 포함해야 하고(§22), authoring 파일은 제외해야 하며, snapshot은 그 사이다.

> **권장 기본값**: reflection 기반 serializer **하나** + visibility mask 셋. **정의(유일)**: authoring = `!RuntimeOnly && !Transient`; snapshot = `!Transient`; save = `Save && !Transient` (§42.2 `PropFlags`). §26.1·§88.2는 이 정의를 참조한다. save = "다른 mask를 쓴 snapshot" + 게임 정의 헤더. save 버전은 §53과 같은 per-component version + rename table. save 파일도 §5.3 canonical JSON(또는 같은 키 규칙의 binary)으로, `game save inspect`가 동작해야 한다.

## 88.9 파일 granularity, canonical 순서, 텍스트 데이터의 규모

§5는 world당 JSON 하나를 보여주고 §71은 enemy 10개를 놓지만 §69는 procedural / 대량 데이터 게임을 겨냥한다(규모는 §88.0에서 정한다). changeset-undo 리서치는 "id 정렬 vs 생성 순서 vs order key"와 대량 삭제 ChangeSet의 blob 분리를 미결로 남겼다.

> **권장 기본값**:
> - 시작은 world당 1파일, prefab당 1파일. entity object 키 순서 = **id 정렬** (§5.3).
> - world가 N entity(예 2,000)를 넘거나 procedural content가 들어오면 `Worlds/Main/<chunk>.world.json`로 분할 — chunk 경계는 공간이 아니라 **authoring 단위**(encounter, region). 분할은 `game world split` command로만.
> - `entity.delete`의 `before`가 subtree 전체를 포함하므로, ChangeSet 크기가 임계(예 1MB)를 넘으면 `ops`를 `Cache/objects/`(§78 blob store)로 빼고 history에는 참조만 둔다.
> - `game validate` / `game query`가 JSON 재파싱 대신 binary cache(§54)를 써야 하는 규모는 측정 후 결정 (§65).

## 88.10 (EnTT 채택 시) 자체 query 문법 — 만들지 않는다

§16에 적었다. 구조화 형태(BRP 모양)만. 텍스트 DSL은 Flecs가 있을 때만 그것을 쓴다.

---

# 89. ▶ v2: 변경 요약

| 절 | 변경 |
|---|---|
| 헤더, §0, **§0.1 신설** | 1st-party MCP 현황 반영. 리서치 후 문서의 위치 — 원칙은 업계 합의, 차별점은 5개로 축소, 약해진 전제 2개 명시 |
| §1 | MCP 2026-07-28 변경 사항을 정확히 기술 (stateless core, `_meta`, `server/discover`, `resultType`, MRTR, Tasks, deprecations, HTTP+SSE 유예). C++ SDK 부재 |
| §3, **§3.1 신설** | 검증된 라이브러리 표(버전/날짜/라이선스). "라이브러리는 후순위" 정정. Build vs Adopt + Phase -1 Substrate Spike. Flecs 기본 후보 + 규율 |
| §5, **§5.3 신설** | Cache 삭제 가능성의 조건. Canonical Project JSON 규약 (주석 금지 이유, 키 순서, float 표기, id-keyed entities, parent+order 계층, 파생 카운터 금지, JCS 해시) |
| §6 | 예시를 규약에 맞게 수정. 표기 통일(PascalCase component / camelCase property / JSON Pointer). world 파일 예시 추가 |
| §7, **§7.1–7.4 신설** | TypeID v0.3 / UUIDv7 grammar, 결정적 runtime id, sidecar 규칙, 중복 ID 정책, id + name + path, BRP runtime-id 반례 |
| §8, **§8.1–8.2 신설** | `CommandKind` 분리(Mutation/Query/RuntimeControl/Meta), `ICommand::Undo` 제거, `ChangeBuilder`. command id 정식 명명 + BRP alias. CommandApply 단계 |
| §9, **§9.1–9.2 신설** | 명시적 tx handle (MCP stateless 대응). commit 경로: write-ahead journal + temp+rename, `BASE_MISMATCH` |
| §10, **§10.1–10.2 신설** | Undo = inverse(ops), redo는 effect 재적용, context별 history, MergeMode, actor 태깅, `UNDO_CONFLICT`, CRDT/OT 비채택 기록 |
| §11 | Godot 플래그 차용, headless 계약("Editor 전용 API 없음") |
| §12, **§12.1 신설** | 규범적 envelope, non-TTY JSON 기본, `--fields/--jq/--limit/--cursor`, API 계약 규칙, 출력 예산 |
| §13 | RFC 9457 + `isError` + applicability 기반 오류 envelope, exit code 표 |
| §14, **§14.1 신설** | JSON Schema 2020-12 + `x-*`, 검증기 선택, wire_format guide (Bevy 교훈) |
| §15 | 완전한 tool descriptor(OpenRPC 형태), annotations 규칙, tool 집합 고정 |
| §16 | 자체 DSL 폐기. BRP 구조 + (Flecs) expr. JSONPath는 선택용 |
| §18, §19 | lifecycle/context 노출 (GameEngineBench). reference graph는 authoring 기반, sub-asset 주소, Flecs traits |
| §20, **§20.1 신설** | SDL3 dummy/offscreen 매핑. accumulator 없는 headless 루프, 플래그 |
| §22, **§22.1–22.3 신설** | "physics tolerance" 폐기. T0/T1/T2 계약, 체크리스트(time/flags/math/RNG/ECS/physics/input/verification), replay 포맷 |
| §23, **§23.1 신설** | 스키마 확장(as, inputs, events, determinism, capture). assertion 시점·대상·언어 결정(고정 문법 → Luau; CEL-cpp/JSONPath/Flecs alerts 탈락 이유) |
| §24 | run 단위 results.json + JUnit XML + 결정성 divergence 보고 |
| §26, **§26.1 신설** | snapshot 포맷(rng/tick/seed/build/physics), Flecs round-trip 주의, 직렬화 소비자 3종 |
| §27, **§27.1 신설** | visual feedback의 측정 근거. golden-image 테스트: bucketing, tolerance, software rasterizer |
| §28 | OTel Logs Data Model JSONL, Quill/spdlog, MCP Logging 금지 |
| §29 | SARIF rule registry, `--fix` 의미론, baseline, Flecs alerts, 추가 검사 코드 9종 |
| §31 | Flecs Explorer 2단계 Editor + 우회 경고 |
| §34, §35 | set/add/remove + JSON Pointer, absent=inherit, Godot #94912 반례, 9개 규칙. diff = RFC 6902 |
| §37, §38 | sidecar 구조, cache key 정의, subAssets, `cache gc/verify` |
| §39 | efsw 설계(hot reload도 Command), cr.h/Live++ 비용표 + 60초 트리거 |
| §41 | `det_fp_flags`, C++ 표준 floor, CMake 4 |
| §42, **§42.1–42.2 신설** | 2026 reflection 선택지 표(C++26 현황, dead 라이브러리), `PropertyMeta` 단일 테이블, backend 교체 계획, aggregate 규칙, leaf 타입 |
| §43, **§43.1–43.2 신설** | 속성 어휘 대응표(6개 외부 시스템), 기성 라이브러리로 대체 가능한 부분 |
| §46, **§46.1–46.2 신설** | sidecar 토폴로지 결정, Command ↔ MCP 매핑표, 전송·보안 |
| §47 | 수치 근거(127~268 tools vs 47/6/20), `run_command` → typed `apply`, 15개 tool 집합 (command와 tool의 2층 구조) |
| §48 | Contract test 정의 |
| §49 | batch 규범 형태(atomic, named ref, idempotencyKey) |
| §50 | dry-run = fork + execute, client/server, `--if-match`, exit 4 |
| §51 | semantic diff vs file diff |
| §52 | content-addressed checkpoint, libgit2 경로, 자동 checkpoint |
| §53 | per-component version + rename table + migrate-on-load, Unreal 규칙 |
| §55, §57, §58 | Flecs 저장소 정책. Box2D 2D-first. rollback/SaveState 분기 |
| §61, **§61.1 신설** | 강제 지점 3곳. C++ + 제한적 Luau 결정(근거, 경계, 탈락 대안, 도입 조건) |
| §62 | 결정성 lint 8종 |
| §64, §65, §66 | Chrome Trace JSON. Tracy 채택 + 백분위. Google Benchmark JSON + Mann–Whitney + noise threshold |
| §67, **§67.1 신설** | 8개 시스템 비교표 + Godot 대응표 |
| §72, §73 | 5개 arm, 지표 우선순위 재배치(rubric 1순위, tool call 보조), 기대 규모 41–55%. 중단 기준 3개 추가 |
| §74 | **Phase -1 신설**, Phase 1 대폭 확장(reflection/ID/데이터 모델/Box2D/FP flags), Phase 3/4/5/7 구체화 |
| §77, **§78 재작성, §78.1 신설** | `optional<ChangeSet>`. RFC 6902 superset ChangeSet 스키마 + 8규칙 + Command→ops 매핑표 |
| §79 | SARIF/LSP/rustc 합성 Diagnostic (physical + logical + applicability + fingerprint) |
| §80, §82, §84, §85 | 예시 갱신. 1st-party 현황. 설계 원칙 13–20 추가. 업계 수렴 문단 + PoC 질문 재정의 |
| §86 | Phase별 체크리스트 재구성 (사서 쓰는 것 표시, 옵션의 측정 조건) |
| §87 | 출처 6개 → 약 120개 (엔진/선행 사례, 벤치마크, MCP, CLI 지침, 표준, 결정론, reflection, 라이브러리) |
| **§88 신설** | 미결 설계 결정 11개와 권장 기본값 (목표 게임, 프로세스 모델, authoring/play world, 입력, 크래시, build, 보안, sub-asset, 직렬화 소비자, 파일 granularity, query DSL) |

보강에 쓰인 리서치 원문(11개 주제, finding 217개, 권고 118개)과 critic의 gap 분석, 보강 전 원본(v1)은 **공개 저장소에 포함하지 않는다**(엔진이 아닌 작업 자료; 작성자가 별도 보관). 출처 목록은 §87.

**▶ v3 (구현 중 수정, 2026-08-21)**: Phase 3 구현에서 설계를 구체화/수정한 곳. 각 항목의 근거는 `Docs/DECISIONS.md` ADR-0018~0022.

| § | v3 변경 |
|---|---|
| §78 규칙 1 | `"/arr/-"`(append) 포인터는 역연산이 없으므로 ChangeBuilder 가 **구체 인덱스로 치환**해 기록한다 (ADR-0019). |
| §78 규칙 2, §10.2 | `before` 와 현재 값의 비교, base 해시는 **JCS(RFC 8785) 기준**(키 순서 무시). 파일을 거친 문서는 §5.3 순서로 재정렬되기 때문 (ADR-0018). |
| §8, §78.1 | `component.add/remove`, `property.set` 의 `entity` 인자는 **prefab selector 도 받는다**. derived prefab(`base`)은 인스턴스와 같은 set/add/remove 규칙. `property.set` 이 base 값과 같은 값을 받으면 override 를 지운다 (ADR-0021). |
| §9.2 | commit 전 검증은 **새로 생긴 error 만** 거부한다(baseline fingerprint). `--no-validate` 로 우회 (ADR-0020). |
| §29, §79 | `validate --fix` = MachineApplicable fix 를 `apply`/`document.patch` 로 CommandBus 경유(undo 가능). canonical 재직렬화만 예외. `COMPONENT_DEPENDENCY_MISSING` fix 는 전이적 의존성을 모두 add (ADR-0022). |
| §8 | 내장 command 에 `document.patch {doc, ops}`(raw RFC 6902 escape hatch, copy 금지), `tag.add/remove`, `world.create` 추가. `entity.create` 는 Transform 을 자동 추가하고 누락 prop 을 default 로 채운다. |
| §49 | `$name` 은 해당 change 의 `result.id`(없으면 result 전체), `$name.field` 는 필드, `$$x` 는 리터럴. 비-atomic 은 각 change 를 개별 commit. |
| §10 | history 는 프로젝트당 선형 스택(`Cache/history/history.jsonl` + `cursor.json`). undo/redo 는 history 에 push 하지 않고 cursor 만 움직인다; 새 commit 이 redo 꼬리를 버린다. `--actor X` undo 는 최근 항목이 다른 actor 면 거부. |
| §23, §23.1 | 구현: `pme::expr::Expr`(CEL 부분집합 자체 evaluator). **undefined 멤버는 오류**(has() 밖). "tick N 의 snapshot" = N tick 을 돌린 뒤. `always` 첫 위반에서 run 중단 + 나머지 assert 는 중단 시점에 note 와 함께 평가. setup 에 `entity`(기존 entity binding) 추가, `spawn` 은 `set` 포인터 맵·`name`·`tags` 지원. `inputs.untilTick` 생략 시 다음 `release` 까지. enum 값은 reflection 문자열(소문자) (ADR-0023, 0024). |
| §22.2, §24 | run-twice 결정성: 어긋나면 A 를 divergent tick 까지 재실행해 snapshot diff(entity/path/a/b) + `firstDivergentSystem`. `threads` 는 1 (단일 스레드) (ADR-0025). `run.gpuBackend/projectRev` 는 미기록. |
| §20, §27, §27.1 | 구현: `--no-render`/`--offscreen` 대신 **`game run --headless`(SDL 미초기화)** 와 **software renderer capture**(SDL `dummy` driver + `SDL_CreateSoftwareRenderer`; 창·GPU 없이 byte-deterministic PNG). `offscreen` GL driver 와 WARP/SwiftShader 논의는 2D PoC 에서 불필요 (ADR-0026). 렌더 API 는 SDL_Renderer (ADR-0027). `videoDriver` 보고는 유지. 비교는 perPixel + maxMismatchRatio 만 (AA 제외/local window 미구현). |
| §20.1, §22.3 | 창 모드 `game run --record inputs.jsonl` 이 tick 당 InputFrame(JSONL) 을 쓰고 `--headless --replay` 가 같은 finalHash 를 냄을 확인 (90 tick). |
| §23, §24 | 테스트 파일에 `"requires": ["renderer"]` 추가 — renderer 가 없는 빌드에서 `skipped` (ADR-0028). capture assertion 은 `at` 에서만; golden 경로 `Tests/Golden/<test>/<golden>_<WxH>.png`, `--update-golden`. |
| §88.3 | `Config/input.json` 의 gamepad/mouse 바인딩은 파싱만 하고 `unsupported` 로 보고 (키보드만 구현). |
| §88.1, §9.1, §46.2 | 구현: `game serve` = loopback TCP **NDJSON JSON-RPC**(HTTP 아님) + per-session token(`Cache/serve.json`), 모든 `game <cmd>` 자동 포워딩(`--local` 로 우회), `--stdio`. tx = TTL 있는 opaque handle, 만료/미지 → `TX_UNKNOWN_OR_EXPIRED`; one-shot 에서 `--tx` → `TX_REQUIRES_SERVE`. `run.start` → `result.run` + `run status` (ADR-0029, 0031). named pipe·HTTP·watcher 는 미구현. |
| §46, §46.1, §47 | 구현: sidecar 대신 **`game mcp`(C++ 네이티브 stdio MCP)**. `server/discover`(2026-07-28) + `initialize`(2025-xx) 둘 다 응답. tools = `capabilities.tools[].enabled`, `tools/call` → `structuredContent` envelope + `isError`. resources/prompts/Tasks 미구현 (ADR-0030). |
| §15, §47 | `capabilities` 에 `busCommands[]`(Mutation command + args JSON Schema = `apply.changes[].op` 의 oneOf) 추가. `tools[]` 15개에 `enabled` 플래그. |

**검증 (2026-08-21)**: v2 추가 내용을 7개 에이전트가 리서치 원문·웹 1차 출처로 fact-check하고 전체 일관성을 점검했다 — 130건(high 2, medium 37, low 91) 중 거의 전부를 반영했다. 검증 목록은 저장소 밖의 작업 자료에 있고, 구현에 영향을 준 항목은 아래와 Docs/DECISIONS.md 에 남겼다. 구현에 영향을 준 수정: `game capabilities`의 tools[]/commands[] 2층 구조(§15/§47), §74 Phase 0 구성, ChangeSet `replace`의 `value` 필드(§78), §13 오류 객체 = §79 Diagnostic + α, command id 정식 명명(§8.1), `CommandKind::Meta`(§8), tx commit의 compose 규칙(§9.2).

---

# 마지막 한 줄

> **Unreal을 다시 만드는 게 아니라, AI가 게임을 만들기 위해 Unreal 같은 인간용 에디터를 우회할 필요가 없는 구조를 만든다.**
