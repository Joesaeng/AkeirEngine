// akeir/ecs/PlayWorld.h — play world: authoring 문서를 Flecs world + Box2D 로 투영하고 tick 한다. 설계 문서 §3.1 (Flecs 채택, authoring JSON 은 source of truth),
// §8.2 (CommandApply 단계), §16 (query), §20.1 (tick), §22.2 (결정적 순서·hash), §25 (dump), §26.1 (snapshot), §57 (Box2D sync), §88.2 (authoring ≠ play).
//
//   build(project, worldId) :
//     - entity 를 persistent id 순으로 생성 (결정적). Flecs entity name = persistent id → ecs_lookup 가능
//     - component 는 reflection(ComponentMeta) 으로 Flecs 에 동적 등록 (size/align + ctor/dtor/copy/move 훅)
//     - 계층 = ChildOf pair
//     - RigidBody2D + Collider2D 가 있으면 Box2D body 생성 (handle = entity 순번)
//   tick(input, simTime) :
//     1. systems (등록 순서) — Game/ 의 로직 (입력 → 속도 등)
//     2. physics.step(dt)  — RigidBody2D.velocity → body, 그 뒤 body → Transform.position / velocity
//     3. contact 이벤트 drain (정렬됨) → 이번 tick 의 events()
//   hash() : entity id 순으로 모든 component 의 reflected 값(float 은 bit pattern) + rng 상태 + physics
//   play world 의 변경은 authoring 문서에 닿지 않는다 (§88.2).
#pragma once

#include "akeir/core/Rng.h"
#include "akeir/physics/PhysicsWorld.h"
#include "akeir/reflection/PropertyMeta.h"
#include "akeir/reflection/Registry.h"
#include "akeir/runtime/Application.h"
#include "akeir/runtime/Project.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct ecs_world_t;

namespace akeir {

struct PlayWorldConfig {
    std::uint64_t seed = 0;
    std::int32_t tickRate = 60;
    PhysicsConfig physics{};
};

class PlayWorld;
using SystemFn = std::function<void(PlayWorld&, const InputFrame&, const SimTime&)>;

class PlayWorld final : public ISimulation {
public:
    ~PlayWorld() override;
    PlayWorld(const PlayWorld&) = delete;
    PlayWorld& operator=(const PlayWorld&) = delete;
    PlayWorld(PlayWorld&&) = delete;              // unique_ptr 로만 다룬다 (flecs world 소유권)
    PlayWorld& operator=(PlayWorld&&) = delete;

    /// authoring 문서 → play world. 실패(세계 없음, component 오류)는 diagnostics 로.
    static std::unique_ptr<PlayWorld> build(const Project& project, std::string_view worldId, const PlayWorldConfig& cfg, std::vector<Diagnostic>& diagnostics);

    // ---- ISimulation ----
    void tick(const InputFrame& input, const SimTime& simTime) override;
    std::uint64_t hash() const override;
    Json systemHashes() const override;

    // ---- systems (Game/ 가 등록) ----
    void addSystem(std::string name, SystemFn fn);
    std::vector<std::string> systemNames() const;
    /// OnSpawn 훅 (§18 lifecycle "init"): 등록 즉시 기존 entity 전부에 적용되고, 이후 spawn() 마다 호출된다.
    /// 예: Health.current = Health.max 초기화 (runtimeOnly 값은 authoring 파일에 없으므로 여기서 채운다)
    using SpawnHook = std::function<void(PlayWorld&, const std::string& entityId)>;
    void addSpawnHook(std::string name, SpawnHook fn);

    // ---- entity / component 접근 (persistent id 기준) ----
    const std::vector<std::string>& entityIds() const { return ids_; }     // 정렬됨
    bool hasEntity(std::string_view id) const;
    bool hasComponent(std::string_view id, std::string_view component) const;
    void* component(std::string_view id, std::string_view component);       // 없으면 nullptr (수정 시 markModified 불필요 — 값 타입)
    const void* component(std::string_view id, std::string_view component) const;
    template <class T> T* get(std::string_view id) {
        const ComponentMeta* m = Registry::global().find<T>();
        return m ? static_cast<T*>(component(id, m->name)) : nullptr;
    }
    template <class T> const T* get(std::string_view id) const {
        const ComponentMeta* m = Registry::global().find<T>();
        return m ? static_cast<const T*>(component(id, m->name)) : nullptr;
    }
    const std::vector<std::string>& tags(std::string_view id) const;
    bool hasTag(std::string_view id, std::string_view tag) const;
    std::string name(std::string_view id) const;
    std::optional<std::string> parent(std::string_view id) const;
    /// runtime 에 entity 추가 (§7.1: 결정적 id = Id::deterministic(seed, tick, ordinal)). components = {Name: json}
    std::string spawn(const std::string& name, const Json& components, const std::vector<std::string>& tags = {}, std::optional<std::string> parent = std::nullopt);
    void despawn(std::string_view id);

    // ---- query (§16) ----
    std::vector<std::string> query(const std::vector<std::string>& with, const std::vector<std::string>& without = {}) const;

    // ---- inspection ----
    Json dumpEntity(std::string_view id, Visibility v = Visibility::Snapshot) const;   // §25
    Json snapshot() const;                                                             // §26.1
    const std::vector<ContactEvent>& contactEvents() const { return events_; }        // 이번 tick
    PhysicsWorld& physics() { return *physics_; }
    RngStream& rng(const std::string& streamName);                                     // (seed, name) 스트림, 상태는 snapshot 에
    std::int64_t currentTick() const { return tick_; }
    std::uint64_t seed() const { return cfg_.seed; }
    ecs_world_t* flecs() { return world_; }
    const std::string& worldId() const { return worldId_; }

private:
    PlayWorld();
    struct EntityRec;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    ecs_world_t* world_ = nullptr;
    std::unique_ptr<PhysicsWorld> physics_;
    PlayWorldConfig cfg_;
    std::string worldId_;
    std::vector<std::string> ids_;                    // 정렬 유지
    std::vector<std::pair<std::string, SystemFn>> systems_;
    std::vector<std::pair<std::string, SpawnHook>> spawnHooks_;
    std::vector<ContactEvent> events_;
    std::map<std::string, RngStream> rngs_;
    std::int64_t tick_ = 0;
    std::uint64_t spawnOrdinal_ = 0;

    void syncToPhysics();
    void syncFromPhysics();
    void ensureBody(const std::string& id);
};

} // namespace akeir
