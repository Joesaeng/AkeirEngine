# Contributing

AKEIR is a personal research engine; issues and pull requests are welcome, but the design principles below are not up for negotiation per PR — open a discussion first if a change touches them.

## Principles (see `AKEIR.md` §0.1, `Docs/DECISIONS.md`)
1. Authoring JSON under the project is the source of truth; ECS/physics are projections.
2. Every persistent mutation goes through `CommandBus` and leaves a ChangeSet (undoable). The only exceptions are `akeir fmt` (canonical re-serialization) and migrations.
3. Everything works headless; the CLI, `serve` and `mcp` share the same command table. No MCP- or editor-specific logic in the engine core.
4. Determinism: same inputs → same `finalHash`. Sim code never reads wall-clock time or unordered containers.

## Workflow
- Read `Docs/00-START-HERE.md`, then `Docs/STATUS.md` (what exists, what's next) and `Docs/CONVENTIONS.md` (naming, no `;` in test names, LF-only JSON).
- Build: `scripts\build.cmd msvc-headless all` (fast, no SDL) or `msvc-release all` (SDL). Tests: `build\<preset>\Tests\akeir_tests.exe` and `cd Game && ..\build\<preset>\bin\akeir.exe test --json`.
- Reference determinism value: `akeir run --headless --ticks 600 --json` in `Game/` → `0x404c60567ccb9e85`. If your change moves it, say why in the PR.
- Add a unit test under `Tests/` and, for behaviour visible to AI clients, a line in the relevant `Engine/*/README.md` or `Tools/CLI/README.md`.
- Decisions → ADR in `Docs/DECISIONS.md`; design changes → mark `▶ v3` in `AKEIR.md`.

## Commits
Conventional prose is fine; keep the subject under 72 characters. Don't commit `build/`, `Cache/`, `Tests/.results/`, or `.mcp.json` (use `.mcp.json.example`).

## License
By contributing you agree your contribution is licensed under the MIT License in `LICENSE`.
