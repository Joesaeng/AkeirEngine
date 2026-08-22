# Tools/Player — the game executable (`bin/<ProjectName>.exe`)

What a person double-clicks. `Player.cpp` is the windowed `akeir run` without the terminal: it finds the project, builds the play world with the Game/ systems, opens an SDL window and runs the fixed-tick loop (`runInteractive`) until the window is closed.

- **Name**: taken from `Game/project.json` → `name` (`TestArena.exe` for the sample; `CatSurvivor.exe` for a project called CatSurvivor). Built only with `AKEIR_WITH_SDL=ON`. **A Debug build is named `<Name>-debug.exe`** — it simulates 10–15× slower; play and ship `msvc-release` (ADR-0042).
- **Profile**: the `player.stop` line in `Cache/player.log` carries frame-time avg/p95/max and the PlayWorld profile (per-system ms, physics, queries) — read it after a slow session (ADR-0044).
- **Project lookup**: `--project DIR` → `project.json` next to the exe (shipped-folder layout) → walk up from the exe directory for `Game/project.json` / `project.json` (repo and zip layouts) → the current directory.
- **No console**: Windows GUI subsystem with a plain `main()` (`/ENTRY:mainCRTStartup`). Logs go to `<project>/Cache/player.log`; fatal errors show a message box; crash dumps go to `<project>/Cache/crash/player-*.dmp`.
- **Arguments** (optional): `--ticks N` auto-closes after N ticks, `--width/--height`, `--video-driver dummy` (headless smoke: CI runs `TestArena.exe --ticks 30 --video-driver dummy`), `--record inputs.jsonl` (replayable with `akeir run --headless --replay`), `--world <selector>`, `--seed N`.
- Rebuilding while the game is running works like the CLI: the locked exe is moved aside (`cmake/UnlockExe.cmake`). In the release-zip layout a `msvc-release` build refreshes `<root>/bin/<ProjectName>.exe` (`cmake/InstallToBin.cmake`).

Not implemented: a shipping/packaging step that copies the exe + project data into a standalone folder (`scripts/package.py` only builds the engine zip), command-line-free settings (resolution, fullscreen) — add them to `project.json` when a game needs them.
