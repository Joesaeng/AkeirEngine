// akeir/ecs/PlayWorld.cpp — 설계 문서 §3.1, §16, §20.1, §22.2, §25, §26.1, §57, §88.2
#include "akeir/ecs/PlayWorld.h"
#include "akeir/core/Hash.h"
#include "akeir/core/Id.h"
#include "akeir/core/Log.h"
#include "akeir/reflection/Registry.h"
#include "akeir/runtime/Components.h"
#include "akeir/serialization/Canonical.h"
#include "akeir/serialization/ComponentJson.h"

#define FLECS_NO_CPP   // C API 만 쓴다 (C++ addon 헤더의 경고 회피)
#include <flecs.h>

#include <algorithm>
#include <cmath>

namespace akeir {

// ---------------------------------------------------------------- flecs hooks (reflection → Flecs type hooks)

namespace {

const ComponentMeta* metaOf(const ecs_type_info_t* ti) { return static_cast<const ComponentMeta*>(ti->hooks.binding_ctx); }

void hookCtor(void* ptr, int32_t count, const ecs_type_info_t* ti) {
    const ComponentMeta* m = metaOf(ti);
    for (int32_t i = 0; i < count; ++i) m->construct(static_cast<char*>(ptr) + static_cast<std::size_t>(i) * ti->size);
}
void hookDtor(void* ptr, int32_t count, const ecs_type_info_t* ti) {
    const ComponentMeta* m = metaOf(ti);
    for (int32_t i = 0; i < count; ++i) m->destroy(static_cast<char*>(ptr) + static_cast<std::size_t>(i) * ti->size);
}
void hookCopy(void* dst, const void* src, int32_t count, const ecs_type_info_t* ti) {
    const ComponentMeta* m = metaOf(ti);
    for (int32_t i = 0; i < count; ++i) m->copy(static_cast<char*>(dst) + static_cast<std::size_t>(i) * ti->size, static_cast<const char*>(src) + static_cast<std::size_t>(i) * ti->size);
}
void hookMove(void* dst, void* src, int32_t count, const ecs_type_info_t* ti) {
    const ComponentMeta* m = metaOf(ti);
    for (int32_t i = 0; i < count; ++i) m->move(static_cast<char*>(dst) + static_cast<std::size_t>(i) * ti->size, static_cast<char*>(src) + static_cast<std::size_t>(i) * ti->size);
}

void hashJsonValue(Hasher& h, const Json& v) {
    if (v.is_number_float()) h.f64(v.get<double>());
    else if (v.is_number_integer()) h.i64(v.get<std::int64_t>());
    else if (v.is_boolean()) h.u32(v.get<bool>() ? 1u : 0u);
    else if (v.is_string()) h.str(v.get<std::string>());
    else if (v.is_array()) { h.u64(v.size()); for (const auto& e : v) hashJsonValue(h, e); }
    else if (v.is_object()) { h.u64(v.size()); for (auto it = v.begin(); it != v.end(); ++it) { h.str(it.key()); hashJsonValue(h, it.value()); } }
    else h.u32(0xFFFFFFFFu);
}

} // namespace

// ---------------------------------------------------------------- Impl

struct PlayWorld::EntityRec {
    ecs_entity_t e = 0;
    std::string name;
    std::vector<std::string> tags;
    std::optional<std::string> parent;
    BodyHandle body = 0;
};

struct PlayWorld::Impl {
    std::map<std::string, EntityRec> entities;              // id → rec (정렬된 맵)
    std::map<std::string, ecs_entity_t> components;         // component name → flecs id
    BodyHandle nextHandle = 1;
    std::vector<std::string> emptyTags;

    ecs_entity_t componentId(ecs_world_t* world, const ComponentMeta& meta) {
        auto it = components.find(meta.name);
        if (it != components.end()) return it->second;
        ecs_entity_desc_t ed{};
        ed.name = meta.name.c_str();
        ed.use_low_id = true;
        ecs_component_desc_t cd{};
        cd.entity = ecs_entity_init(world, &ed);
        cd.type.size = static_cast<ecs_size_t>(meta.size);
        cd.type.alignment = static_cast<ecs_size_t>(meta.align);
        ecs_entity_t comp = ecs_component_init(world, &cd);
        ecs_type_hooks_t hooks{};
        hooks.ctor = hookCtor;
        hooks.dtor = hookDtor;
        hooks.copy = hookCopy;
        hooks.move = hookMove;
        hooks.binding_ctx = const_cast<ComponentMeta*>(&meta);
        ecs_set_hooks_id(world, comp, &hooks);
        components[meta.name] = comp;
        return comp;
    }
};

// ---------------------------------------------------------------- ctor/dtor

PlayWorld::PlayWorld() : impl_(std::make_unique<Impl>()) {}
PlayWorld::~PlayWorld() { if (world_) ecs_fini(world_); }

// ---------------------------------------------------------------- build

std::unique_ptr<PlayWorld> PlayWorld::build(const Project& project, std::string_view worldId, const PlayWorldConfig& cfg, std::vector<Diagnostic>& diagnostics) {
    registerBuiltinComponents();
    auto loc = project.locate(worldId);
    if (!loc || loc->kind != "world") {
        diagnostics.push_back(Diagnostic::error("WORLD_NOT_FOUND", "World " + std::string(worldId) + " does not exist in the project."));
        return nullptr;
    }
    const Json& worldDoc = *project.document(loc->doc);
    std::unique_ptr<PlayWorld> pw(new PlayWorld());
    pw->cfg_ = cfg;
    pw->worldId_ = std::string(worldId);
    pw->assets_ = project.assets();
    for (const auto& path : project.prefabPaths()) {   // ADR-0038: prefabs resolved once, spawnable at runtime
        const Json* pd = project.document(path);
        if (!pd || !pd->contains("id") || !(*pd)["id"].is_string()) continue;
        PrefabInfo info;
        info.id = (*pd)["id"].get<std::string>();
        info.name = pd->value("name", "");
        if (pd->contains("tags") && (*pd)["tags"].is_array()) for (const auto& t : (*pd)["tags"]) if (t.is_string()) info.tags.push_back(t.get<std::string>());
        std::vector<Diagnostic> pdiags;
        if (auto comps = project.resolvePrefab(info.id, &pdiags)) info.components = *comps;
        else { for (auto& d : pdiags) diagnostics.push_back(d); continue; }
        pw->prefabs_[info.id] = std::move(info);
    }
    pw->world_ = ecs_init();
    pw->physics_ = createBox2DWorld(cfg.physics);

    // 1) entity 목록을 id 순으로 (결정적, §22.2)
    std::vector<std::string> ids;
    if (worldDoc.contains("entities") && worldDoc["entities"].is_object())
        for (auto it = worldDoc["entities"].begin(); it != worldDoc["entities"].end(); ++it) ids.push_back(it.key());
    std::sort(ids.begin(), ids.end());

    // 2) entity 생성 (이름 = persistent id)
    for (const auto& id : ids) {
        const Json& e = worldDoc["entities"][id];
        ecs_entity_desc_t ed{};
        ed.name = id.c_str();
        EntityRec rec;
        rec.e = ecs_entity_init(pw->world_, &ed);
        rec.name = e.value("name", id);
        if (e.contains("tags") && e["tags"].is_array()) for (const auto& t : e["tags"]) if (t.is_string()) rec.tags.push_back(t.get<std::string>());
        if (e.contains("parent") && e["parent"].is_string()) rec.parent = e["parent"].get<std::string>();
        pw->impl_->entities[id] = std::move(rec);
        pw->ids_.push_back(id);
    }
    // 3) 계층
    for (const auto& id : ids) {
        auto& rec = pw->impl_->entities[id];
        if (rec.parent) {
            auto pit = pw->impl_->entities.find(*rec.parent);
            if (pit != pw->impl_->entities.end()) ecs_add_id(pw->world_, rec.e, ecs_pair(EcsChildOf, pit->second.e));
        }
    }
    // 4) components (prefab resolve 포함, §34) + prefab 의 tags 병합
    bool ok = true;
    for (const auto& id : ids) {
        std::vector<Diagnostic> ds;
        auto comps = project.resolveEntityComponents(id, &ds);
        diagnostics.insert(diagnostics.end(), ds.begin(), ds.end());
        if (!comps) { ok = false; continue; }
        auto& rec = pw->impl_->entities[id];
        const Json& e = worldDoc["entities"][id];
        if (e.contains("prefab") && e["prefab"].is_string()) {
            auto ploc = project.locate(e["prefab"].get<std::string>());
            if (ploc) { const Json& pd = *project.document(ploc->doc); if (pd.contains("tags") && pd["tags"].is_array()) for (const auto& t : pd["tags"]) if (t.is_string() && std::find(rec.tags.begin(), rec.tags.end(), t.get<std::string>()) == rec.tags.end()) rec.tags.push_back(t.get<std::string>()); }
        }
        std::vector<std::string> names;
        for (auto it = comps->begin(); it != comps->end(); ++it) names.push_back(it.key());
        std::sort(names.begin(), names.end());
        for (const auto& cname : names) {
            const ComponentMeta* meta = Registry::global().find(cname);
            if (!meta) { diagnostics.push_back(Diagnostic::error("COMPONENT_UNKNOWN", "Unknown component '" + cname + "' on " + id + ".").at(LogicalLocation{id, cname, std::nullopt})); ok = false; continue; }
            ecs_entity_t comp = pw->impl_->componentId(pw->world_, *meta);
            void* ptr = ecs_ensure_id(pw->world_, rec.e, comp, meta->size);
            auto r = componentFromJson(*meta, ptr, (*comps)[cname], Visibility::Authoring, PhysicalLocation{loc->doc, "/entities/" + id + "/components/" + cname, std::nullopt}, id);
            for (auto& d : r.diagnostics) if (d.level == Severity::Error) diagnostics.push_back(d);
            if (!r.ok) ok = false;
            ecs_modified_id(pw->world_, rec.e, comp);
        }
    }
    if (!ok) return nullptr;
    // 5) physics bodies (id 순)
    for (const auto& id : ids) pw->ensureBody(id);
    AKEIR_LOG(Info, "ecs", "world_built", "Play world built.", Json{{"game.world", std::string(worldId)}, {"game.entities", ids.size()}, {"game.bodies", pw->physics_->bodyCount()}});
    return pw;
}

// ---------------------------------------------------------------- physics sync (§57)

void PlayWorld::ensureBody(const std::string& id) {
    auto it = impl_->entities.find(id);
    if (it == impl_->entities.end() || it->second.body != 0) return;
    const auto* rb = get<RigidBody2D>(id);
    const auto* col = get<Collider2D>(id);
    const auto* tf = get<Transform>(id);
    if (!rb || !col || !tf) return;
    BodyDesc d;
    d.handle = impl_->nextHandle++;
    d.type = rb->type == BodyType::Static ? PhysBodyType::Static : rb->type == BodyType::Kinematic ? PhysBodyType::Kinematic : PhysBodyType::Dynamic;
    d.position = {tf->position.x, tf->position.y};
    d.mass = rb->mass;
    d.linearDamping = rb->linearDamping;
    d.gravityScale = rb->gravityScale;
    d.fixedRotation = rb->fixedRotation;
    d.shape = col->shape == ColliderShape::Box ? PhysShape::Box : col->shape == ColliderShape::Circle ? PhysShape::Circle : PhysShape::Capsule;
    d.size = col->size;
    d.radius = col->radius;
    d.offset = col->offset;
    d.isSensor = col->isSensor;
    if (physics_->createBody(d)) it->second.body = d.handle;
}

void PlayWorld::syncToPhysics() {
    for (const auto& id : ids_) {
        const auto& rec = impl_->entities[id];
        if (!rec.body) continue;
        if (const auto* rb = get<RigidBody2D>(id)) physics_->setVelocity(rec.body, rb->velocity);
    }
}

void PlayWorld::syncFromPhysics() {
    for (const auto& id : ids_) {
        const auto& rec = impl_->entities[id];
        if (!rec.body) continue;
        BodyState s = physics_->getState(rec.body);
        if (auto* tf = get<Transform>(id)) { tf->position.x = s.position.x; tf->position.y = s.position.y; }
        if (auto* rb = get<RigidBody2D>(id)) rb->velocity = s.velocity;
    }
}

// ---------------------------------------------------------------- tick

void PlayWorld::tick(const InputFrame& input, const SimTime& simTime) {
    tick_ = simTime.tick;
    for (auto& s : systems_) if (s.phase == SystemPhase::PrePhysics) s.fn(*this, input, simTime);   // 등록 순서 (결정적)
    syncToPhysics();
    physics_->step(simTime.dt(), cfg_.physics.subSteps);
    syncFromPhysics();
    events_ = physics_->drainContactEvents();
    for (auto& s : systems_) if (s.phase == SystemPhase::PostPhysics) s.fn(*this, input, simTime);  // 이번 tick 의 contactEvents() 를 본다 (ADR-0038)
    tick_ = simTime.tick + 1;   // "지금까지 시뮬레이션한 tick 수". snapshot.tick / dump.tick 이 이 값 (N tick 돌리면 N)
}

void PlayWorld::addSystem(std::string name, SystemFn fn, SystemPhase phase) { systems_.push_back({std::move(name), std::move(fn), phase}); }
void PlayWorld::addSpawnHook(std::string name, SpawnHook fn) {
    for (const auto& id : ids_) fn(*this, id);   // 기존 entity 에 즉시 적용 (id 순)
    spawnHooks_.emplace_back(std::move(name), std::move(fn));
}
std::vector<std::string> PlayWorld::systemNames() const {
    std::vector<std::string> out;
    for (const auto& s : systems_) if (s.phase == SystemPhase::PrePhysics) out.push_back(s.name);
    for (const auto& s : systems_) if (s.phase == SystemPhase::PostPhysics) out.push_back(s.name);
    return out;
}

// ---------------------------------------------------------------- access

bool PlayWorld::hasEntity(std::string_view id) const { return impl_->entities.count(std::string(id)) != 0; }

bool PlayWorld::hasComponent(std::string_view id, std::string_view component) const {
    auto it = impl_->entities.find(std::string(id));
    auto c = impl_->components.find(std::string(component));
    if (it == impl_->entities.end() || c == impl_->components.end()) return false;
    return ecs_has_id(world_, it->second.e, c->second);
}

void* PlayWorld::component(std::string_view id, std::string_view component) {
    auto it = impl_->entities.find(std::string(id));
    auto c = impl_->components.find(std::string(component));
    if (it == impl_->entities.end() || c == impl_->components.end()) return nullptr;
    if (!ecs_has_id(world_, it->second.e, c->second)) return nullptr;
    const ComponentMeta* meta = Registry::global().find(component);
    return ecs_ensure_id(world_, it->second.e, c->second, meta ? meta->size : 0);
}
const void* PlayWorld::component(std::string_view id, std::string_view component) const {
    auto it = impl_->entities.find(std::string(id));
    auto c = impl_->components.find(std::string(component));
    if (it == impl_->entities.end() || c == impl_->components.end()) return nullptr;
    return ecs_get_id(world_, it->second.e, c->second);
}

const std::vector<std::string>& PlayWorld::tags(std::string_view id) const {
    auto it = impl_->entities.find(std::string(id));
    return it == impl_->entities.end() ? impl_->emptyTags : it->second.tags;
}
bool PlayWorld::hasTag(std::string_view id, std::string_view tag) const {
    const auto& t = tags(id);
    return std::find(t.begin(), t.end(), tag) != t.end();
}
std::string PlayWorld::name(std::string_view id) const {
    auto it = impl_->entities.find(std::string(id));
    return it == impl_->entities.end() ? std::string() : it->second.name;
}
std::optional<std::string> PlayWorld::parent(std::string_view id) const {
    auto it = impl_->entities.find(std::string(id));
    return it == impl_->entities.end() ? std::nullopt : it->second.parent;
}

std::string PlayWorld::spawn(const std::string& name_, const Json& components, const std::vector<std::string>& tags_, std::optional<std::string> parent_, std::string* error) {
    if (!components.is_object()) { if (error) *error = "components must be an object {Name: {...}}"; return ""; }
    for (auto it = components.begin(); it != components.end(); ++it) {   // ADR-0038: no silent skipping
        if (!Registry::global().find(it.key())) {
            std::string known; for (const auto* m : Registry::global().all()) known += (known.empty() ? "" : ", ") + m->name;
            if (error) *error = "unknown component '" + it.key() + "' (registered: " + known + ")";
            AKEIR_LOG(Error, "ecs", "spawn_unknown_component", "spawn refused: unknown component.", Json{{"name", name_}, {"component", it.key()}});
            return "";
        }
    }
    // §7.1: runtime-spawned id 는 결정적 (seed, tick, ordinal)
    Id id = Id::deterministic("entity", cfg_.seed, static_cast<std::uint64_t>(tick_), spawnOrdinal_++);
    const std::string sid = id.str();
    ecs_entity_desc_t ed{};
    ed.name = sid.c_str();
    EntityRec rec;
    rec.e = ecs_entity_init(world_, &ed);
    rec.name = name_;
    rec.tags = tags_;
    rec.parent = parent_;
    if (parent_) { auto pit = impl_->entities.find(*parent_); if (pit != impl_->entities.end()) ecs_add_id(world_, rec.e, ecs_pair(EcsChildOf, pit->second.e)); }
    impl_->entities[sid] = std::move(rec);
    ids_.insert(std::upper_bound(ids_.begin(), ids_.end(), sid), sid);
    std::vector<std::string> names;
    for (auto it = components.begin(); it != components.end(); ++it) names.push_back(it.key());
    std::sort(names.begin(), names.end());
    for (const auto& cname : names) {
        const ComponentMeta* meta = Registry::global().find(cname);
        ecs_entity_t comp = impl_->componentId(world_, *meta);
        void* ptr = ecs_ensure_id(world_, impl_->entities[sid].e, comp, meta->size);
        componentFromJson(*meta, ptr, components[cname], Visibility::Snapshot);   // runtime 값도 허용
        ecs_modified_id(world_, impl_->entities[sid].e, comp);
    }
    ensureBody(sid);
    for (auto& [hn, hook] : spawnHooks_) hook(*this, sid);
    if (error) error->clear();
    return sid;
}

std::string PlayWorld::spawnPrefab(std::string_view selector, const Json& overrides, const std::vector<std::string>& tags_, std::optional<std::string> parent_, std::string name_, std::string* error) {
    const PrefabInfo* info = nullptr;
    if (auto it = prefabs_.find(std::string(selector)); it != prefabs_.end()) info = &it->second;
    else {
        std::string_view want = selector.rfind("name:", 0) == 0 ? selector.substr(5) : selector;
        int matches = 0;
        for (const auto& [pid, p] : prefabs_) if (p.name == want) { info = &p; ++matches; }
        if (matches > 1) { if (error) *error = "prefab name '" + std::string(want) + "' is ambiguous; use the id"; return ""; }
    }
    if (!info) {
        std::string names; for (const auto& [pid, p] : prefabs_) names += (names.empty() ? "" : ", ") + p.name;
        if (error) *error = "no prefab '" + std::string(selector) + "' (prefabs: " + names + ")";
        return "";
    }
    Json root = Json::object();
    root["components"] = info->components;
    if (overrides.is_object()) {
        for (const auto& [ptr, v] : overrides.items()) {
            try { root[Json::json_pointer(ptr)] = v; }
            catch (const std::exception& e) { if (error) *error = "bad override pointer '" + ptr + "': " + e.what(); return ""; }
        }
    }
    std::vector<std::string> tags = info->tags;
    for (const auto& t : tags_) if (std::find(tags.begin(), tags.end(), t) == tags.end()) tags.push_back(t);
    return spawn(name_.empty() ? info->name : name_, root["components"], tags, std::move(parent_), error);
}

bool PlayWorld::addComponent(std::string_view id, std::string_view component, const Json& value, std::string* error) {
    auto it = impl_->entities.find(std::string(id));
    if (it == impl_->entities.end()) { if (error) *error = "no entity " + std::string(id); return false; }
    const ComponentMeta* meta = Registry::global().find(component);
    if (!meta) { if (error) *error = "unknown component '" + std::string(component) + "'"; return false; }
    ecs_entity_t comp = impl_->componentId(world_, *meta);
    if (ecs_has_id(world_, it->second.e, comp)) { if (error) *error = std::string(component) + " is already on " + std::string(id); return false; }
    void* ptr = ecs_ensure_id(world_, it->second.e, comp, meta->size);
    if (value.is_object() && !value.empty()) {
        ApplyResult applied = componentFromJson(*meta, ptr, value, Visibility::Snapshot);
        if (!applied.ok) {
            ecs_remove_id(world_, it->second.e, comp);
            if (error) { error->clear(); for (const auto& d : applied.diagnostics) if (d.level == Severity::Error) *error += (error->empty() ? "" : "; ") + d.ruleId + ": " + d.message.text; }
            return false;
        }
    }
    ecs_modified_id(world_, it->second.e, comp);
    ensureBody(std::string(id));
    if (error) error->clear();
    return true;
}

bool PlayWorld::removeComponent(std::string_view id, std::string_view component, std::string* error) {
    auto it = impl_->entities.find(std::string(id));
    if (it == impl_->entities.end()) { if (error) *error = "no entity " + std::string(id); return false; }
    if (component == "Transform") { if (error) *error = "Transform cannot be removed"; return false; }
    auto c = impl_->components.find(std::string(component));
    if (c == impl_->components.end() || !ecs_has_id(world_, it->second.e, c->second)) { if (error) *error = std::string(component) + " is not on " + std::string(id); return false; }
    ecs_remove_id(world_, it->second.e, c->second);
    if ((component == "RigidBody2D" || component == "Collider2D") && it->second.body) { physics_->destroyBody(it->second.body); it->second.body = 0; }
    if (error) error->clear();
    return true;
}

bool PlayWorld::addTag(std::string_view id, std::string_view tag) {
    auto it = impl_->entities.find(std::string(id));
    if (it == impl_->entities.end() || tag.empty()) return false;
    auto& t = it->second.tags;
    if (std::find(t.begin(), t.end(), tag) != t.end()) return false;
    t.emplace_back(tag);
    return true;
}

bool PlayWorld::removeTag(std::string_view id, std::string_view tag) {
    auto it = impl_->entities.find(std::string(id));
    if (it == impl_->entities.end()) return false;
    auto& t = it->second.tags;
    auto pos = std::find(t.begin(), t.end(), tag);
    if (pos == t.end()) return false;
    t.erase(pos);
    return true;
}

void PlayWorld::despawn(std::string_view id) {
    auto it = impl_->entities.find(std::string(id));
    if (it == impl_->entities.end()) return;
    if (it->second.body) physics_->destroyBody(it->second.body);
    ecs_delete(world_, it->second.e);
    impl_->entities.erase(it);
    ids_.erase(std::remove(ids_.begin(), ids_.end(), std::string(id)), ids_.end());
}

// ---------------------------------------------------------------- query (§16)

std::vector<std::string> PlayWorld::query(const std::vector<std::string>& with, const std::vector<std::string>& without) const {
    std::vector<std::string> out;
    for (const auto& id : ids_) {
        bool ok = true;
        for (const auto& w : with) {
            bool isTag = !w.empty() && w[0] == '#';
            if (isTag ? !hasTag(id, w.substr(1)) : !hasComponent(id, w)) { ok = false; break; }
        }
        if (!ok) continue;
        for (const auto& w : without) {
            bool isTag = !w.empty() && w[0] == '#';
            if (isTag ? hasTag(id, w.substr(1)) : hasComponent(id, w)) { ok = false; break; }
        }
        if (ok) out.push_back(id);
    }
    return out;
}

// ---------------------------------------------------------------- hash / dump / snapshot

std::uint64_t PlayWorld::hash() const {
    Hasher h;
    h.i64(tick_);
    const auto all = Registry::global().all();
    for (const auto& id : ids_) {
        h.str(id);
        for (const auto* meta : all) {
            const void* ptr = component(id, meta->name);
            if (!ptr) continue;
            h.str(meta->name);
            for (const auto& p : meta->props) { if (has(p.flags, PropFlags::Transient)) continue; hashJsonValue(h, p.get(ptr)); }
        }
    }
    for (const auto& [name, r] : rngs_) { h.str(name); for (auto s : r.state()) h.u64(s); }
    physics_->hashInto(h);
    return h.value();
}

Json PlayWorld::systemHashes() const {
    Json j = Json::object();
    Hasher rngH; for (const auto& [name, r] : rngs_) { rngH.str(name); for (auto s : r.state()) rngH.u64(s); }
    j["Rng"] = toHex64(rngH.value());
    Hasher ph; physics_->hashInto(ph);
    j["Physics"] = toHex64(ph.value());
    Hasher tf;
    for (const auto& id : ids_) if (const auto* t = get<Transform>(id)) { tf.str(id); tf.f32(t->position.x); tf.f32(t->position.y); tf.f32(t->position.z); }
    j["Transform"] = toHex64(tf.value());
    return j;
}

Json PlayWorld::dumpEntity(std::string_view id, Visibility v) const {
    auto it = impl_->entities.find(std::string(id));
    if (it == impl_->entities.end()) return Json(nullptr);
    Json j = Json::object();
    j["id"] = std::string(id);
    j["name"] = it->second.name;
    if (it->second.parent) j["parent"] = *it->second.parent; else j["parent"] = nullptr;
    j["tags"] = it->second.tags;
    Json comps = Json::object();
    for (const auto* meta : Registry::global().all()) {
        const void* ptr = component(id, meta->name);
        if (ptr) comps[meta->name] = componentToJson(*meta, ptr, v);
    }
    j["components"] = comps;
    return j;
}

Json PlayWorld::snapshot() const {
    Json s = Json::object();
    s["schemaVersion"] = 1;
    s["tick"] = tick_;
    s["seed"] = cfg_.seed;
    s["tickRate"] = cfg_.tickRate;
    s["world"] = worldId_;
    s["worldHash"] = toHex64(hash());
    s["systemHashes"] = systemHashes();
    Json rng = Json::object();
    for (const auto& [name, r] : rngs_) { Json st = Json::array(); for (auto v : r.state()) st.push_back(toHex64(v)); rng[name] = st; }
    s["rng"] = rng;
    Json bodies = Json::array();
    for (const auto& id : ids_) {
        const auto& rec = impl_->entities.at(id);
        if (!rec.body) continue;
        BodyState b = physics_->getState(rec.body);
        bodies.push_back(Json{{"entity", id}, {"p", Json::array({canonicalizeFloat(b.position.x), canonicalizeFloat(b.position.y)})}, {"q", canonicalizeFloat(b.angleRad)},
                              {"v", Json::array({canonicalizeFloat(b.velocity.x), canonicalizeFloat(b.velocity.y)})}, {"awake", b.awake}});
    }
    s["physics"] = Json{{"engine", physics_->backendName()}, {"bodies", bodies}};
    Json ents = Json::array();
    for (const auto& id : ids_) ents.push_back(dumpEntity(id, Visibility::Snapshot));
    s["entities"] = ents;
    return s;
}

RngStream& PlayWorld::rng(const std::string& streamName) {
    auto it = rngs_.find(streamName);
    if (it == rngs_.end()) it = rngs_.emplace(streamName, RngStream(cfg_.seed, streamName)).first;
    return it->second;
}

} // namespace akeir
