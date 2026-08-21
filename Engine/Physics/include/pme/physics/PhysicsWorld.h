// pme/physics/PhysicsWorld.h — physics 추상화 + Box2D v3.1.1 백엔드. 설계 문서 §57 (2D-first, PhysicsWorld 인터페이스 뒤에 Box2D), §22.2 (Physics 결정론 규칙), §3 (Box2D MIT, C17).
//
//   규칙 (§22.2 Physics):
//     - tick 당 정확히 1회 Step(dt, subSteps=const)
//     - body 생성 순서 = entity id 순 (호출자가 보장)
//     - contact/sensor 이벤트는 버퍼링 후 (a, b) 정렬해 gameplay 로 전달 → DrainContactEvents()
//     - Box2D v3.1+ 은 설정 없이 thread-count 독립 bit-exact 결정적 (FAQ) — 여기서는 worker 없이(단일 스레드) 돌린다
//   Box2D 는 SaveState 가 없다 (§58). snapshot 복원 = world 재생성 (§26.1).
#pragma once

#include "pme/core/Math.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace pme {

using BodyHandle = std::uint64_t;   // 호출자(entity)가 정한 안정 키. 0 = 없음

enum class PhysBodyType { Static, Kinematic, Dynamic };
enum class PhysShape { Box, Circle, Capsule };

struct BodyDesc {
    BodyHandle handle = 0;
    PhysBodyType type = PhysBodyType::Dynamic;
    Vec2 position{};
    float angleRad = 0.f;
    float mass = 1.f;
    float linearDamping = 0.f;
    float gravityScale = 0.f;
    bool fixedRotation = true;
    // shape
    PhysShape shape = PhysShape::Box;
    Vec2 size{1.f, 1.f};      // box full extents
    float radius = 0.5f;      // circle / capsule
    Vec2 offset{};
    bool isSensor = false;
    std::uint16_t layerBits = 0x0001;   // collision category (이후 layer 이름 → bit 매핑)
    std::uint16_t maskBits = 0xFFFF;
};

struct BodyState {
    Vec2 position{};
    float angleRad = 0.f;
    Vec2 velocity{};
    bool awake = true;
};

struct ContactEvent {
    enum Kind { Begin, End, SensorBegin, SensorEnd } kind;
    BodyHandle a, b;          // a < b 로 정규화
    bool operator<(const ContactEvent& o) const { return std::tie(a, b, kind) < std::tie(o.a, o.b, o.kind); }
};

class PhysicsWorld {
public:
    virtual ~PhysicsWorld() = default;
    virtual bool createBody(const BodyDesc& desc) = 0;      // handle 중복이면 false
    virtual void destroyBody(BodyHandle h) = 0;
    virtual bool hasBody(BodyHandle h) const = 0;
    virtual void step(float dt, int subSteps = 4) = 0;
    virtual BodyState getState(BodyHandle h) const = 0;
    virtual void setVelocity(BodyHandle h, Vec2 v) = 0;
    virtual void setTransform(BodyHandle h, Vec2 position, float angleRad) = 0;
    /// 마지막 step 의 contact/sensor 이벤트, (a, b, kind) 정렬됨. 호출하면 비운다.
    virtual std::vector<ContactEvent> drainContactEvents() = 0;
    /// 결정적 해시용: handle 순으로 모든 body 의 position/angle/velocity (bit pattern)
    virtual void hashInto(class Hasher& h) const = 0;
    virtual std::size_t bodyCount() const = 0;
    virtual const char* backendName() const = 0;   // "box2d-3.1.1"
    /// 모든 body 의 상태 (handle 순) — snapshot (§26.1 physics.bodies)
    virtual std::vector<std::pair<BodyHandle, BodyState>> allStates() const = 0;
};

struct PhysicsConfig {
    Vec2 gravity{0.f, 0.f};    // top-down 기본
    bool enableSleep = true;
    int subSteps = 4;
};

std::unique_ptr<PhysicsWorld> createBox2DWorld(const PhysicsConfig& cfg);

} // namespace pme
