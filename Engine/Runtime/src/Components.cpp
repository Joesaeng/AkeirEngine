// akeir/runtime/Components.cpp — builtin component reflection 등록. 설계 문서 §42.2, §43.1
#include "akeir/runtime/Components.h"
#include "akeir/reflection/Reflect.h"

AKEIR_REFLECT_ENUM(akeir::ColliderShape, "box", "circle", "capsule")
AKEIR_REFLECT_ENUM(akeir::BodyType, "static", "kinematic", "dynamic")
AKEIR_REFLECT_ENUM(akeir::TextAlign, "left", "center", "right")

namespace akeir {

AKEIR_REFLECT_BEGIN(Transform, "Position, rotation and scale in world units")
    AKEIR_PROP(position, "World position [x, y, z]. 2D games use z as depth.").unit("m");
    AKEIR_PROP(rotation, "Rotation quaternion [x, y, z, w]").advanced();
    AKEIR_PROP(scale, "Scale per axis");
AKEIR_REFLECT_END(Transform)

AKEIR_REFLECT_BEGIN(SpriteRenderer, "Draws a 2D sprite")
    AKEIR_REQUIRES("Transform");
    AKEIR_LIFECYCLE("OnSpawn", "Render phase", "OnDespawn");
    AKEIR_PROP(sprite, "Sprite sub-asset (asset_…#sprites/<name>)").refType("asset:texture");
    AKEIR_PROP(layer, "Render layer name");
    AKEIR_PROP(tint, "Color multiplier RGBA");
    AKEIR_PROP(flipX, "Mirror horizontally");
    AKEIR_PROP(flipY, "Mirror vertically");
    AKEIR_PROP(sortingOrder, "Draw order within layer").range(-1000, 1000);
    AKEIR_PROP(screenSpace, "true: HUD element — rect top-left = anchor × viewport + Transform.position.xy in pixels (ADR-0045)");
    AKEIR_PROP(anchor, "screenSpace only: viewport fraction the position is relative to (0,0 top-left, 0.5,0.5 center, 1,1 bottom-right)");
    AKEIR_PROP(pixelSize, "screenSpace only: size in pixels; 0 = natural (sprite pixels × scale, placeholder 32)");
    AKEIR_PROP(fill, "horizontal fill fraction kept from the left (HP bars)").range(0.0, 1.0);
AKEIR_REFLECT_END(SpriteRenderer)

AKEIR_REFLECT_BEGIN(Collider2D, "2D collision shape (Box2D)")
    AKEIR_REQUIRES("Transform");
    AKEIR_LIFECYCLE("OnSpawn (after Transform)", "Physics step", "OnDespawn");
    AKEIR_PROP(shape, "Primitive shape");
    AKEIR_PROP(size, "Box full extents [w, h]").unit("m");
    AKEIR_PROP(radius, "Circle/capsule radius").min(0.001).warn(0.05, 50).unit("m");
    AKEIR_PROP(offset, "Local offset from Transform").unit("m");
    AKEIR_PROP(isSensor, "Detect overlaps without collision response");
    AKEIR_PROP(layer, "Collision layer name");
AKEIR_REFLECT_END(Collider2D)

AKEIR_REFLECT_BEGIN(RigidBody2D, "2D rigid body (Box2D)")
    AKEIR_REQUIRES("Transform");
    AKEIR_REQUIRES("Collider2D");
    AKEIR_LIFECYCLE("OnSpawn (after Collider2D)", "Physics step", "OnDespawn");
    AKEIR_PROP(type, "static | kinematic | dynamic");
    AKEIR_PROP(mass, "Mass in kg").min(0.001).unit("kg");
    AKEIR_PROP(linearDamping, "Velocity damping").range(0, 100);
    AKEIR_PROP(gravityScale, "Gravity multiplier (0 for top-down)").range(-10, 10);
    AKEIR_PROP(fixedRotation, "Prevent rotation");
    AKEIR_PROP(velocity, "Current linear velocity").runtimeOnly().save().unit("m/s");
AKEIR_REFLECT_END(RigidBody2D)

AKEIR_REFLECT_BEGIN(Camera2D, "Orthographic 2D camera")
    AKEIR_REQUIRES("Transform");
    AKEIR_PROP(orthoSize, "Half height of the view in world units").min(0.01).unit("m").ui(1, 100, 0.5);
    AKEIR_PROP(primary, "Render from this camera");
    AKEIR_PROP(background, "Clear color");
AKEIR_REFLECT_END(Camera2D)

AKEIR_REFLECT_BEGIN(TextRenderer, "Text drawn with the built-in 5x7 bitmap font (HUD when screenSpace)")
    AKEIR_REQUIRES("Transform");
    AKEIR_PROP(text, "Text to draw. Lowercase prints as uppercase; unsupported characters print as a box. Systems may update it every tick");
    AKEIR_PROP(color, "Text color");
    AKEIR_PROP(scale, "Pixels per font pixel (a glyph is 5x7)").min(0.25).ui(1, 8, 0.5);
    AKEIR_PROP(align, "Horizontal alignment around the position");
    AKEIR_PROP(screenSpace, "true: Transform.position.x/y are pixels from the top-left of the screen (HUD); false: world space");
    AKEIR_PROP(sortingOrder, "Draw order (higher = on top)");
AKEIR_REFLECT_END(TextRenderer)

void registerBuiltinComponents() {
    // 이 TU 의 static initializer 들이 등록을 수행한다. 이 함수는 링커가 TU 를 버리지 않게 하는 앵커다.
    (void)akeirReflectInstance_Transform;
    (void)akeirReflectInstance_SpriteRenderer;
    (void)akeirReflectInstance_Collider2D;
    (void)akeirReflectInstance_RigidBody2D;
    (void)akeirReflectInstance_Camera2D;
    (void)akeirReflectInstance_TextRenderer;
}

} // namespace akeir
