// akeir/physics/Box2DWorld.cpp — Box2D v3.1.1 백엔드. 설계 문서 §57, §22.2
#include "akeir/physics/PhysicsWorld.h"
#include "akeir/core/Hash.h"

#include <box2d/box2d.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace akeir {

namespace {

class Box2DWorld final : public PhysicsWorld {
public:
    explicit Box2DWorld(const PhysicsConfig& cfg) : subSteps_(cfg.subSteps) {
        b2WorldDef def = b2DefaultWorldDef();
        def.gravity = {cfg.gravity.x, cfg.gravity.y};
        def.enableSleep = cfg.enableSleep;
        // workerCount / enqueueTask 는 기본(단일 스레드). Box2D 는 어차피 worker 수와 무관하게 결정적 (FAQ).
        world_ = b2CreateWorld(&def);
    }
    ~Box2DWorld() override { if (b2World_IsValid(world_)) b2DestroyWorld(world_); }

    bool createBody(const BodyDesc& d) override {
        if (d.handle == 0 || bodies_.count(d.handle)) return false;
        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = d.type == PhysBodyType::Static ? b2_staticBody : d.type == PhysBodyType::Kinematic ? b2_kinematicBody : b2_dynamicBody;
        bd.position = {d.position.x, d.position.y};
        bd.rotation = b2MakeRot(d.angleRad);
        bd.linearDamping = d.linearDamping;
        bd.gravityScale = d.gravityScale;
        bd.fixedRotation = d.fixedRotation;
        bd.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(d.handle));
        b2BodyId body = b2CreateBody(world_, &bd);

        b2ShapeDef sd = b2DefaultShapeDef();
        sd.isSensor = d.isSensor;
        sd.enableContactEvents = true;
        sd.enableSensorEvents = true;
        sd.filter.categoryBits = d.layerBits;
        sd.filter.maskBits = d.maskBits;
        sd.userData = bd.userData;
        // density 로 mass 를 맞춘다 (면적 기준). Box2D 는 mass 를 density × area 로 계산한다.
        float area = 1.f;
        switch (d.shape) {
        case PhysShape::Box: {
            area = std::max(1e-6f, d.size.x * d.size.y);
            sd.density = d.mass / area;
            b2Polygon box = b2MakeOffsetBox(d.size.x * 0.5f, d.size.y * 0.5f, {d.offset.x, d.offset.y}, b2MakeRot(0.f));
            b2CreatePolygonShape(body, &sd, &box);
            break;
        }
        case PhysShape::Circle: {
            area = std::max(1e-6f, 3.14159265f * d.radius * d.radius);
            sd.density = d.mass / area;
            b2Circle c{{d.offset.x, d.offset.y}, d.radius};
            b2CreateCircleShape(body, &sd, &c);
            break;
        }
        case PhysShape::Capsule: {
            // 2D top-down 에서 capsule 은 세로로 긴 형태: 높이 = 2*radius (중심 둘이 radius 만큼 떨어짐)
            float half = d.radius * 0.5f;
            area = std::max(1e-6f, 3.14159265f * d.radius * d.radius + 2.f * d.radius * 2.f * half);
            sd.density = d.mass / area;
            b2Capsule c{{d.offset.x, d.offset.y - half}, {d.offset.x, d.offset.y + half}, d.radius};
            b2CreateCapsuleShape(body, &sd, &c);
            break;
        }
        }
        bodies_[d.handle] = body;
        return true;
    }

    void destroyBody(BodyHandle h) override {
        auto it = bodies_.find(h);
        if (it == bodies_.end()) return;
        b2DestroyBody(it->second);
        bodies_.erase(it);
    }
    bool hasBody(BodyHandle h) const override { return bodies_.count(h) != 0; }

    void step(float dt, int subSteps) override {
        b2World_Step(world_, dt, subSteps > 0 ? subSteps : subSteps_);
        // §22.2: 이벤트는 버퍼링 후 정렬
        b2ContactEvents ce = b2World_GetContactEvents(world_);
        for (int i = 0; i < ce.beginCount; ++i) push(ContactEvent::Begin, ce.beginEvents[i].shapeIdA, ce.beginEvents[i].shapeIdB);
        for (int i = 0; i < ce.endCount; ++i) push(ContactEvent::End, ce.endEvents[i].shapeIdA, ce.endEvents[i].shapeIdB);
        b2SensorEvents se = b2World_GetSensorEvents(world_);
        for (int i = 0; i < se.beginCount; ++i) push(ContactEvent::SensorBegin, se.beginEvents[i].sensorShapeId, se.beginEvents[i].visitorShapeId);
        for (int i = 0; i < se.endCount; ++i) push(ContactEvent::SensorEnd, se.endEvents[i].sensorShapeId, se.endEvents[i].visitorShapeId);
        std::sort(events_.begin(), events_.end());
    }

    BodyState getState(BodyHandle h) const override {
        BodyState s;
        auto it = bodies_.find(h);
        if (it == bodies_.end()) return s;
        b2Vec2 p = b2Body_GetPosition(it->second);
        b2Vec2 v = b2Body_GetLinearVelocity(it->second);
        s.position = {p.x, p.y};
        s.angleRad = b2Rot_GetAngle(b2Body_GetRotation(it->second));
        s.velocity = {v.x, v.y};
        s.awake = b2Body_IsAwake(it->second);
        return s;
    }
    void setVelocity(BodyHandle h, Vec2 v) override {
        auto it = bodies_.find(h);
        if (it != bodies_.end()) { b2Body_SetLinearVelocity(it->second, {v.x, v.y}); if (v.x != 0.f || v.y != 0.f) b2Body_SetAwake(it->second, true); }
    }
    void setTransform(BodyHandle h, Vec2 position, float angleRad) override {
        auto it = bodies_.find(h);
        if (it != bodies_.end()) b2Body_SetTransform(it->second, {position.x, position.y}, b2MakeRot(angleRad));
    }
    std::vector<ContactEvent> drainContactEvents() override { auto out = std::move(events_); events_.clear(); return out; }

    void hashInto(Hasher& h) const override {
        for (const auto& [handle, body] : bodies_) {       // std::map → handle 순 (결정적)
            BodyState s = getState(handle);
            h.u64(handle); h.f32(s.position.x); h.f32(s.position.y); h.f32(s.angleRad); h.f32(s.velocity.x); h.f32(s.velocity.y);
        }
    }
    std::size_t bodyCount() const override { return bodies_.size(); }
    const char* backendName() const override { return "box2d-3.1.1"; }
    std::vector<std::pair<BodyHandle, BodyState>> allStates() const override {
        std::vector<std::pair<BodyHandle, BodyState>> out;
        out.reserve(bodies_.size());
        for (const auto& [handle, body] : bodies_) out.emplace_back(handle, getState(handle));
        return out;
    }

private:
    // Box2D reports End/SensorEnd events on the step AFTER gameplay destroyed a body (despawn on contact is the
    // normal survivor pattern). Those shape ids are stale: querying them trips Box2D's debug assert and reads
    // garbage in release. A handle whose body is gone is meaningless to gameplay, so the event is dropped. (ADR-0042)
    BodyHandle handleOf(b2ShapeId shape) const {
        if (!b2Shape_IsValid(shape)) return 0;
        b2BodyId body = b2Shape_GetBody(shape);
        if (!b2Body_IsValid(body)) return 0;
        auto h = static_cast<BodyHandle>(reinterpret_cast<std::uintptr_t>(b2Body_GetUserData(body)));
        return bodies_.count(h) ? h : 0;
    }
    void push(ContactEvent::Kind kind, b2ShapeId a, b2ShapeId b) {
        BodyHandle ha = handleOf(a), hb = handleOf(b);
        if (!ha || !hb) { ++droppedStale_; return; }
        if (ha > hb) std::swap(ha, hb);
        events_.push_back(ContactEvent{kind, ha, hb});
    }

    b2WorldId world_{};
    int subSteps_ = 4;
    std::map<BodyHandle, b2BodyId> bodies_;
    std::vector<ContactEvent> events_;
    std::uint64_t droppedStale_ = 0;
};

} // namespace

std::unique_ptr<PhysicsWorld> createBox2DWorld(const PhysicsConfig& cfg) { return std::make_unique<Box2DWorld>(cfg); }

} // namespace akeir
