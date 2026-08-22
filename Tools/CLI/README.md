# Tools/CLI (`akeir.exe`)

The first-class interface shared by AI agents and humans (§11). Every output is a §12 envelope (JSON); exit codes follow the §13 table (`akeir/core/ExitCodes.h`).
Two execution models (ADR-0011, 0029): **one-shot** (the project is opened and closed per call) and **serve forwarding** (RPC to the daemon named in `Cache/serve.json`). The command code is the same — with `Context::resident/residentBus` it uses the resident state, otherwise the disk.

## Files

| File | Contents |
|---|---|
| `src/main.cpp` | parseArgs → crash handler (`Cache/crash`) → `--timeout` watchdog → findCommand → run → envelope output → exit code. C++ exceptions become `INTERNAL_ERROR` (exit 1) |
| `src/Args.h/.cpp` | `--key value` / `--key=value` / flags. Options that take a value must be listed in `kValueOptions` (otherwise they are treated as flags) |
| `src/Commands.h/.cpp` | the `CommandSpec{id, cli spelling, kind, …, run}` table, `findCommand` (longest prefix match), `capabilitiesJson()` (15 tools + commands + busCommands + exitCodes + errorCodes), version / debug crash-test / hang-test |
| `src/ProjectCommands.cpp` | reads: `project info`, `validate [--fix]`, `fmt`, `schema` (`component <Name>`, `--all`, `test` = scenario schema + expression reference), `entity list`, `explain`, `refs`. Shared `openProject()` |
| `src/InitCommand.cpp` | `project init <name> [--dir D] [--tick-rate] [--seed] [--force]` — creates an empty project (works without a project; not forwarded to serve) |
| `src/RunCommands.cpp` | play world: `run --headless`, `dump`, `query` |
| `src/MutationCommands.cpp` | writes (all through `CommandBus`): `entity create\|delete\|rename\|reparent`, `component add\|remove`, `set`, `tag add\|remove`, `prefab create\|instantiate`, `world create`, `asset import`, `apply`, `undo\|redo\|history`, `cmd` |
| `src/TestCommands.cpp` | `test [filter] [--junit f] [--results-dir d] [--no-artifacts] [--update-golden] [--list]` — the `Engine/Testing` runner, exit 3 on failure. SDL builds inject the capture hook. `test explain "<expr>" [--snapshot f --as name=id]` parses/evaluates an assertion expression (ADR-0039) |
| `src/SdlCommands.cpp` | `capture`, `input map`, windowed `run` (without `--headless`), `installCaptureHooks`. Without `AKEIR_HAS_SDL` everything returns `FEATURE_UNAVAILABLE` |
| `src/Serve.h/.cpp` | `ServeHost` (resident Project + single CommandBus + run registry, JSON-RPC dispatch), `akeir serve` (Winsock loopback NDJSON + token / `--stdio`), `tryRemote` (thin client), `serve status\|stop` |
| `src/Mcp.cpp` | the MCP **worker** (`akeir mcp --worker`) — a stdio MCP server on top of ServeHost (server/discover, initialize, tools/list, tools/call) |
| `src/McpAdapter.cpp` | `akeir mcp` — the relay the client actually starts: spawns the worker from `akeir.exe`, forwards NDJSON both ways, and replaces the worker with the rebuilt `akeir.exe` between requests (`MCP_WORKER_RESTARTED` note). ADR-0034 |
| `src/ExeInfo.h/.cpp` | `ownExePath`, `exeStamp`, `fileSha256`, `ownExeInfoJson` — which build is running (`version`, `capabilities.info.exe`, `serve.json.exeSha256` → `SERVE_STALE_EXE`) |
| `src/MutationCommands.cpp` (tx) | `tx begin\|commit\|rollback\|list` — only on serve's bus (`TX_REQUIRES_SERVE`) |

## Rules

- stdout = exactly one envelope. Pretty-printed on a TTY, one line otherwise; `--json` forces it. Logs go to stderr as JSONL.
- The project is `--project <dir>` or found by walking up from the cwd until a `project.json` appears.
- Options shared by write commands: `--dry-run` (§50), `--no-validate`, `--actor <id>`, `--idempotency-key <k>` (apply). In a success envelope, `changes[]` holds the ChangeSet ops, `meta.changeSet` the id, `meta.committed` whether it was actually saved.
- Write commands recover `Cache/journal` before running (§9.2). If something was recovered, `warnings[]` carries a `JOURNAL_RECOVERED` note. serve does this once at startup.
- With a daemon running, `meta.via = "serve"`. `--local` ignores the daemon. `serve`, `mcp`, `capabilities`, `version` and `serve status|stop` are never forwarded.
- Under `akeir mcp` / `akeir serve --stdio`, stdout is the protocol channel — logs only ever go to stderr.
- Rebuilding while `akeir mcp`/`serve` runs is fine: the link step moves the locked `akeir.exe` aside (`cmake/UnlockExe.cmake`). The MCP worker is restarted from the new file on the next request; a daemon keeps the old build and warns `SERVE_STALE_EXE` (restart it). `scripts/test_resident_rebuild.py` checks the whole loop.
- `akeir cmd <id> --args '{json}'` calls any command that has no CLI sugar (e.g. `document.patch`). Ids and argument schemas: `akeir capabilities --json` → `result.busCommands[]`.
- Value arguments (`akeir set … <value>`) are parsed as JSON first and fall back to a string. `--position x,y,z` and `--tags a,b` are comma lists.
- In Windows cmd, escape the quotes in JSON arguments like `"{\"a\":1}"`. PowerShell: `'{"a":1}'`.

## Examples

See `Docs/BUILD.md`: the CLI smoke test, the Phase 1 check with the sample project, and the Phase 3 check with the write commands.

## Not implemented

- Human-oriented text output (`--format table`), `--fields`/`--jq` projection, pagination (`--cursor`): the options are parsed but have no effect (Phase 4).
- The `capture` tool is enabled only in SDL builds (headless builds return `FEATURE_UNAVAILABLE`).
- serve: multi-threading/concurrent connections, a file watcher, a resident play world (`run.step`), HTTP/named pipes. MCP: resources, prompts, progress/Tasks, argument schema validation.
- The CLI links Game/'s component/system registration directly (`game::registerGameComponents/Systems`) — making the Game module swappable is a Phase 4 task.
