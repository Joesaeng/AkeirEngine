// Runtime_Project.cpp — 설계 문서 §6 (문서 모델), §7 (id index, selector), §29 (validate), §34 (prefab resolve), §5.3 (canonical save), §3.1 S-A (round-trip)
#include <doctest/doctest.h>
#include "akeir/runtime/Components.h"
#include "akeir/runtime/PhysicsLayers.h"
#include "akeir/runtime/Project.h"
#include "akeir/serialization/Canonical.h"
#include "TestPng.h"
#include "akeir/core/Id.h"

#include <filesystem>
#include <fstream>

using namespace akeir;
namespace fs = std::filesystem;

namespace {
std::string sampleProjectDir() {
    // frozen fixture (Tests/Fixtures/TestArena, ADR-0036) — never the user's Game/, which may be any game
    return std::string(AKEIR_TEST_FIXTURES) + "/TestArena";
}
std::string idOf(const Project& prj, const char* selector) {
    auto v = prj.resolveSelector(selector);
    REQUIRE(v.size() == 1);
    return v.front();
}
} // namespace

TEST_CASE("Project: load the TestArena fixture, index ids, resolve selectors (§6, §7.4)") {
    registerBuiltinComponents();
    std::string dir = sampleProjectDir();
    REQUIRE_FALSE(dir.empty());
    std::vector<Diagnostic> diags;
    auto prj = Project::load(dir, diags);
    REQUIRE(prj.has_value());
    CHECK(diags.empty());
    CHECK(prj->name() == "TestArena");
    CHECK(prj->tickRate() == 60);
    CHECK(prj->seed() == 381251);
    CHECK(prj->worldPaths().size() == 1);
    CHECK(prj->prefabPaths().size() == 3);

    std::string goblin01 = idOf(*prj, "name:Goblin_01");
    CHECK(goblin01.rfind("entity_", 0) == 0);
    auto loc = prj->locate(goblin01);
    REQUIRE(loc);
    CHECK(loc->kind == "entity");
    CHECK(loc->doc == "Worlds/TestArena.world.json");
    CHECK(*prj->entityPath(goblin01) == "TestArena/Arena/Encounter_01/Goblin_01");
    CHECK(idOf(*prj, "path:Arena/Encounter_01/Goblin_01") == goblin01);
    CHECK(idOf(*prj, "path:TestArena/Arena/Encounter_01/Goblin_01") == goblin01);
    CHECK(idOf(*prj, goblin01.substr(0, 24).c_str()) == goblin01);   // 고유 prefix (샘플 id 들은 같은 ms 에 생성되어 앞 18자가 같다)
    CHECK(prj->resolveSelector("name:Nobody").empty());
    CHECK(prj->resolveSelector("entity_").empty());
    CHECK(*prj->defaultWorld() == idOf(*prj, "name:TestArena"));
}

TEST_CASE("Project: prefab resolve with base + set, entity instance overrides (§34)") {
    registerBuiltinComponents();
    std::vector<Diagnostic> diags;
    auto prj = Project::load(sampleProjectDir(), diags);
    REQUIRE(prj);
    std::string elite = idOf(*prj, "name:GoblinElite");
    auto c = prj->resolvePrefab(elite, &diags);
    REQUIRE(c);
    CHECK(diags.empty());
    CHECK((*c)["Collider2D"]["radius"] == 0.55);                 // set
    CHECK((*c)["Collider2D"]["shape"] == "capsule");             // base 에서 상속 (absent = inherit)
    CHECK((*c)["SpriteRenderer"]["tint"] == Json::array({1, 0.5, 0.5, 1}));
    CHECK((*c)["RigidBody2D"]["linearDamping"] == 4);            // 상속

    std::string g3 = idOf(*prj, "name:Goblin_03");               // GoblinElite 인스턴스 + position override
    auto ec = prj->resolveEntityComponents(g3, &diags);
    REQUIRE(ec);
    CHECK((*ec)["Transform"]["position"] == Json::array({7, -2, 0}));
    CHECK((*ec)["Collider2D"]["radius"] == 0.55);
    CHECK((*ec)["Transform"]["scale"] == Json::array({1.3, 1.3, 1}));

    std::string arena = idOf(*prj, "name:Arena");                // 비인스턴스
    auto ac = prj->resolveEntityComponents(arena, &diags);
    REQUIRE(ac);
    CHECK(ac->contains("Transform"));
    CHECK(diags.empty());
}

TEST_CASE("Project: validate passes on the sample except canonical-form warnings — errors are §79 diagnostics (§29)") {
    registerBuiltinComponents();
    std::vector<Diagnostic> diags;
    auto prj = Project::load(sampleProjectDir(), diags);
    REQUIRE(prj);
    auto v = prj->validate();
    for (const auto& d : v) {
        // 샘플 파일은 python json.dumps 로 썼으므로 JSON_NOT_CANONICAL 경고만 허용
        CAPTURE(d.ruleId); CAPTURE(d.message.text);
        CHECK(d.ruleId == "JSON_NOT_CANONICAL");
        CHECK(d.level == Severity::Warning);
        CHECK(d.fixes[0].applicability == Applicability::MachineApplicable);
    }
}

TEST_CASE("Project: validation catches dependency, dangling ref, override target, cycle, duplicate id, unknown component") {
    registerBuiltinComponents();
    Project prj = Project::create((fs::temp_directory_path() / "akeir_prj_validate").string(), "V");
    const std::string w = "world_01j5xq8z3mf0n9k2c7p4rtvw6y";
    const std::string pA = "prefab_01j5xq8z3mf0n9k2c7p4rtvw60";
    const std::string pB = "prefab_01j5xq8z3mf0n9k2c7p4rtvw61";
    const std::string e1 = "entity_01j5xq8z3mf0n9k2c7p4rtvw70";
    const std::string e2 = "entity_01j5xq8z3mf0n9k2c7p4rtvw71";
    Json prefabA = Json{{"$schema", "game://schema/prefab/1"}, {"schemaVersion", 1}, {"id", pA}, {"name", "A"}, {"base", pB}, {"set", Json{{"/components/Nope/x", 1}}}};
    Json prefabB = Json{{"$schema", "game://schema/prefab/1"}, {"schemaVersion", 1}, {"id", pB}, {"name", "B"}, {"base", pA}};   // cycle
    Json world = Json{{"$schema", "game://schema/world/1"}, {"schemaVersion", 1}, {"id", w}, {"name", "W"}, {"entities", Json::object()}};
    world["entities"][e1] = Json{{"name", "NoTransform"}, {"parent", nullptr}, {"order", "a0"},
                                 {"components", Json{{"SpriteRenderer", Json{{"sprite", "asset_01j5xq8z3mf0n9k2c7p4rtvw6y#sprites/x"}}},
                                                     {"Bogus", Json::object()}}}};
    world["entities"][e2] = Json{{"name", "Dup"}, {"parent", e1}, {"order", "a0"},
                                 {"components", Json{{"Transform", Json::object()}, {"Collider2D", Json{{"radius", -1}}}}}};
    Json world2 = world; world2["id"] = "world_01j5xq8z3mf0n9k2c7p4rtvw6z";   // e1/e2 중복 id
    prj.setDocument("Prefabs/A.prefab.json", prefabA);
    prj.setDocument("Prefabs/B.prefab.json", prefabB);
    prj.setDocument("Worlds/W.world.json", world);
    prj.setDocument("Worlds/W2.world.json", world2);
    auto v = prj.validate();
    auto count = [&](const char* rule) { int n = 0; for (const auto& d : v) if (d.ruleId == rule) ++n; return n; };
    CHECK(count("DUPLICATE_PERSISTENT_ID") == 2);          // e1, e2
    CHECK(count("PREFAB_CHAIN_CYCLE") >= 1);
    CHECK(count("COMPONENT_UNKNOWN") >= 1);
    CHECK(count("COMPONENT_DEPENDENCY_MISSING") >= 1);     // SpriteRenderer requires Transform
    CHECK(count("PROPERTY_OUT_OF_RANGE") >= 1);            // radius -1
    for (const auto& d : v) if (d.ruleId == "COMPONENT_DEPENDENCY_MISSING") {
        REQUIRE(!d.fixes.empty());
        CHECK(d.fixes[0].commands[0].op == "component.add");
        CHECK(d.fixes[0].commands[0].args["component"] == "Transform");
        CHECK(d.fixes[0].applicability == Applicability::MachineApplicable);
        CHECK(d.physical->uri.rfind("Worlds/W", 0) == 0);   // W 와 W2 (중복) 둘 다
    }
}

TEST_CASE("Project: canonical save round-trips byte-identically and preserves §5.3 ordering") {
    registerBuiltinComponents();
    std::vector<Diagnostic> diags;
    auto src = Project::load(sampleProjectDir(), diags);
    REQUIRE(src);
    // 임시 디렉터리로 복사 저장
    fs::path tmp = fs::temp_directory_path() / "akeir_prj_roundtrip";
    fs::remove_all(tmp);
    fs::create_directories(tmp);
    Project copy = Project::create(tmp.string(), src->name(), src->tickRate());
    for (const auto& [p, d] : src->documents()) copy.setDocument(p, d);
    auto failed = copy.saveAll();
    CHECK(failed.empty());

    // 1) 파일 텍스트가 canonical 이다
    std::ifstream in(tmp / "Prefabs" / "GoblinElite.prefab.json", std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    CHECK(text.rfind("{\n  \"$schema\": \"game://schema/prefab/1\",\n  \"schemaVersion\": 1,\n  \"id\": \"prefab_", 0) == 0);
    CHECK(text.find("\"base\"") < text.find("\"set\""));          // 헤더 키 순서
    CHECK(text.back() == '\n');

    // 2) 다시 로드 → 같은 문서 (canonical 형태끼리 비교)
    std::vector<Diagnostic> d2;
    auto back = Project::load(tmp.string(), d2);
    REQUIRE(back);
    CHECK(d2.empty());
    for (const auto& [p, d] : src->documents()) CHECK(Project::canonicalizeDocument(d) == *back->document(p));

    // 3) 두 번째 저장은 byte-identical (저장할 때마다 같아야 한다, §5.3)
    auto failed2 = back->saveAll();
    CHECK(failed2.empty());
    std::ifstream in2(tmp / "Prefabs" / "GoblinElite.prefab.json", std::ios::binary);
    std::string text2((std::istreambuf_iterator<char>(in2)), std::istreambuf_iterator<char>());
    in2.close();
    CHECK(text2 == text);
    // 4) 재로드한 프로젝트의 validate 는 경고도 없다 (canonical 이므로)
    auto v = back->validate();
    CHECK(v.empty());

    // component 내부 property 순서 = reflection 선언 순서 (Transform: position, rotation, scale)
    const Json& player = *back->document("Prefabs/Player.prefab.json");
    auto it = player["components"]["Transform"].begin();
    CHECK(it.key() == "position"); ++it; CHECK(it.key() == "rotation"); ++it; CHECK(it.key() == "scale");
    // component 이름 A→Z
    auto c = player["components"].begin();
    CHECK(c.key() == "Collider2D");
    fs::remove_all(tmp);
}

TEST_CASE("Project: reference graph — who points at a prefab / entity (§19)") {
    registerBuiltinComponents();
    std::vector<Diagnostic> diags;
    auto prj = Project::load(sampleProjectDir(), diags);
    REQUIRE(prj);
    const std::string goblin = idOf(*prj, "name:Goblin");
    auto in = prj->referencesTo(goblin);
    // Goblin_01/02 (prefab) + GoblinElite (base); Goblin_03 은 GoblinElite 인스턴스
    int prefabRefs = 0, baseRefs = 0;
    for (const auto& r : in) { if (r.kind == "prefab") ++prefabRefs; if (r.kind == "base") ++baseRefs; }
    CHECK(prefabRefs == 2);
    CHECK(baseRefs == 1);
    auto elite = prj->referencesTo(idOf(*prj, "name:GoblinElite"));
    REQUIRE(elite.size() == 1);
    CHECK(elite[0].kind == "prefab");
    CHECK(elite[0].from == idOf(*prj, "name:Goblin_03"));
    const std::string arena = idOf(*prj, "path:TestArena/Arena");
    auto children = prj->referencesTo(arena);
    CHECK(children.size() >= 2);            // Player, Encounter_01 (parent)
    for (const auto& r : children) CHECK(r.kind == "parent");
    const std::string world = *prj->defaultWorld();
    auto dw = prj->referencesTo(world);
    REQUIRE(dw.size() == 1);
    CHECK(dw[0].kind == "defaultWorld");
    CHECK(dw[0].from == "project");
    const std::string g1 = idOf(*prj, "name:Goblin_01");
    auto out = prj->referencesFrom(g1);
    REQUIRE(out.size() == 2);               // parent + prefab
    CHECK(out[0].kind == "parent");
    CHECK(out[1].kind == "prefab");
    CHECK(out[1].detail == goblin);
}

TEST_CASE("Project::create with an empty root resolves to the current directory (fs::absolute(\"\") throws on MSVC STL 14.51+)") {
    // `akeir capabilities` / MCP tools/list build a scratch project with root "" — must not depend on the STL's absolute("") behavior.
    Project scratch = Project::create("", "scratch");
    CHECK(scratch.rootDir() == fs::current_path().generic_string());
    std::vector<Diagnostic> diags;
    CHECK_FALSE(Project::load("", diags).has_value());   // no project.json in the test cwd — a diagnostic, not an exception
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].ruleId == "PROJECT_NOT_FOUND");
}

// ---- asset sidecars (§37, ADR-0037) ----
namespace {
struct AssetProject {
    fs::path dir;
    std::string assetId = Id::generate("asset").str();
    AssetProject() {
        registerBuiltinComponents();
        dir = fs::temp_directory_path() / "akeir_asset_test";
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir / "Assets" / "Textures");
        fs::create_directories(dir / "Worlds");
        akeirtest::writePng((dir / "Assets" / "Textures" / "sheet.png").string(), 4, 2, std::vector<std::uint8_t>(4 * 2 * 4, 255));
        Json prj = Json{{"$schema", "game://schema/project/1"}, {"schemaVersion", 1}, {"name", "A"}, {"tickRate", 60}, {"seed", 1}, {"defaultWorld", "world_01j5xq8z3mf0n9k2c7p4rtvw6y"}};
        std::ofstream(dir / "project.json") << prj.dump(2);
    }
    ~AssetProject() { std::error_code ec; fs::remove_all(dir, ec); }
    void sidecar(const Json& subs, const char* source = "Assets/Textures/sheet.png") {
        Json m = Json{{"$schema", "game://schema/asset-meta/1"}, {"schemaVersion", 1}, {"id", assetId}, {"source", source}, {"importer", "Texture2D"}, {"settings", Json{{"filter", "nearest"}, {"pixelsPerUnit", 2}}}, {"subAssets", subs}};
        std::ofstream(dir / "Assets" / "Textures" / "sheet.png.meta.json") << m.dump(2);
    }
    void world(const std::string& spriteRef) {
        Json w = Json{{"$schema", "game://schema/world/1"}, {"schemaVersion", 1}, {"id", "world_01j5xq8z3mf0n9k2c7p4rtvw6y"}, {"name", "W"},
                      {"entities", Json{{"entity_01j5xq8z3mf0n9k2c7p4rtvw70", Json{{"name", "S"}, {"components", Json{{"Transform", Json::object()}, {"SpriteRenderer", Json{{"sprite", spriteRef}}}}}}}}}};
        std::ofstream(dir / "Worlds" / "W.world.json") << w.dump(2);
    }
    std::vector<std::string> rules() {
        std::vector<Diagnostic> d;
        auto prj = Project::load(dir.string(), d);
        REQUIRE(prj);
        std::vector<std::string> out;
        for (const auto& x : prj->validate()) out.push_back(x.ruleId);
        return out;
    }
};
bool has(const std::vector<std::string>& v, const char* r) { for (const auto& x : v) if (x == r) return true; return false; }
} // namespace

TEST_CASE("Assets: a sidecar is loaded as a document, indexed by id, and sprite refs resolve (ADR-0037)") {
    AssetProject t;
    t.sidecar(Json::array({Json{{"name", "a"}, {"kind", "sprite"}, {"rect", Json::array({0, 0, 2, 2})}}, Json{{"name", "b"}, {"kind", "sprite"}, {"rect", Json::array({2, 0, 2, 2})}, {"pivot", Json::array({0, 1})}}}));
    t.world(t.assetId + "#sprites/b");
    std::vector<Diagnostic> d;
    auto prj = Project::load(t.dir.string(), d);
    REQUIRE(prj);
    CHECK(prj->assets().size() == 1);
    CHECK(prj->assetDocs() == std::vector<std::string>{"Assets/Textures/sheet.png.meta.json"});
    auto loc = prj->locate(t.assetId);
    REQUIRE(loc);
    CHECK(loc->kind == "asset");
    const AssetMeta* a = prj->assets().find(t.assetId);
    REQUIRE(a);
    CHECK(a->pixelsPerUnit == 2.f);
    CHECK(a->sprites.size() == 2);
    std::string why;
    const SpriteRegion* r = prj->assets().resolveSprite(Ref{t.assetId + "#sprites/b"}, nullptr, &why);
    REQUIRE(r);
    CHECK(r->x == 2); CHECK(r->pivot.y == 1.f); CHECK(why.empty());
    CHECK(prj->assets().resolveSprite(Ref{t.assetId + "#sprites/zzz"}, nullptr, &why) == nullptr);
    CHECK(why.find("no sprite 'zzz'") != std::string::npos);
    for (const auto& x : prj->validate()) CHECK(x.ruleId == "JSON_NOT_CANONICAL");   // only the hand-written formatting, no asset errors
    // refs: the entity's sprite property points at the asset
    auto refs = prj->referencesTo(t.assetId);
    REQUIRE(refs.size() == 1);
    CHECK(refs[0].detail == "SpriteRenderer.sprite");
}

TEST_CASE("Assets: validate reports missing source, bad rects, unknown sub-assets and dangling asset ids (ADR-0037)") {
    AssetProject t;
    t.sidecar(Json::array({Json{{"name", "a"}, {"kind", "sprite"}, {"rect", Json::array({0, 0, 2, 2})}}}));
    t.world(t.assetId + "#sprites/nope");
    auto r1 = t.rules();
    CHECK(has(r1, "ASSET_SUBASSET_MISSING"));
    t.world("asset_01j5xq8z3mf0n9k2c7p4rtvw99#sprites/a");   // no such sidecar
    CHECK(has(t.rules(), "REF_DANGLING"));
    t.world(t.assetId + "#sprites/a");
    t.sidecar(Json::array({Json{{"name", "a"}, {"kind", "sprite"}, {"rect", Json::array({3, 0, 2, 2})}}}));   // exceeds the 4x2 image
    auto r3 = t.rules();
    CHECK(has(r3, "ASSET_SUBASSET_RECT_INVALID"));
    CHECK(has(r3, "ASSET_SUBASSET_MISSING"));                  // the bad rect was dropped, so the ref no longer resolves
    t.sidecar(Json::array({Json{{"name", "a"}, {"kind", "sprite"}, {"rect", Json::array({0, 0, 2, 2})}}}), "Assets/Textures/missing.png");
    CHECK(has(t.rules(), "ASSET_SOURCE_MISSING"));
}

// ---- collision layers (ADR-0043) ----
TEST_CASE("PhysicsLayers: project.json physics.layers → symmetric category/mask bits; undeclared partners are problems") {
    std::vector<std::pair<std::string, std::string>> problems;
    Json pj = Json{{"physics", Json{{"layers", Json{{"Player", Json::array({"Enemy", "Pickup"})}, {"Enemy", Json::array({"Enemy"})}, {"Pickup", Json::array()}, {"Effect", Json::array()}, {"Weapon", Json::array({"Enemy", "Ghost"})}}}}}};
    PhysicsLayers pl = PhysicsLayers::fromProjectJson(pj, &problems);
    CHECK(pl.declared());
    CHECK(pl.names().size() == 5);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].second.find("undeclared layer 'Ghost'") != std::string::npos);
    CHECK(pl.collides("Player", "Enemy"));
    CHECK(pl.collides("Enemy", "Player"));          // symmetric even though only Player listed Enemy
    CHECK(pl.collides("Pickup", "Player"));          // Player listed Pickup
    CHECK(pl.collides("Enemy", "Enemy"));
    CHECK_FALSE(pl.collides("Pickup", "Enemy"));
    CHECK_FALSE(pl.collides("Effect", "Player"));
    CHECK_FALSE(pl.collides("Effect", "Effect"));
    CHECK(pl.collides("Weapon", "Enemy"));
    CHECK(pl.bits("Player").category == 1ULL);
    CHECK(pl.bits("Enemy").category == 2ULL);
    CHECK(pl.bits("Effect").mask == 0ULL);
    CHECK(pl.bits("Nope").mask == ~0ULL);            // undeclared → collide with everything (validate reports it)
    PhysicsLayers none = PhysicsLayers::fromProjectJson(Json::object());
    CHECK_FALSE(none.declared());
    CHECK(none.collides("A", "B"));
    CHECK(pl.toJson()["Enemy"] == Json::array({"Player", "Enemy", "Weapon"}));
}

TEST_CASE("Project: validate reports Collider2D.layer names that are not declared in physics.layers (ADR-0043)") {
    AssetProject t;   // a minimal temp project
    Json prj = Json{{"$schema", "game://schema/project/1"}, {"schemaVersion", 1}, {"name", "L"}, {"tickRate", 60}, {"seed", 1}, {"defaultWorld", "world_01j5xq8z3mf0n9k2c7p4rtvw6y"},
                    {"physics", Json{{"layers", Json{{"Player", Json::array({"Enemy"})}, {"Enemy", Json::array()}}}}}};
    std::ofstream(t.dir / "project.json") << prj.dump(2);
    Json w = Json{{"$schema", "game://schema/world/1"}, {"schemaVersion", 1}, {"id", "world_01j5xq8z3mf0n9k2c7p4rtvw6y"}, {"name", "W"},
                  {"entities", Json{{"entity_01j5xq8z3mf0n9k2c7p4rtvw70", Json{{"name", "P"}, {"components", Json{{"Transform", Json::object()}, {"Collider2D", Json{{"layer", "Player"}}}, {"RigidBody2D", Json::object()}}}}},
                                    {"entity_01j5xq8z3mf0n9k2c7p4rtvw71", Json{{"name", "X"}, {"components", Json{{"Transform", Json::object()}, {"Collider2D", Json{{"layer", "Lava"}}}, {"RigidBody2D", Json::object()}}}}}}}};
    std::ofstream(t.dir / "Worlds" / "W.world.json") << w.dump(2);
    std::vector<Diagnostic> d;
    auto p = Project::load(t.dir.string(), d);
    REQUIRE(p);
    CHECK(p->physicsLayers().declared());
    int unknown = 0;
    for (const auto& x : p->validate()) if (x.ruleId == "PHYSICS_LAYER_UNKNOWN") { ++unknown; CHECK(x.message.text.find("'Lava'") != std::string::npos); }
    CHECK(unknown == 1);
}
