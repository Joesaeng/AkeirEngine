// akeir/runtime/Components.h — 엔진 내장(builtin) component. 설계 문서 §2.1 (Runtime 목표), §6 (예시 데이터), §42.2 (aggregate 규칙), §57 (2D-first).
//
//   Game/ 의 component(Health, Movement, EnemyAI …)는 여기 없다 — Game/Source 에 있다 (§60, §76).
//   모두 aggregate struct. JSON 표현과 mutation path 는 reflection 이 결정한다 (§14.1):
//     Transform: { "position": [x,y,z], "rotation": [x,y,z,w], "scale": [x,y,z] }   path /position/0 …
#pragma once

#include "akeir/core/Math.h"
#include "akeir/core/Ref.h"

#include <string>

namespace akeir {

/// 위치/회전/크기. 2D 게임도 z 를 depth/layer 로 쓴다 (§57: 2.5D 확장 여지).
struct Transform {
    Vec3 position{};
    Quat rotation{};           // identity {0,0,0,1}
    Vec3 scale{1.f, 1.f, 1.f};
};

/// 2D 스프라이트. sprite 는 asset_…#sprites/<name> 참조 (§19 sub-asset).
struct SpriteRenderer {
    Ref sprite;
    std::string layer = "Default";
    Color tint{};
    bool flipX = false;
    bool flipY = false;
    int sortingOrder = 0;
};

enum class ColliderShape { Box, Circle, Capsule };

/// 2D 충돌체 (Box2D 로 투영, §57). size 는 box 의 full extents, radius 는 circle/capsule.
struct Collider2D {
    ColliderShape shape = ColliderShape::Box;
    Vec2 size{1.f, 1.f};
    float radius = 0.5f;
    Vec2 offset{};
    bool isSensor = false;
    std::string layer = "Default";
};

enum class BodyType { Static, Kinematic, Dynamic };

/// 2D 강체. Collider2D 가 있어야 physics body 가 만들어진다.
struct RigidBody2D {
    BodyType type = BodyType::Dynamic;
    float mass = 1.f;
    float linearDamping = 0.f;
    float gravityScale = 0.f;   // top-down 게임 기본: 중력 없음
    bool fixedRotation = true;
    Vec2 velocity{};            // runtimeOnly
};

/// 2D 카메라. orthoSize = 세로 절반 높이 (world units).
struct Camera2D {
    float orthoSize = 10.f;
    bool primary = true;
    Color background{0.1f, 0.1f, 0.12f, 1.f};
};

enum class TextAlign { Left, Center, Right };

/// Text with the built-in 5x7 bitmap font (ADR-0040). World-space by default (Transform.position, camera applies);
/// screenSpace = true puts it on the HUD: Transform.position.x/y are pixels from the top-left of the window/capture.
/// Lowercase is drawn as uppercase; characters outside the font draw as a box. A game system may rewrite `text` every tick.
struct TextRenderer {
    std::string text;
    Color color{1.f, 1.f, 1.f, 1.f};
    float scale = 2.f;                 // pixels per font pixel (screen space) — a 5x7 glyph at 2 is 10x14 px
    TextAlign align = TextAlign::Left;
    bool screenSpace = false;
    int sortingOrder = 100;            // drawn after sprites of lower order
};

/// builtin component 등록을 강제로 링크 (정적 라이브러리에서 .obj 가 버려지는 것 방지). main() 에서 한 번 호출.
void registerBuiltinComponents();

} // namespace akeir
