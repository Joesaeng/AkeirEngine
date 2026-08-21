# 00 — START HERE

이 문서는 **이 저장소를 처음 보는 세션(사람이든 AI 에이전트든)** 을 위한 진입점이다.
이전 대화 내용, 메모리, 외부 맥락 없이 여기서부터 읽으면 프로젝트를 이해하고 이어서 작업할 수 있어야 한다.
그렇지 않다면 그것은 Docs 의 버그다 — 고치고 넘어간다.

## 1. 이 프로젝트는 무엇인가 (30초)

- **이름**: AKEIR Engine (에이키어; 그리스어 ἄχειρ "손이 없는" ← ἀχειροποίητος "사람 손으로 만들어지지 않은"). 실행 파일 `akeir.exe`. 저장소 디렉터리만 `Project_ME`(역사적 이름), C++ 네임스페이스·헤더 경로·CMake 타깃 접두어·매크로는 전부 `akeir`/`AKEIR_`. 첫 릴리즈 = git tag **`v0.1.0`** (ADR-0032).

- **목표**: AI 에이전트가 Unreal/Unity 같은 인간용 에디터를 MCP 로 원격 조종하는 대신, **텍스트 프로젝트 데이터(JSON)를 Command API 로 직접 수정하고 headless 로 실행·테스트**할 수 있는 개인용 C++ 게임 프레임워크.
- **차별점** (설계 문서 §0.1): ① 데이터 수준 Transaction/ChangeSet(self-inverting, dry-run, undo) ② 파일 위치 + 자동 적용 가능성이 붙은 구조화 진단 ③ 결정론 계약 + 데이터화된 테스트 + replay ④ persistent ID + canonical JSON ⑤ Editor/CLI/MCP/Test 가 같은 Command API 만 사용.
- **지금 단계**: PoC 구현 초기. 정확한 진행 상태는 [`STATUS.md`](STATUS.md).
- **목표 게임 (가정)**: 2D top-down arena — 플레이어 WASD 이동 + 근접 고블린, Windows 전용, 싱글플레이 (설계 문서 §71 시나리오 그대로). 근거와 상태는 [`DECISIONS.md`](DECISIONS.md) ADR-0001.

## 2. 읽는 순서

릴리즈 zip(`AKEIR-<ver>.zip`)을 푼 경우에는 **`QUICKSTART.md`** 부터 — 빌드 없이 `bin/akeir.exe` 로 바로 시작한다. 아래 순서는 소스 저장소에서 개발을 이어갈 때.

| 순서 | 문서 | 왜 |
|---|---|---|
| 1 | 이 문서 | 지도 |
| 2 | [`STATUS.md`](STATUS.md) | 어디까지 됐고 다음이 무엇인지. **매 작업 세션의 시작과 끝에 읽고 갱신한다.** |
| 3 | [`DECISIONS.md`](DECISIONS.md) | 이미 내려진 결정과 근거. 같은 질문을 다시 열지 않기 위해 |
| 4 | [`BUILD.md`](BUILD.md) | 빌드·테스트·실행. 복붙 가능한 명령 |
| 5 | [`ARCHITECTURE.md`](ARCHITECTURE.md) | 코드 모듈 ↔ 설계 문서 § 대응, 데이터 흐름 |
| 6 | [`CONVENTIONS.md`](CONVENTIONS.md) | 코드를 쓰기 전에 |
| 7 | [`../AKEIR.md`](../AKEIR.md) | 설계 정본. 4,900줄. **전부 읽을 필요는 없다** — 아래 "설계 문서 읽는 법" |

## 3. 설계 문서 읽는 법

`AKEIR.md` 는 §0–§89 로 되어 있고, `▶ v2` 표시가 붙은 부분이 리서치로 검증·보강된 내용이다.
작업하려는 모듈에 해당하는 § 만 읽으면 된다. 모듈별 대응은 [`ARCHITECTURE.md`](ARCHITECTURE.md) 에 있다. 반드시 읽어야 하는 것:

- **§0.1** — 리서치 후 이 문서의 위치 (무엇이 차별점이고 무엇이 아닌지)
- **§84** — 핵심 설계 원칙 20개
- **§88** — 미결 설계 결정 11개와 권장 기본값 (구현 중 가정을 둔 곳은 DECISIONS.md 에 기록되어 있다)
- **§74 / §86** — Phase 순서와 체크리스트 (STATUS.md 가 이것을 추적한다)

설계와 구현이 어긋나면 **설계 문서를 고치고 `▶ v3` 로 표시**한다. 코드 주석에 설계를 숨기지 않는다.

## 4. 저장소 지도

```
Project_ME/
├── AKEIR.md   설계 정본
├── README.md                            루트 진입점 (이 문서로 안내)
├── Docs/                                ← 지금 여기
├── .mcp.json                            Claude Code 용 MCP 서버 등록 — 개발 저장소에서는 절대 경로(msvc-debug 빌드), 릴리즈 zip 에서는 상대 경로 `bin\akeir.exe`. `akeir mcp --print-config` 가 절대 경로 버전을 출력한다
├── QUICKSTART.md                        릴리즈 zip 사용자용 진입점 (빌드 없이 bin/akeir.exe 로 시작)
├── CMakeLists.txt, CMakePresets.json    빌드 (§41). preset: msvc-debug / msvc-release / msvc-headless / clang-cl-debug
├── cmake/                               CPM.cmake(vendored), Dependencies.cmake(버전 고정), DetFpFlags.cmake(§22.2), ProjectWarnings.cmake
├── scripts/                             build.cmd / build.ps1 (VS 환경 잡아서 cmake 호출), fetch-deps.ps1
├── Engine/                              엔진 정적 라이브러리들 (§75). 각 디렉터리에 README.md
│   ├── Core/        ID·Hash·RNG·Time·Log·Diagnostic·Envelope·Crash·ExitCodes      [구현됨]
│   ├── Reflection/  PropertyMeta registry, AKEIR_REFLECT_* (§42.2)                    [구현됨]
│   ├── Serialization/ canonical JSON, JSON Schema, component↔JSON (§5.3, §14)       [구현됨]
│   ├── Runtime/     Project(authoring 문서 모델, validate), Application, 내장 component (§6, §20.1) [구현됨]
│   ├── ECS/         PlayWorld = Flecs 투영 + physics sync (§3.1)                    [구현됨]
│   ├── Physics/     PhysicsWorld + Box2D (§57)                                       [구현됨]
│   ├── Commands/    CommandBus / ChangeSet / Tx / History / apply (§8–§10, §49, §78) [구현됨, 유일한 쓰기 경로]
│   ├── Testing/     Test Scenario 러너 + assertion 표현식 (§23, §24)                 [구현됨]
│   ├── Platform/    SDL3 창/입력/창 모드 루프 (§20, §88.3)                          [구현됨, AKEIR_WITH_SDL]
│   ├── Render/      Renderer2D placeholder 스프라이트, software capture, golden 비교 (§27) [구현됨, AKEIR_WITH_SDL]
│   └── Validation/  rule registry, SARIF (§29)                                      [예정 — 지금은 Project::validate]
├── Tools/
│   ├── CLI/         `akeir` 실행 파일 (§11–§13, §15): 명령 ~45개(`akeir --help` / `capabilities --json` 이 정본) + `serve`(상주 RPC) + `mcp`(stdio MCP)   [구현됨]
│   ├── Editor/      ImGui (Phase 6)                                                 [없음]
│   └── MCP/         (없음 — `akeir mcp` 가 CLI 안에 있다, ADR-0030)
├── Game/            샘플 프로젝트 "TestArena" — project.json, Worlds/, Prefabs/, Config/, Tests/(*.test.json, Golden/) + Source/(Health/Movement/PlayerController/EnemyAI + systems)  [구현됨]
├── Tests/           doctest 단위 테스트 (akeir_tests.exe). 파일명 = <모듈>_<주제>.cpp
├── .cpm-cache/      의존성 소스 clone (gitignore; scripts/fetch-deps.ps1 로 재생성)
└── build/           CMake 출력 (gitignore)
```

## 5. 새 세션의 작업 절차

1. `STATUS.md` 를 읽는다 — 현재 Phase, 체크리스트, 알려진 문제, "다음 할 일".
2. `BUILD.md` 대로 빌드하고 `akeir_tests.exe` 가 전부 통과하는지 확인한다. 깨져 있으면 그것이 첫 작업이다.
3. 작업 대상 모듈의 설계 § 와 `Engine/<모듈>/README.md` 를 읽는다.
4. 구현 → 테스트 추가(`Tests/`) → 빌드 → 테스트.
5. **끝내기 전에** `STATUS.md` 를 갱신한다 (완료 항목 체크, 새로 알게 된 문제, 다음 할 일). 결정을 내렸으면 `DECISIONS.md` 에 ADR 추가. 설계를 바꿨으면 설계 문서에 `▶ v3`.
6. git: 공개 저장소 https://github.com/Joesaeng/AkeirEngine, 태그 `v0.1.0` 이 있다. **이후 커밋은 사용자가 지시할 때만** 한다 (STATUS.md). 릴리즈 zip 은 `git archive` 라 `.git` 이 없다.

## 6. 자주 쓰는 명령

```bash
scripts\build.cmd msvc-headless all          # configure + build (SDL 없이, 빠름)
scripts\build.cmd msvc-debug all             # 전체 (SDL3 포함, 첫 빌드 수 분)
build\msvc-headless\Tests\akeir_tests.exe      # 단위 테스트
build\msvc-headless\bin\akeir.exe version --json
build\msvc-headless\bin\akeir.exe capabilities --json
build\msvc-headless\bin\akeir.exe debug crash-test        # exit 6 + Cache/crash/*.dmp
build\msvc-headless\bin\akeir.exe debug hang-test --timeout 2s   # exit 7
cd Game && ..\build\msvc-headless\bin\akeir.exe run --headless --ticks 600 --json   # 결정론 실행 (finalHash)
cd Game && ..\build\msvc-headless\bin\akeir.exe set name:Goblin_01 Health.max 45 --dry-run --json   # 쓰기 명령 (dry-run)
cd Game && ..\build\msvc-headless\bin\akeir.exe test --json                                        # 데이터화 테스트 (§23)
cd Game && ..\build\msvc-debug\bin\akeir.exe run --json                                              # 창 모드 (SDL 빌드)
cd Game && ..\build\msvc-debug\bin\akeir.exe capture --ticks 300 --out Cache\capture\f.png --json      # PNG capture (§27)
cd Game && ..\build\msvc-headless\bin\akeir.exe serve                                                # 상주 프로세스 (다른 창의 game 명령이 포워딩됨)
cd Game && ..\build\msvc-headless\bin\akeir.exe mcp                                                  # MCP 서버 (stdio)
```

## 7. 핵심 불변 조건 (깨지면 설계가 깨진 것)

- authoring JSON(`Game/`)이 source of truth. Flecs/Box2D 는 투영이다 (§88.2).
- 파일을 바꾸는 경로는 `CommandBus`(ChangeSet 을 남김)와 `akeir fmt`(canonical 재직렬화)뿐이다.
- 모든 ChangeSet 은 self-inverting 이다 — undo 뒤 파일은 byte-identical 이어야 한다 (Tests/Commands_Bus.cpp 가 확인).
- `akeir run --headless` 는 같은 입력에 같은 `finalHash` 를 낸다 (T0). `akeir test` 의 run-twice 가 이것을 시나리오마다 확인한다.
- 출력은 항상 §12 envelope, 오류는 §79 Diagnostic 형태 + `ruleId`.
- 골든 이미지는 software renderer(`akeir capture` / `akeir test --update-golden`)로만 만든다 — 창(GPU) 픽셀은 비교에 쓰지 않는다 (ADR-0026).
- 프로젝트의 writer 는 하나다: `akeir serve` 가 떠 있으면 그것, 아니면 각 one-shot 명령. CLI·MCP·(Editor) 는 전부 같은 `CommandSpec` 표와 `ServeHost` 를 쓴다.
