# Engine/Platform (`akeir_platform`) — SDL3 window · input · windowed loop

Design doc §20 (SDL3 dummy/offscreen driver), §20.1 (headless has no accumulator — the accumulator exists only in windowed mode), §88.3 (input = `Config/input.json` action map → `InputFrame`), §22.3 (input recording → replay).
**Compiled only in `AKEIR_WITH_SDL=ON` builds (preset `msvc-debug`).** It is absent from `msvc-headless`, where the corresponding CLI commands return `FEATURE_UNAVAILABLE`.

| File | Contents |
|---|---|
| `include/akeir/platform/Platform.h`, `src/Platform.cpp` | `Platform::init(PlatformConfig{videoDriver, window, width, height, title})` — sets `SDL_HINT_VIDEO_DRIVER` + `SDL_Init(VIDEO\|EVENTS)` + (optionally) a window. `currentVideoDriver()` reports the driver actually in use. `pumpEvents()` = QUIT/ESC/resize |
| `include/akeir/platform/InputMap.h`, `src/InputMap.cpp` | `InputMap::loadFile("Config/input.json")` → scancode bindings per action. `sample(tick)` = `SDL_GetKeyboardState` → `InputFrame{actions}` (actions at 0 are omitted). axis = sum of the pressed keys' scales clamped to [-1,1], button = 1.0. Gamepad/mouse bindings are parsed only and reported as `unsupported` |
| `include/akeir/platform/Interactive.h`, `src/Interactive.cpp` | `runInteractive(platform, renderer, world, input, cfg)` — a wall-clock accumulator decides how many fixed ticks to run, then `renderer.render/present` every frame. `maxFrameSeconds` (0.25) prevents the spiral of death. With `recordInputsPath`, one `InputFrame.toJson()` line per tick (JSONL) — the same format `akeir run --headless --replay` reads |

## Invariants

- The sim knows nothing about Platform. Windowed and headless differ only in *when* they tick and in rendering. **Inputs recorded with `--record` in windowed mode, replayed headless with `--replay`, produce the same finalHash** (verified 2026-08-21: 90 ticks, `0xafcd091ec8be292a`).
- Key names are SDL scancode names (`SDL_GetScancodeFromName`: "A", "Left", "Space", "Up"). Unknown names produce an `INPUT_KEY_UNKNOWN` warning.
- One `Platform` per process (SDL is global). For capture/test the CLI keeps a single static windowless instance on the `dummy` driver (`SdlCommands.cpp::headlessSdl`).

## Not implemented

- Gamepad / mouse bindings, an input remapping UI, camera aspect correction on window resize (the renderer only uses the vertical orthoSize), the `offscreen` driver path (§20 — the software renderer covers it, ADR-0026), audio.
