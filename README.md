# AKEIR Engine

[![CI](https://github.com/Joesaeng/AkeirEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/Joesaeng/AkeirEngine/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/Joesaeng/AkeirEngine)](https://github.com/Joesaeng/AkeirEngine/releases)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

**AKEIR is an AI-native C++ game engine where project data is text-first, every edit is a reversible command, and games can be built and verified headlessly — without relying on human-authored editor workflows.**

- **Text-first authoring** — worlds, prefabs and config are canonical JSON; the files are the source of truth, ECS/physics are projections.
- **Single reversible Command API** — every mutation goes through one `CommandBus` and returns a self-inverting ChangeSet: undo/redo, multi-call transactions, dry-run, structured diagnostics with machine-applicable fixes.
- **Deterministic headless testing** — fixed tick, explicit seed, same input → same `finalHash`; tests are data files with snapshot assertions, run-twice determinism checks and golden captures.
- **CLI / MCP as adapters** — `akeir <cmd>`, `akeir serve` (resident process) and `akeir mcp` (MCP server, 15 tools) share one command table over the same engine.

> **AKEIR** ← Greek ἄχειρ (*acheir*, "without hands"), from ἀχειροποίητος ("not made by human hands"). Pronounced *ay-KEER*.

## Start

**Release zip** (no compiler needed): download from [Releases](https://github.com/Joesaeng/AkeirEngine/releases), unpack, read `QUICKSTART.md` — `bin\akeir.exe` is prebuilt for Windows x64.

**From source** (Windows, VS2022 17.14, bundled CMake/Ninja; dependencies are fetched by CPM on first configure):

```bash
scripts\build.cmd msvc-headless all            # no SDL: fastest; `msvc-release all` builds the SDL window/capture too
build\msvc-headless\Tests\akeir_tests.exe
cd Game
..\build\msvc-headless\bin\akeir.exe run --headless --ticks 600 --json    # result.finalHash = 0x404c60567ccb9e85
..\build\msvc-headless\bin\akeir.exe set name:Goblin Movement.speed 4.5   # edit a prefab (all instances), undoable
..\build\msvc-headless\bin\akeir.exe test --json                          # Tests/**/*.test.json
..\build\msvc-headless\bin\akeir.exe mcp                                  # MCP server over stdio
```

An SDL build (`scripts\build.cmd msvc-release all`) also produces the **game executable** `build\msvc-release\bin\TestArena.exe` (named after `Game/project.json`) — double-click to play, no terminal.

MCP for Claude Code: `copy .mcp.json.example .mcp.json` (or `akeir mcp --print-config > .mcp.json` for absolute paths).

## Documentation

| What | Where |
|---|---|
| New session? Read first | [`Docs/00-START-HERE.md`](Docs/00-START-HERE.md) |
| Current state / next work | [`Docs/STATUS.md`](Docs/STATUS.md) |
| Decisions (ADR-0001…) | [`Docs/DECISIONS.md`](Docs/DECISIONS.md) |
| Build · test · run | [`Docs/BUILD.md`](Docs/BUILD.md) |
| Code ↔ design sections | [`Docs/ARCHITECTURE.md`](Docs/ARCHITECTURE.md) |
| Conventions | [`Docs/CONVENTIONS.md`](Docs/CONVENTIONS.md) |
| Design document (§0–§89) | [`AKEIR.md`](AKEIR.md) |
| Contributing · licenses | [`CONTRIBUTING.md`](CONTRIBUTING.md) · [`LICENSE`](LICENSE) (MIT) · [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) |

The repository directory is still called `Project_ME` on the author's machine (historical); code uses `akeir::`, headers `akeir/…`, CMake targets `akeir_*`, macros `AKEIR_*`. `Docs/` and the design spec `AKEIR.md` are written in Korean (the author's working language); the READMEs, QUICKSTART, the CLI, error messages and schemas are English.
