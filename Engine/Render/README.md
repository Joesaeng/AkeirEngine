# Engine/Render (`pme_render`) — 2D placeholder 렌더러 · PNG capture · golden 비교

설계 문서 §27 (capture 는 1급 기능), §27.1 (vision 검사용 capture vs golden 회귀), §20 (headless 에서의 capture). **`PME_WITH_SDL=ON` 빌드(preset `msvc-debug`)에서만.**

| 파일 | 내용 |
|---|---|
| `include/pme/render/Renderer2D.h`, `src/Renderer2D.cpp` | `Renderer2D::createForWindow(SDL_Window*)`(가속 SDL_Renderer, vsync) / `createSoftware(w, h)`(SDL_Surface + `SDL_CreateSoftwareRenderer` — **창/GPU 불필요**). `render(world)` → `RenderStats{sprites, backend, camera}`. `savePng(path)`(`SDL_SavePNG`), `readPixels()`. `compareCaptures(expected, actual, CaptureTolerance{perPixel, maxMismatchRatio}, diffPngOut)` |

## 무엇을 그리는가 (Phase 2 PoC)

- `SpriteRenderer` 가 있는 entity 를 `Transform.position`(x, y) 에 `tint` 색으로. 크기 = `Collider2D`(box: size, circle/capsule: radius×2) 또는 1×1 unit, × `Transform.scale`. circle 은 스캔라인 사각형으로 근사(결정적). 텍스처(`sprite` Ref)는 아직 로드하지 않는다.
- 정렬: `sortingOrder` 오름차순, 같으면 entity id 순 (결정적).
- 카메라: `Camera2D.primary` 인 첫 entity(id 순)의 `Transform.position` 중심, `orthoSize` = 세로 반높이(world 단위), `background`. 없으면 원점·10. Y 는 위가 +.
- **software 타깃은 CPU rasterizer** 라 같은 world → 같은 PNG 바이트 (테스트 `Render_Capture.cpp` 가 확인). 창 타깃(direct3d11 등)의 픽셀은 골든 비교에 쓰지 않는다 (§27.1: 골든은 같은 rasterizer 로).
- `render()` 끝에 `SDL_FlushRenderer` — software renderer 는 배치를 flush 해야 surface 에 픽셀이 있다.

## 비교 (§27.1 단순화)

pixelmatch 의 축소판: 픽셀의 채널 최대 차이(0~255)가 `perPixel×255` 를 넘으면 mismatch, `mismatched/total > maxMismatchRatio` 면 실패. `diffPngOut` 이 있으면 mismatch 는 빨강, 나머지는 expected 를 밝게 한 이미지. 크기가 다르면 `sizeMismatch`. AA 제외·local error window 는 미구현.

## 쓰는 곳

- `game capture [--ticks N] [--width W --height H] [--out f.png] [--compare golden.png --diff d.png]` (Tools/CLI/SdlCommands.cpp)
- `game test` 의 capture assertion (`TestRunnerOptions.capture/compare` hook) — golden 은 `Tests/Golden/<test>/<golden>_<WxH>.png`, `--update-golden` 으로 생성/갱신
- `game run` 창 모드 (createForWindow)

## 미구현

SDL_GPU 경로(§3 "SDL_GPU 우선" — PoC 는 SDL_Renderer 로 충분, ADR-0027), 텍스처/스프라이트 아틀라스, 텍스트, 카메라 회전/종횡비 보정, 레이어 필터, offscreen GL driver.
