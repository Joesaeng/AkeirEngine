// ECS_PlayWorld.cpp — 설계 문서 §3.1 (Flecs 투영 round-trip), §16 (query), §20.1/§22.1 (T0 run-twice), §25 (dump), §26.1 (snapshot), §57 (Box2D sync), §71 (시나리오 2: WASD 이동)
#include <doctest/doctest.h>
#include "GameComponents.h"
#include "GameSystems.h"
#include "akeir/ecs/PlayWorld.h"
#include "akeir/runtime/Components.h"
#include "akeir/runtime/Project.h"

#include <filesystem>

using namespace akeir;
namespace fs = std::filesystem;

namespace {
std::string sampleDir() {
    // frozen fixture (Tests/Fixtures/TestArena, ADR-0036) — never the user's Game/, which may be any game
    return std::string(AKEIR_TEST_FIXTURES) + "/TestArena";
}
struct Fixture {
    std::vector<Diagnostic> diags;
    std::optional<Project> prj;
    Fixture() { registerBuiltinComponents(); game::registerGameComponents(); prj = Project::load(sampleDir(), diags); }
    std::unique_ptr<PlayWorld> world(std::uint64_t seed = 381251) {
        PlayWorldConfig cfg; cfg.seed = seed; cfg.tickRate = 60;
        std::vector<Diagnostic> d;
        auto w = PlayWorld::build(*prj, *prj->defaultWorld(), cfg, d);
        for (const auto& x : d) { CAPTURE(x.ruleId); CAPTURE(x.message.text); }
        REQUIRE(d.empty());
        REQUIRE(w);
        game::registerGameSystems(*w);
        return w;
    }
    std::string one(const char* sel) { auto v = prj->resolveSelector(sel); REQUIRE(v.size() == 1); return v.front(); }
};
} // namespace

TEST_CASE("PlayWorld: build projects authoring docs into Flecs + Box2D (§3.1 S-A)") {
    Fixture f;
    auto w = f.world();
    CHECK(w->entityIds().size() == 7);
    std::string player = f.one("path:Arena/Player");
    CHECK(w->hasComponent(player, "Transform"));
    CHECK(w->hasComponent(player, "PlayerController"));
    CHECK(w->hasTag(player, "player"));                         // prefab 의 tags 병합
    CHECK(w->get<game::Health>(player)->max == 100.f);
    CHECK(w->get<game::Health>(player)->current == 100.f);      // aggregate 기본값 (runtimeOnly 는 파일에 없다)
    std::string g3 = f.one("name:Goblin_03");
    CHECK(w->get<game::Health>(g3)->max == 80.f);               // GoblinElite set override
    CHECK(w->get<Collider2D>(g3)->radius == 0.55f);
    CHECK(w->get<Transform>(g3)->position.x == 7.f);
    CHECK(w->parent(g3) == f.one("name:Encounter_01"));
    CHECK(w->physics().bodyCount() == 4);                       // Player + 3 Goblins (RigidBody2D + Collider2D)
    CHECK(w->query({"EnemyAI"}).size() == 3);
    CHECK(w->query({"#enemy"}).size() == 3);
    CHECK(w->query({"Transform"}, {"RigidBody2D"}).size() == 3); // Arena, Camera, Encounter
    CHECK(w->query({"#elite"}) == std::vector<std::string>{g3});
}

TEST_CASE("PlayWorld: WASD input moves the player through physics (§71 scenario 2, §57)") {
    Fixture f;
    auto w = f.world();
    std::string player = f.one("path:Arena/Player");
    float x0 = w->get<Transform>(player)->position.x;
    RunConfig cfg; cfg.ticks = 5; cfg.seed = 381251; cfg.hashEvery = 0;
    ScriptedInputSource in;
    for (int t = 0; t < 60; ++t) { InputFrame fr; fr.tick = t; fr.actions["MoveX"] = 1.0f; in.add(fr); }
    RunResult r = Application::runHeadless(cfg, *w, in);
    CHECK(r.ticksRun == 5);
    CHECK(w->get<RigidBody2D>(player)->velocity.x > 0.f);       // physics → RigidBody2D.velocity 동기화 (접촉 전)
    cfg.ticks = 55;
    ScriptedInputSource in2;
    for (int t = 0; t < 55; ++t) { InputFrame fr; fr.tick = t; fr.actions["MoveX"] = 1.0f; in2.add(fr); }
    Application::runHeadless(cfg, *w, in2);
    float x1 = w->get<Transform>(player)->position.x;
    CHECK(x1 > x0 + 2.0f);                                      // speed 5 m/s × 1s, 고블린과의 접촉·damping 고려해도 2m 이상
    // 고블린은 chase 상태로 플레이어 쪽으로 이동했다
    std::string g1 = f.one("name:Goblin_01");
    CHECK(w->get<game::EnemyAI>(g1)->state != game::AiState::Idle);
    CHECK(w->get<game::EnemyAI>(g1)->target.value == player);
}

TEST_CASE("PlayWorld: T0 — two builds with same seed/input produce identical hash sequences (§22.1)") {
    Fixture f;
    auto a = f.world(), b = f.world();
    RunConfig cfg; cfg.ticks = 240; cfg.seed = 381251; cfg.hashEvery = 20;
    ScriptedInputSource ia, ib;
    for (int t = 0; t < 240; ++t) { InputFrame fr; fr.tick = t; fr.actions["MoveX"] = (t < 120) ? 1.0f : -1.0f; fr.actions["MoveY"] = 0.5f; ia.add(fr); ib.add(fr); }
    RunResult ra = Application::runHeadless(cfg, *a, ia);
    RunResult rb = Application::runHeadless(cfg, *b, ib);
    REQUIRE(ra.hashes.size() == rb.hashes.size());
    for (std::size_t i = 0; i < ra.hashes.size(); ++i) { CHECK(ra.hashes[i].world == rb.hashes[i].world); CHECK(ra.hashes[i].systems == rb.hashes[i].systems); }
    CHECK(ra.finalHash == rb.finalHash);
    CHECK(a->snapshot() == b->snapshot());
    // 적이 공격해 플레이어 체력이 줄었다 (시나리오 진행 확인)
    std::string player = f.one("path:Arena/Player");
    CHECK(a->get<game::Health>(player)->current < 100.f);
}

TEST_CASE("PlayWorld: dump and snapshot shapes (§25, §26.1)") {
    Fixture f;
    auto w = f.world();
    std::string g1 = f.one("name:Goblin_01");
    Json d = w->dumpEntity(g1);
    CHECK(d["id"] == g1);
    CHECK(d["name"] == "Goblin_01");
    CHECK(d["components"]["EnemyAI"]["state"] == "idle");       // snapshot visibility → runtimeOnly 포함
    CHECK(d["components"]["Health"]["current"] == 30.0);
    CHECK(d["tags"] == Json::array({"enemy"}));
    Json s = w->snapshot();
    CHECK(s["tick"] == 0);                                     // build 직후 = 0 tick 시뮬레이션됨
    CHECK(s["seed"] == 381251);
    CHECK(s["physics"]["engine"] == "box2d-3.1.1");
    CHECK(s["physics"]["bodies"].size() == 4);
    CHECK(s["entities"].size() == 7);
    CHECK(s["worldHash"].get<std::string>().rfind("0x", 0) == 0);
    CHECK(s["systemHashes"].contains("Physics"));
    // entities 는 id 순
    for (std::size_t i = 1; i < s["entities"].size(); ++i) CHECK(s["entities"][i - 1]["id"] < s["entities"][i]["id"]);
}

TEST_CASE("PlayWorld: runtime spawn gets deterministic v8 ids and joins physics (§7.1)") {
    Fixture f;
    auto a = f.world(), b = f.world();
    Json comps = Json{{"Transform", Json{{"position", Json::array({1, 2, 0})}}}, {"Collider2D", Json{{"shape", "circle"}, {"radius", 0.3}}}, {"RigidBody2D", Json{{"type", "dynamic"}}}};
    std::string ia = a->spawn("Spawned", comps, {"enemy"});
    std::string ib = b->spawn("Spawned", comps, {"enemy"});
    CHECK(ia == ib);
    CHECK(ia.rfind("entity_", 0) == 0);
    CHECK(a->hasEntity(ia));
    CHECK(a->physics().bodyCount() == 5);
    CHECK(a->query({"#enemy"}).size() == 4);
    CHECK(a->hash() == b->hash());
    a->despawn(ia);
    CHECK_FALSE(a->hasEntity(ia));
    CHECK(a->physics().bodyCount() == 4);
}

// ---- ADR-0038: system phases, prefab spawn, runtime component/tag mutation, unknown-component spawn ----
TEST_CASE("PlayWorld: PostPhysics systems see this tick's contact events, PrePhysics ones see last tick's (ADR-0038)") {
    Fixture f;
    auto w = f.world();
    std::vector<std::size_t> pre, post;
    w->addSystem("Probe.Post", [&](PlayWorld& pw, const InputFrame&, const SimTime&) { post.push_back(pw.contactEvents().size()); }, PlayWorld::SystemPhase::PostPhysics);
    w->addSystem("Probe.Pre", [&](PlayWorld& pw, const InputFrame&, const SimTime&) { pre.push_back(pw.contactEvents().size()); });
    auto names = w->systemNames();
    CHECK(names.back() == "Probe.Post");                         // execution order: every PrePhysics system first
    CHECK(std::find(names.begin(), names.end(), "Probe.Pre") < std::find(names.begin(), names.end(), "Probe.Post"));
    SimTime st; st.tickRate = 60;
    for (int i = 0; i < 240; ++i) { InputFrame fr; fr.tick = st.tick; w->tick(fr, st); st.advance(); }
    REQUIRE(pre.size() == 240); REQUIRE(post.size() == 240);
    std::size_t first = 0; while (first < post.size() && post[first] == 0) ++first;
    REQUIRE_MESSAGE(first < post.size(), "goblins never touched the player in 240 ticks");
    CHECK(pre[first] == 0);                                      // the PrePhysics system ran before the physics step of that tick
    CHECK(pre[first + 1] == post[first]);                        // …and only sees those contacts one tick later
}

TEST_CASE("PlayWorld: spawnPrefab instantiates an authoring prefab with overrides, merged tags and a physics body (ADR-0038)") {
    Fixture f;
    auto w = f.world();
    REQUIRE(w->prefabs().size() == 3);
    const std::size_t bodies = w->physics().bodyCount();
    std::string err;
    std::string g = w->spawnPrefab("Goblin", Json{{"/components/Health/max", 5}, {"/components/Transform/position", Json::array({9, 9, 0})}}, {"wave1"}, std::nullopt, "", &err);
    REQUIRE_MESSAGE(!g.empty(), err);
    CHECK(w->name(g) == "Goblin");
    CHECK(w->get<game::Health>(g)->max == 5.f);
    CHECK(w->get<game::Health>(g)->current == 5.f);              // spawn hooks ran (HealthInit)
    CHECK(w->get<Transform>(g)->position.x == 9.f);
    CHECK(w->hasTag(g, "enemy"));                                // from the prefab
    CHECK(w->hasTag(g, "wave1"));                                // merged
    CHECK(w->physics().bodyCount() == bodies + 1);
    std::string e = w->spawnPrefab("name:GoblinElite", Json::object(), {}, std::nullopt, "Boss", &err);
    REQUIRE_MESSAGE(!e.empty(), err);
    CHECK(w->name(e) == "Boss");
    CHECK(w->get<game::Health>(e)->max == 80.f);                 // base chain + set override resolved
    CHECK(w->hasTag(e, "elite"));
    const std::string pid = w->prefabs().begin()->first;
    CHECK_FALSE(w->spawnPrefab(pid, Json::object(), {}, std::nullopt, "", &err).empty());   // by id
    CHECK(w->spawnPrefab("Dragon", Json::object(), {}, std::nullopt, "", &err).empty());
    CHECK(err.find("no prefab 'Dragon'") != std::string::npos);
    CHECK(w->spawnPrefab("Goblin", Json{{"components/Health/max", 1}}, {}, std::nullopt, "", &err).empty());   // pointers start with /
    CHECK(err.find("override pointer") != std::string::npos);
}

TEST_CASE("PlayWorld: spawn refuses unknown components instead of skipping them (ADR-0038)") {
    Fixture f;
    auto w = f.world();
    std::string err;
    CHECK(w->spawn("X", Json{{"Transform", Json::object()}, {"Teleporter", Json::object()}}, {}, std::nullopt, &err).empty());
    CHECK(err.find("unknown component 'Teleporter'") != std::string::npos);
    CHECK(err.find("registered:") != std::string::npos);
    CHECK_FALSE(w->spawn("Y", Json{{"Transform", Json::object()}}, {}, std::nullopt, &err).empty());
    CHECK(err.empty());
}

TEST_CASE("PlayWorld: runtime add/remove of components and tags is immediate, affects hash and physics, and is deterministic (ADR-0038)") {
    Fixture f;
    auto run = [&](bool mutate) {
        auto w = f.world();
        std::string id = w->spawn("Crate", Json{{"Transform", Json{{"position", Json::array({3, 3, 0})}}}});
        const std::size_t bodies = w->physics().bodyCount();
        std::uint64_t h0 = w->hash();
        std::string err;
        if (mutate) {
            REQUIRE(w->addComponent(id, "Collider2D", Json{{"shape", "circle"}, {"radius", 0.3}}, &err));
            CHECK(w->physics().bodyCount() == bodies);                 // no body without RigidBody2D
            REQUIRE(w->addComponent(id, "RigidBody2D", Json::object(), &err));
            CHECK(w->physics().bodyCount() == bodies + 1);             // created once all three are present
            CHECK(w->hasComponent(id, "Collider2D"));
            CHECK(w->get<Collider2D>(id)->radius == 0.3f);
            CHECK_FALSE(w->addComponent(id, "Collider2D", Json::object(), &err));
            CHECK(err.find("already") != std::string::npos);
            CHECK_FALSE(w->addComponent(id, "Nope", Json::object(), &err));
            CHECK_FALSE(w->addComponent(id, "Health", Json{{"max", "many"}}, &err));   // type error → not added
            CHECK_FALSE(w->hasComponent(id, "Health"));
            CHECK_FALSE(w->removeComponent(id, "Transform", &err));
            CHECK(w->addTag(id, "loot"));
            CHECK_FALSE(w->addTag(id, "loot"));
            CHECK(w->hasTag(id, "loot"));
            CHECK(w->query({"#loot"}) == std::vector<std::string>{id});
            CHECK(w->removeTag(id, "loot"));
            CHECK_FALSE(w->hasTag(id, "loot"));
            CHECK(w->hash() != h0);
            REQUIRE(w->removeComponent(id, "RigidBody2D", &err));
            CHECK(w->physics().bodyCount() == bodies);                 // body destroyed with it
            CHECK_FALSE(w->hasComponent(id, "RigidBody2D"));
            CHECK(w->hasComponent(id, "Collider2D"));
        }
        SimTime st; st.tickRate = 60;
        for (int i = 0; i < 30; ++i) { InputFrame fr; fr.tick = st.tick; w->tick(fr, st); st.advance(); }
        return w->hash();
    };
    std::uint64_t a = run(true), b = run(true), c = run(false);
    CHECK(a == b);
    CHECK(a != c);
}
