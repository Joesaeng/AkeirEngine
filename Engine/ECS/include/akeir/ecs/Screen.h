// akeir/ecs/Screen.h — world ↔ screen 변환과 sprite 의 화면 사각형, hit-test. ADR-0045.
//
//   렌더러와 게임 코드가 같은 정의를 쓴다: Renderer2D 는 여기서 rect 를 받아 그리고, 게임은 같은 rect 로 "포인터가 이 버튼 위인가" 를 판단한다.
//   좌표계: 화면은 픽셀, 좌상단 원점, y 아래. world 는 y 위. primary Camera2D 의 orthoSize = 화면 세로 절반에 해당하는 world 단위.
//   screenSpace sprite (SpriteRenderer.screenSpace=true): rect 의 좌상단 = anchor × viewport + Transform.position(px), 크기 = pixelSize (0 이면 natural × scale).
//   TextRenderer.screenSpace 와 같은 규칙(좌상단 = position px). pivot 은 world-space 에서만 적용.
#pragma once

#include "akeir/core/Math.h"
#include "akeir/runtime/Components.h"

#include <optional>
#include <string>

namespace akeir {

class PlayWorld;
struct SpriteRegion;
struct AssetMeta;

struct Viewport {
    int width = 0, height = 0;
};

struct CameraView {
    std::string entity;            // primary Camera2D 를 가진 entity (없으면 "")
    Vec2 position{};               // Transform.position.xy
    float orthoSize = 10.f;
    Color background{0.1f, 0.1f, 0.12f, 1.f};
};

struct ScreenRect {
    float x = 0.f, y = 0.f, w = 0.f, h = 0.f;   // 좌상단 + 크기 (px)
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

/// id 순서로 첫 primary Camera2D. 없으면 기본 카메라(원점, orthoSize 10).
CameraView primaryCamera(const PlayWorld& world);
/// 화면 세로 픽셀 / (2 × orthoSize)
float pixelsPerUnit(const CameraView& cam, const Viewport& vp);
Vec2 worldToScreen(const CameraView& cam, const Viewport& vp, Vec2 world);
Vec2 screenToWorld(const CameraView& cam, const Viewport& vp, Vec2 screen);

/// sprite 의 화면 사각형을 구할 때 같이 얻는 그리기 정보 (렌더러용)
struct SpriteGeometry {
    ScreenRect rect;                       // fill 적용 전 전체 사각형
    bool circle = false;                   // 텍스처 없음 + Collider2D circle/capsule → 원 placeholder
    const SpriteRegion* region = nullptr;  // 해석된 sub-asset (없으면 placeholder)
    const AssetMeta* asset = nullptr;
    std::string why;                       // region == nullptr 이고 sprite ref 가 있었으면 이유
};

/// entity 의 SpriteRenderer + Transform 으로 화면 사각형. SpriteRenderer/Transform 이 없으면 nullopt.
std::optional<ScreenRect> spriteScreenRect(const PlayWorld& world, const std::string& id, const CameraView& cam, const Viewport& vp, SpriteGeometry* geom = nullptr);

/// 화면 점(px) 아래의 가장 위 sprite entity (sortingOrder 큰 것, 같으면 id 큰 것 = 나중에 그려진 것). 없으면 "".
/// screenSpace 와 world-space sprite 모두 대상. fill 로 잘린 부분은 포함하지 않는다.
std::string hitTest(const PlayWorld& world, const Viewport& vp, Vec2 screen);

} // namespace akeir
