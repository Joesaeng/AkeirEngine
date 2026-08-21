# MoltEngine (ME) 0.1 — AI-Native Game Framework

> 저장소 디렉터리명은 `Project_ME`, 엔진 이름은 **MoltEngine**, 코드 네임스페이스/타깃 접두어는 `pme`. 릴리즈 태그 `ME0.1` = `v0.1.0`.

AI(코딩 에이전트)가 인간용 에디터를 거치지 않고 **텍스트 프로젝트 데이터 + Command API + headless 실행**만으로 게임을 만들 수 있게 하는 개인용 C++ 게임 프레임워크 PoC.

> **처음 온 세션은 [`Docs/00-START-HERE.md`](Docs/00-START-HERE.md) 부터 읽는다.**
> 이 저장소는 이전 대화 맥락 없이도 Docs와 코드만으로 이어서 작업할 수 있도록 쓰여 있다.

| 무엇 | 어디 |
|---|---|
| 설계 문서 (정본, §0–§89) | [`AI_Native_Game_Framework_Design.md`](AI_Native_Game_Framework_Design.md) |
| 읽는 순서 / 저장소 지도 | [`Docs/00-START-HERE.md`](Docs/00-START-HERE.md) |
| 내려진 결정 (ADR) | [`Docs/DECISIONS.md`](Docs/DECISIONS.md) |
| 현재 진행 상태 / 다음 할 일 | [`Docs/STATUS.md`](Docs/STATUS.md) |
| 빌드·테스트·실행 명령 | [`Docs/BUILD.md`](Docs/BUILD.md) |
| 코드 ↔ 설계 § 대응 | [`Docs/ARCHITECTURE.md`](Docs/ARCHITECTURE.md) |
| 코딩 규약 | [`Docs/CONVENTIONS.md`](Docs/CONVENTIONS.md) |
| 리서치 원문 / 검증 결과 | [`research/README.md`](research/README.md) |

빠른 시작 (Windows, VS2022):

```bash
scripts\build.cmd msvc-headless all
build\msvc-headless\Tests\pme_tests.exe
build\msvc-headless\bin\game.exe version --json
```

상태 요약 (2026-08-21): Phase 0·1·2·3·4·5 와 MCP 서버(Phase 7 일부)까지 구현됨 — authoring JSON → reflection → Flecs/Box2D play world, Command/ChangeSet/undo, 데이터화 테스트, SDL3 창·software capture, `game serve`(상주 RPC), `game mcp`(stdio MCP). 자세한 것은 `Docs/STATUS.md`.

```bash
cd Game
..\build\msvc-headless\bin\game.exe run --headless --ticks 600 --json     # 결정론 실행 (finalHash 0xbc23e49a65efb2e8)
..\build\msvc-headless\bin\game.exe set name:Goblin Movement.speed 4.5     # 모든 고블린 속도 (prefab 편집, undo 가능)
..\build\msvc-headless\bin\game.exe test --json                            # Tests/**/*.test.json
..\build\msvc-headless\bin\game.exe mcp                                    # MCP 서버 (stdio)
```
