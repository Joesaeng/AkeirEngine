# Tools/CLI (`akeir.exe`)

AI 에이전트와 사람이 같이 쓰는 1급 인터페이스 (§11). 모든 출력은 §12 envelope(JSON), exit code 는 §13 표(`pme/core/ExitCodes.h`).
두 가지 실행 모델 (ADR-0011, 0029): **one-shot**(호출마다 프로젝트를 열고 닫음) 과 **serve 포워딩**(`Cache/serve.json` 이 가리키는 데몬에 RPC). 명령 코드는 같다 — `Context::resident/residentBus` 가 있으면 상주 상태를, 없으면 디스크를 쓴다.

## 파일

| 파일 | 내용 |
|---|---|
| `src/main.cpp` | parseArgs → crash handler(`Cache/crash`) → `--timeout` watchdog → findCommand → run → envelope 출력 → exit code. C++ 예외는 `INTERNAL_ERROR`(exit 1) |
| `src/Args.h/.cpp` | `--key value` / `--key=value` / 플래그. 값을 받는 옵션은 `kValueOptions` 목록에 있어야 한다 (아니면 플래그로 취급) |
| `src/Commands.h/.cpp` | `CommandSpec{id, cli 철자, kind, …, run}` 테이블, `findCommand`(가장 긴 prefix 매치), `capabilitiesJson()`(tools 15 + commands + busCommands + exitCodes + errorCodes), version / debug crash-test / hang-test |
| `src/ProjectCommands.cpp` | 읽기: `project info`, `validate [--fix]`, `fmt`, `schema`, `entity list`, `explain`, `refs`. 공용 `openProject()` |
| `src/InitCommand.cpp` | `project init <name> [--dir D] [--tick-rate] [--seed] [--force]` — 빈 프로젝트 생성 (프로젝트 없이 동작, serve 로 포워딩되지 않음) |
| `src/RunCommands.cpp` | play world: `run --headless`, `dump`, `query` |
| `src/MutationCommands.cpp` | 쓰기(전부 `CommandBus` 경유): `entity create|delete|rename|reparent`, `component add|remove`, `set`, `tag add|remove`, `prefab create|instantiate`, `world create`, `apply`, `undo|redo|history`, `cmd` |
| `src/TestCommands.cpp` | `test [filter] [--junit f] [--results-dir d] [--no-artifacts] [--update-golden] [--list]` — `Engine/Testing` 러너, exit 3 on failure. SDL 빌드에서는 capture hook 주입 |
| `src/SdlCommands.cpp` | `capture`, `input map`, 창 모드 `run`(`--headless` 없을 때), `installCaptureHooks`. `PME_HAS_SDL` 이 없으면 전부 `FEATURE_UNAVAILABLE` |
| `src/Serve.h/.cpp` | `ServeHost`(상주 Project + 단일 CommandBus + run registry, JSON-RPC dispatch), `akeir serve`(Winsock loopback NDJSON + token / `--stdio`), `tryRemote`(얇은 클라이언트), `serve status|stop` |
| `src/Mcp.cpp` | `akeir mcp` — ServeHost 위 stdio MCP 서버 (server/discover, initialize, tools/list, tools/call) |
| `src/MutationCommands.cpp` (tx) | `tx begin|commit|rollback|list` — serve 의 bus 에서만 (`TX_REQUIRES_SERVE`) |

## 규칙

- stdout = envelope 한 개. TTY 면 pretty, 아니면 한 줄. `--json` 으로 강제. 로그는 stderr JSONL.
- 프로젝트는 `--project <dir>` 또는 cwd 에서 위로 올라가며 `project.json` 을 찾는다.
- 쓰기 명령 공통 옵션: `--dry-run`(§50), `--no-validate`, `--actor <id>`, `--idempotency-key <k>`(apply). 성공 envelope 의 `changes[]` 가 ChangeSet ops, `meta.changeSet` 이 id, `meta.committed` 가 실제 저장 여부.
- 쓰기 명령은 실행 전에 `Cache/journal` 을 복구한다(§9.2). 복구가 있었으면 `warnings[]` 에 `JOURNAL_RECOVERED` note. serve 는 시작 시 한 번.
- serve 가 떠 있으면 `meta.via = "serve"`. `--local` 은 데몬을 무시한다. `serve`, `mcp`, `capabilities`, `version`, `serve status|stop` 은 포워딩하지 않는다.
- `akeir mcp` / `akeir serve --stdio` 에서는 stdout 이 프로토콜 채널이다 — 로그는 stderr 로만.
- `akeir cmd <id> --args '{json}'` 로 CLI sugar 가 없는 command(`document.patch` 등)도 호출할 수 있다. id 와 인자 스키마는 `akeir capabilities --json` → `result.busCommands[]`.
- 값 인자(`akeir set … <value>`)는 JSON 으로 파싱을 시도하고 실패하면 문자열. `--position x,y,z`, `--tags a,b` 는 콤마 목록.
- Windows cmd 에서 JSON 인자는 `"{\"a\":1}"` 처럼 큰따옴표를 escape 한다. PowerShell 은 `'{"a":1}'`.

## 예

Docs/BUILD.md 의 "CLI 스모크 테스트", "샘플 프로젝트로 Phase 1 확인", "쓰기 명령으로 Phase 3 확인" 절.

## 미구현

- 사람용 텍스트 출력 포맷(`--format table`), `--fields`/`--jq` 투영, 페이지네이션(`--cursor`) 은 옵션만 파싱되고 동작하지 않는다 (Phase 4).
- `capture` tool 은 SDL 빌드에서만 enabled (헤드리스 빌드는 `FEATURE_UNAVAILABLE`).
- serve: 멀티스레드/동시 연결, 파일 watcher, play world 상주(`run.step`), HTTP/named pipe. MCP: resources, prompts, progress/Tasks, 인자 스키마 검증.
- Game/ 의 component/system 등록을 CLI 가 직접 링크한다 (`game::registerGameComponents/Systems`) — Game 모듈 교체는 Phase 4 과제.
