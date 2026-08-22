# Engine/Render (`akeir_render`) — 2D placeholder renderer · PNG capture · golden comparison

Design doc §27 (capture is a first-class feature), §27.1 (capture for vision checks vs. golden regression), §20 (capture in headless mode). **Only in `AKEIR_WITH_SDL=ON` builds (preset `msvc-debug`).**

| File | Contents |
|---|---|
| `include/akeir/render/Renderer2D.h`, `src/Renderer2D.cpp` | `Renderer2D::createForWindow(SDL_Window*)` (accelerated SDL_Renderer, vsync) / `createSoftware(w, h)` (SDL_Surface + `SDL_CreateSoftwareRenderer` — **no window or GPU needed**). `render(world)` → `RenderStats{sprites, backend, camera}`. `savePng(path)` (`SDL_SavePNG`), `readPixels()`. `compareCaptures(expected, actual, CaptureTolerance{perPixel, maxMismatchRatio}, diffPngOut)` |

## What gets drawn (Phase 2 PoC)

- Every entity with a `SpriteRenderer`, at `Transform.position` (x, y). **With a sprite ref** (`sprite = "asset_…#sprites/<name>"`, ADR-0037) the texture region from the sidecar is drawn: size = rect / `pixelsPerUnit` × `Transform.scale`, placed by the sprite's `pivot`, `tint` multiplies the texture (white = unchanged), `flipX/flipY` flip it, `settings.filter` nearest (default) or linear. Textures are loaded once per asset id with `SDL_LoadPNG`; a ref that does not resolve is a `validate` error and draws the placeholder. **Without a sprite ref**: a `tint`-colored shape sized by `Collider2D` (box: size, circle/capsule: radius×2) or 1×1 unit, × `Transform.scale`; circles are approximated by scanline rectangles (deterministic).
- **Text** (ADR-0040): every entity with a `TextRenderer` is drawn after the sprites with the built-in 5×7 bitmap font (`src/Font5x7.h`: digits, uppercase, punctuation; lowercase → uppercase; other characters → a box). `scale` = pixels per font pixel, `align` around the position, `color`. `screenSpace` puts `Transform.position.x/y` in window pixels from the top-left (HUD); otherwise world space through the camera. Glyphs are filled rectangles, so the software path stays byte-deterministic.
- Sprite rects come from `akeir/ecs/Screen.h` (`spriteScreenRect`) — world-space (camera, pivot) or `screenSpace` (anchor × viewport + position px, `pixelSize`); `fill` crops from the left (ADR-0045).
- Sprites/texts whose screen rect lies outside the viewport are culled before sorting (`RenderStats.culled`, ADR-0044); `RenderStats.renderMs` times `render()`.
- Ordering: `sortingOrder` ascending, ties broken by entity id (deterministic).
- Camera: the first entity (by id) with `Camera2D.primary`; centered on its `Transform.position`, `orthoSize` = vertical half-height in world units, `background`. Without one: origin, 10. +Y is up.
- **The software target is a CPU rasterizer**, so the same world → the same PNG bytes (verified by the `Render_Capture.cpp` test). Window-target pixels (direct3d11 etc.) are never used for golden comparison (§27.1: goldens come from the same rasterizer).
- `render()` ends with `SDL_FlushRenderer` — the software renderer must flush its batch before the surface holds pixels.

## Comparison (a simplification of §27.1)

A cut-down pixelmatch: a pixel mismatches when its maximum channel difference (0–255) exceeds `perPixel×255`; the comparison fails when `mismatched/total > maxMismatchRatio`. With `diffPngOut`, mismatches are painted red over a brightened copy of `expected`. Different dimensions → `sizeMismatch`. Anti-aliasing exclusion and local error windows are not implemented.

## Used by

- `akeir capture [--ticks N] [--width W --height H] [--out f.png] [--compare golden.png --diff d.png]` (Tools/CLI/SdlCommands.cpp)
- the capture assertion in `akeir test` (`TestRunnerOptions.capture/compare` hooks) — goldens live in `Tests/Golden/<test>/<golden>_<WxH>.png` and are created/updated with `--update-golden`
- `akeir run` in windowed mode (createForWindow)

## Not implemented

The SDL_GPU path (§3 "SDL_GPU first" — SDL_Renderer is enough for the PoC, ADR-0027), atlas generation / animation frames, TTF fonts / non-Latin text (a font asset is the upgrade path), text wrapping, camera rotation/aspect correction, layer filtering, the offscreen GL driver.
