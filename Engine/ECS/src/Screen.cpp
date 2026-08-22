// Screen.cpp — world ↔ screen, sprite rect, hit-test (ADR-0045). Renderer2D 와 게임 코드의 공통 정의.
#include "akeir/ecs/Screen.h"
#include "akeir/ecs/PlayWorld.h"
#include "akeir/runtime/Assets.h"

#include <algorithm>

namespace akeir {

CameraView primaryCamera(const PlayWorld& world) {
    CameraView v;
    for (const auto& id : world.entityIds()) {
        const Camera2D* cam = world.get<Camera2D>(id);
        if (!cam || !cam->primary) continue;
        if (const Transform* t = world.get<Transform>(id)) v.position = Vec2{t->position.x, t->position.y};
        v.orthoSize = cam->orthoSize > 0.01f ? cam->orthoSize : 10.f;
        v.background = cam->background;
        v.entity = id;
        break;
    }
    return v;
}

float pixelsPerUnit(const CameraView& cam, const Viewport& vp) {
    return static_cast<float>(vp.height) / (2.f * (cam.orthoSize > 0.01f ? cam.orthoSize : 10.f));
}

Vec2 worldToScreen(const CameraView& cam, const Viewport& vp, Vec2 world) {
    const float ppu = pixelsPerUnit(cam, vp);
    return Vec2{vp.width * 0.5f + (world.x - cam.position.x) * ppu, vp.height * 0.5f - (world.y - cam.position.y) * ppu};
}

Vec2 screenToWorld(const CameraView& cam, const Viewport& vp, Vec2 screen) {
    const float ppu = pixelsPerUnit(cam, vp);
    if (ppu <= 0.f) return cam.position;
    return Vec2{cam.position.x + (screen.x - vp.width * 0.5f) / ppu, cam.position.y - (screen.y - vp.height * 0.5f) / ppu};
}

std::optional<ScreenRect> spriteScreenRect(const PlayWorld& world, const std::string& id, const CameraView& cam, const Viewport& vp, SpriteGeometry* geom) {
    const SpriteRenderer* sp = world.get<SpriteRenderer>(id);
    const Transform* t = world.get<Transform>(id);
    if (!sp || !t) return std::nullopt;
    SpriteGeometry g;
    float w = 1.f, h = 1.f;          // world units (or placeholder)
    float pxW = 32.f, pxH = 32.f;    // natural pixel size for screen space
    Vec2 pivot{0.5f, 0.5f};
    if (!sp->sprite.empty()) {
        if (const SpriteRegion* reg = world.assets().resolveSprite(sp->sprite, &g.asset, &g.why)) {
            g.region = reg;
            w = static_cast<float>(reg->w) / g.asset->pixelsPerUnit;
            h = static_cast<float>(reg->h) / g.asset->pixelsPerUnit;
            pxW = static_cast<float>(reg->w); pxH = static_cast<float>(reg->h);
            pivot = reg->pivot;
        }
    }
    if (!g.region) {
        if (const Collider2D* c = world.get<Collider2D>(id)) {
            if (c->shape == ColliderShape::Box) { w = c->size.x; h = c->size.y; }
            else { w = h = c->radius * 2.f; g.circle = c->shape == ColliderShape::Circle; }
        }
    }
    ScreenRect r;
    if (sp->screenSpace) {
        r.w = sp->pixelSize.x > 0.f ? sp->pixelSize.x : pxW * t->scale.x;
        r.h = sp->pixelSize.y > 0.f ? sp->pixelSize.y : pxH * t->scale.y;
        r.x = sp->anchor.x * static_cast<float>(vp.width) + t->position.x;
        r.y = sp->anchor.y * static_cast<float>(vp.height) + t->position.y;
    } else {
        const float ppu = pixelsPerUnit(cam, vp);
        r.w = std::max(1.f, w * t->scale.x * ppu);
        r.h = std::max(1.f, h * t->scale.y * ppu);
        const Vec2 s = worldToScreen(cam, vp, Vec2{t->position.x, t->position.y});
        r.x = s.x - r.w * pivot.x;
        r.y = s.y - r.h * (1.f - pivot.y);
    }
    g.rect = r;
    if (geom) *geom = std::move(g);
    return r;
}

std::string hitTest(const PlayWorld& world, const Viewport& vp, Vec2 screen) {
    const CameraView cam = primaryCamera(world);
    std::string best;
    int bestOrder = 0;
    for (const auto& id : world.entityIds()) {
        const SpriteRenderer* sp = world.get<SpriteRenderer>(id);
        if (!sp) continue;
        auto r = spriteScreenRect(world, id, cam, vp);
        if (!r) continue;
        ScreenRect rr = *r;
        rr.w *= std::clamp(sp->fill, 0.f, 1.f);
        if (!rr.contains(screen.x, screen.y)) continue;
        if (best.empty() || sp->sortingOrder > bestOrder || (sp->sortingOrder == bestOrder && id > best)) { best = id; bestOrder = sp->sortingOrder; }
    }
    return best;
}

} // namespace akeir
