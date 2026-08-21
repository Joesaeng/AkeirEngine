# Engine/Platform (`pme_platform`) — SDL3 창 · 입력 · 창 모드 루프

설계 문서 §20 (SDL3 dummy/offscreen driver), §20.1 (headless 는 accumulator 없음 — accumulator 는 창 모드에만), §88.3 (입력 = `Config/input.json` action map → `InputFrame`), §22.3 (입력 기록 → replay).
**`PME_WITH_SDL=ON` 빌드(preset `msvc-debug`)에서만 컴파일된다.** `msvc-headless` 에는 없고, CLI 의 해당 명령은 `FEATURE_UNAVAILABLE` 을 돌려준다.

| 파일 | 내용 |
|---|---|
| `include/pme/platform/Platform.h`, `src/Platform.cpp` | `Platform::init(PlatformConfig{videoDriver, window, width, height, title})` — `SDL_HINT_VIDEO_DRIVER` 설정 + `SDL_Init(VIDEO|EVENTS)` + (선택) 창. `currentVideoDriver()` 로 실제 driver 보고. `pumpEvents()` = QUIT/ESC/resize |
| `include/pme/platform/InputMap.h`, `src/InputMap.cpp` | `InputMap::loadFile("Config/input.json")` → action 별 scancode 바인딩. `sample(tick)` = `SDL_GetKeyboardState` → `InputFrame{actions}` (0 인 action 은 생략). axis = 눌린 키 scale 합 clamp[-1,1], button = 1.0. gamepad/mouse 바인딩은 파싱만 하고 `unsupported` 로 보고 |
| `include/pme/platform/Interactive.h`, `src/Interactive.cpp` | `runInteractive(platform, renderer, world, input, cfg)` — wall clock accumulator 로 fixed tick 을 몇 번 돌릴지 정하고 매 프레임 `renderer.render/present`. `maxFrameSeconds`(0.25) 로 spiral-of-death 방지. `recordInputsPath` 면 tick 마다 `InputFrame.toJson()` 한 줄(JSONL) — `akeir run --headless --replay` 입력 형식과 같다 |

## 불변 조건

- sim 은 Platform 을 모른다. 창 모드와 headless 의 차이는 "언제 tick 하는가" 와 렌더뿐이다. **창 모드에서 `--record` 한 입력을 headless 로 `--replay` 하면 같은 finalHash** 가 나온다 (2026-08-21 확인: 90 tick, `0xafcd091ec8be292a`).
- 키 이름은 SDL scancode 이름(`SDL_GetScancodeFromName`: "A", "Left", "Space", "Up"). 모르는 이름은 `INPUT_KEY_UNKNOWN` 경고.
- `Platform` 은 프로세스당 하나(SDL 전역). CLI 는 capture/test 용으로 `dummy` driver + 창 없음 인스턴스를 static 으로 하나 든다 (`SdlCommands.cpp::headlessSdl`).

## 미구현

- gamepad / mouse 바인딩, 입력 리매핑 UI, 창 크기 변경에 따른 카메라 종횡비 보정(렌더러가 세로 기준 orthoSize 만 쓴다), `offscreen` driver 경로(§20 — software renderer 가 대신한다, ADR-0026), 오디오.
