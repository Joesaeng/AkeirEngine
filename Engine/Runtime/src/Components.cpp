// pme/runtime/Components.cpp — builtin component reflection 등록. 설계 문서 §42.2, §43.1
#include "pme/runtime/Components.h"
#include "pme/reflection/Reflect.h"

PME_REFLECT_ENUM(pme::ColliderShape, "box", "circle", "capsule")
PME_REFLECT_ENUM(pme::BodyType, "static", "kinematic", "dynamic")

namespace pme {

PME_REFLECT_BEGIN(Transform, "Position, rotation and scale in world units")
    PME_PROP(position, "World position [x, y, z]. 2D games use z as depth.").unit("m");
    PME_PROP(rotation, "Rotation quaternion [x, y, z, w]").advanced();
    PME_PROP(scale, "Scale per axis");
PME_REFLECT_END(Transform)

PME_REFLECT_BEGIN(SpriteRenderer, "Draws a 2D sprite")
    PME_REQUIRES("Transform");
    PME_LIFECYCLE("OnSpawn", "Render phase", "OnDespawn");
    PME_PROP(sprite, "Sprite sub-asset (asset_…#sprites/<name>)").refType("asset:texture");
    PME_PROP(layer, "Render layer name");
    PME_PROP(tint, "Color multiplier RGBA");
    PME_PROP(flipX, "Mirror horizontally");
    PME_PROP(flipY, "Mirror vertically");
    PME_PROP(sortingOrder, "Draw order within layer").range(-1000, 1000);
PME_REFLECT_END(SpriteRenderer)

PME_REFLECT_BEGIN(Collider2D, "2D collision shape (Box2D)")
    PME_REQUIRES("Transform");
    PME_LIFECYCLE("OnSpawn (after Transform)", "Physics step", "OnDespawn");
    PME_PROP(shape, "Primitive shape");
    PME_PROP(size, "Box full extents [w, h]").unit("m");
    PME_PROP(radius, "Circle/capsule radius").min(0.001).warn(0.05, 50).unit("m");
    PME_PROP(offset, "Local offset from Transform").unit("m");
    PME_PROP(isSensor, "Detect overlaps without collision response");
    PME_PROP(layer, "Collision layer name");
PME_REFLECT_END(Collider2D)

PME_REFLECT_BEGIN(RigidBody2D, "2D rigid body (Box2D)")
    PME_REQUIRES("Transform");
    PME_REQUIRES("Collider2D");
    PME_LIFECYCLE("OnSpawn (after Collider2D)", "Physics step", "OnDespawn");
    PME_PROP(type, "static | kinematic | dynamic");
    PME_PROP(mass, "Mass in kg").min(0.001).unit("kg");
    PME_PROP(linearDamping, "Velocity damping").range(0, 100);
    PME_PROP(gravityScale, "Gravity multiplier (0 for top-down)").range(-10, 10);
    PME_PROP(fixedRotation, "Prevent rotation");
    PME_PROP(velocity, "Current linear velocity").runtimeOnly().save().unit("m/s");
PME_REFLECT_END(RigidBody2D)

PME_REFLECT_BEGIN(Camera2D, "Orthographic 2D camera")
    PME_REQUIRES("Transform");
    PME_PROP(orthoSize, "Half height of the view in world units").min(0.01).unit("m").ui(1, 100, 0.5);
    PME_PROP(primary, "Render from this camera");
    PME_PROP(background, "Clear color");
PME_REFLECT_END(Camera2D)

void registerBuiltinComponents() {
    // 이 TU 의 static initializer 들이 등록을 수행한다. 이 함수는 링커가 TU 를 버리지 않게 하는 앵커다.
    (void)pmeReflectInstance_Transform;
    (void)pmeReflectInstance_SpriteRenderer;
    (void)pmeReflectInstance_Collider2D;
    (void)pmeReflectInstance_RigidBody2D;
    (void)pmeReflectInstance_Camera2D;
}

} // namespace pme
