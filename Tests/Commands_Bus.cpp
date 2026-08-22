// Commands_Bus.cpp — 설계 문서 §8 (CommandBus, Mutation command), §9 (tx, commit 절차, journal), §10 (undo/redo, conflict), §49 (apply, $ref, idempotency),
// §50 (dry-run), §78.1 (인스턴스 override 매핑), §5.3 (commit 후 파일이 canonical)
//
// TestArena fixture 를 임시 디렉터리로 복사해 실제 파일 commit 까지 검증한다.
#include <doctest/doctest.h>
#include "akeir/commands/CommandBus.h"
#include "akeir/runtime/Components.h"
#include "akeir/serialization/Canonical.h"
#include "GameSystems.h"
#include "akeir/core/Hash.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

using namespace akeir;
namespace fs = std::filesystem;

namespace {

std::string sampleProjectDir() {
    // frozen fixture (Tests/Fixtures/TestArena, ADR-0036) — never the user's Game/, which may be any game
    return std::string(AKEIR_TEST_FIXTURES) + "/TestArena";
}

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss; ss << in.rdbuf();
    return ss.str();
}

struct TempProject {
    fs::path dir;
    std::optional<Project> prj;
    std::vector<Diagnostic> diags;
    TempProject() {
        registerBuiltinComponents();
        game::registerGameComponents();
        static int n = 0;
        dir = fs::temp_directory_path() / ("akeir_cmd_test_" + std::to_string(++n) + "_" + std::to_string(static_cast<unsigned>(std::hash<std::string>{}(sampleProjectDir()) & 0xffff)));
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir);
        for (const char* sub : {"Worlds", "Prefabs", "Config"}) fs::copy(fs::path(sampleProjectDir()) / sub, dir / sub, fs::copy_options::recursive, ec);
        fs::copy_file(fs::path(sampleProjectDir()) / "project.json", dir / "project.json", ec);
        prj = Project::load(dir.string(), diags);
        REQUIRE(prj);
    }
    ~TempProject() { std::error_code ec; fs::remove_all(dir, ec); }
    std::string id(const char* selector) {
        auto v = prj->resolveSelector(selector);
        REQUIRE(v.size() == 1);
        return v.front();
    }
    bool fileIsCanonical(const std::string& rel) const {
        std::string text = readFile(dir / rel);
        auto j = parseJson(text);
        REQUIRE(j);
        return isCanonicalText(text, *j);
    }
};

} // namespace

TEST_CASE("CommandBus: property.set on a plain entity commits, writes a canonical file, records history (§8, §9.2, §10)") {
    TempProject t;
    CommandBus bus(*t.prj, BusOptions{"test"});
    const std::string arena = t.id("path:TestArena/Arena");

    Envelope e = bus.execute("property.set", Json{{"entity", arena}, {"component", "Transform"}, {"path", "/position/0"}, {"value", 2.5}});
    CHECK(e.ok);
    CHECK(e.changes.size() == 1);
    CHECK(e.changes[0]["op"] == "replace");
    CHECK(e.changes[0]["before"] == 0);
    CHECK(e.changes[0]["value"] == 2.5);
    CHECK(e.meta["committed"] == true);
    std::string csId = e.meta["changeSet"].get<std::string>();
    CHECK(csId.rfind("cs_", 0) == 0);

    // 메모리 / 파일 모두 반영, 파일은 canonical
    auto comps = t.prj->resolveEntityComponents(arena);
    REQUIRE(comps);
    CHECK((*comps)["Transform"]["position"][0] == 2.5);
    CHECK(t.fileIsCanonical("Worlds/TestArena.world.json"));
    auto onDisk = readJsonFile((t.dir / "Worlds/TestArena.world.json").string());
    REQUIRE(onDisk);
    CHECK((*onDisk)["entities"][arena]["components"]["Transform"]["position"][0] == 2.5);

    // history + journal 정리
    CHECK(bus.history().entries().size() == 1);
    CHECK(bus.history().cursor() == 1);
    CHECK(fs::exists(t.dir / "Cache/history/history.jsonl"));
    CHECK_FALSE(fs::exists(t.dir / "Cache/journal" / (csId + ".json")));

    // 같은 값으로 다시 set → no-op (ops 없음, commit 안 함)
    Envelope e2 = bus.execute("property.set", Json{{"entity", arena}, {"component", "Transform"}, {"path", "/position/0"}, {"value", 2.5}});
    CHECK(e2.ok);
    CHECK(e2.changes.empty());
    CHECK(e2.meta["committed"] == false);
    CHECK(bus.history().entries().size() == 1);
}

TEST_CASE("CommandBus: undo restores the file byte-for-byte — redo re-applies (§10.1)") {
    TempProject t;
    const std::string before = readFile(t.dir / "Worlds/TestArena.world.json");
    CommandBus bus(*t.prj, BusOptions{"test"});
    const std::string player = t.id("path:TestArena/Arena/Player");

    REQUIRE(bus.execute("entity.rename", Json{{"entity", player}, {"name", "Hero"}}).ok);
    CHECK(readFile(t.dir / "Worlds/TestArena.world.json") != before);

    Envelope u = bus.undo();
    CHECK(u.ok);
    CHECK(u.result["undone"].size() == 1);
    CHECK(u.changes[0]["op"] == "replace");
    CHECK(u.changes[0]["value"] == "Player");
    CHECK(readFile(t.dir / "Worlds/TestArena.world.json") == before);
    CHECK(bus.history().cursor() == 0);
    CHECK(bus.history().entries().size() == 1);

    Envelope r = bus.redo();
    CHECK(r.ok);
    CHECK(t.prj->entityPath(player) == "TestArena/Arena/Hero");
    CHECK(bus.history().cursor() == 1);

    CHECK_FALSE(bus.redo().ok);
    CHECK(bus.redo().error.diagnostic.ruleId == "NOTHING_TO_REDO");

    // 새 commit 은 redo 꼬리를 버린다
    REQUIRE(bus.undo().ok);
    REQUIRE(bus.execute("tag.add", Json{{"entity", player}, {"tag", "hero"}}).ok);
    CHECK(bus.history().entries().size() == 1);
    CHECK(bus.history().entries()[0].intent["op"] == "tag.add");
}

TEST_CASE("CommandBus: history persists across bus instances — undo conflict when the file changed underneath (§10.2)") {
    TempProject t;
    const std::string arena = t.id("path:TestArena/Arena");
    {
        CommandBus bus(*t.prj, BusOptions{"ai:claude#1"});
        REQUIRE(bus.execute("property.set", Json{{"entity", arena}, {"component", "Transform"}, {"path", "/position/1"}, {"value", 7}}).ok);
    }
    // 다른 actor 가 같은 값을 또 바꿈 (새 bus = 새 프로세스처럼)
    std::vector<Diagnostic> d;
    auto prj2 = Project::load(t.dir.string(), d);
    REQUIRE(prj2);
    CommandBus bus2(*prj2, BusOptions{"human:editor"});
    REQUIRE(bus2.history().entries().size() == 1);
    REQUIRE(bus2.execute("property.set", Json{{"entity", arena}, {"component", "Transform"}, {"path", "/position/1"}, {"value", 9}}).ok);
    CHECK(bus2.history().entries().size() == 2);

    // actor 필터: 최근 항목은 human:editor 의 것 → ai 가 undo 하려면 거부
    Envelope u = bus2.undo(1, "ai:claude#1");
    CHECK_FALSE(u.ok);
    CHECK(u.error.diagnostic.ruleId == "UNDO_ACTOR_MISMATCH");

    // 두 번 undo → 0 으로
    REQUIRE(bus2.undo(2).ok);
    auto comps = prj2->resolveEntityComponents(arena);
    CHECK((*comps)["Transform"]["position"][1] == 0);

    // redo 2 → 9; 그 다음 외부에서 파일을 고치면 undo 가 conflict
    REQUIRE(bus2.redo(2).ok);
    {
        auto j = readJsonFile((t.dir / "Worlds/TestArena.world.json").string());
        (*j)["entities"][arena]["components"]["Transform"]["position"][1] = 123;
        writeCanonicalFile((t.dir / "Worlds/TestArena.world.json").string(), Project::canonicalizeDocument(*j));
    }
    Envelope c = bus2.undo();
    CHECK_FALSE(c.ok);
    CHECK(c.error.diagnostic.ruleId == "BASE_MISMATCH");
    CHECK(c.error.category == ErrorCategory::Conflict);
}

TEST_CASE("CommandBus: prefab instance edits become set/add/remove overrides (§34, §78.1)") {
    TempProject t;
    CommandBus bus(*t.prj, BusOptions{"test"});
    const std::string goblin = t.id("name:Goblin_01");

    // set override
    Envelope e = bus.execute("property.set", Json{{"entity", goblin}, {"component", "Health"}, {"path", "max"}, {"value", 45}});
    REQUIRE(e.ok);
    CHECK(e.result["override"] == "set");
    CHECK(e.changes[0]["path"] == "/entities/" + goblin + "/set/~1components~1Health~1max");
    CHECK((*t.prj->resolveEntityComponents(goblin))["Health"]["max"] == 45);

    // prefab 값(30)으로 되돌리면 override 가 사라진다
    e = bus.execute("property.set", Json{{"entity", goblin}, {"component", "Health"}, {"path", "max"}, {"value", 30}});
    REQUIRE(e.ok);
    CHECK(e.result["override"] == "inherited");
    // 문서 참조는 commit(patch) 뒤에 무효가 될 수 있으니 매번 다시 얻는다
    auto ent = [&]() -> const Json& { return t.prj->document("Worlds/TestArena.world.json")->at("entities").at(goblin); };
    bool stillOverridden = ent().contains("set") && ent()["set"].contains("/components/Health/max");
    CHECK_FALSE(stillOverridden);

    // component.add on instance → add 맵; 값 검증 (unknown prop → 실패)
    Envelope bad = bus.execute("component.add", Json{{"entity", goblin}, {"component", "Camera2D"}, {"value", Json{{"nope", 1}}}});
    CHECK_FALSE(bad.ok);
    CHECK(bad.error.diagnostic.ruleId == "PROPERTY_UNKNOWN");
    e = bus.execute("component.add", Json{{"entity", goblin}, {"component", "Camera2D"}, {"value", Json{{"orthoSize", 12}}}});
    REQUIRE(e.ok);
    CHECK(ent().contains("add"));
    CHECK(ent()["add"]["/components/Camera2D"]["orthoSize"] == 12);
    CHECK((*t.prj->resolveEntityComponents(goblin))["Camera2D"]["primary"] == true);   // default 채움 (Camera2D.primary 기본값 true)

    // property.set on the added component edits the add map in place
    e = bus.execute("property.set", Json{{"entity", goblin}, {"component", "Camera2D"}, {"path", "orthoSize"}, {"value", 6}});
    REQUIRE(e.ok);
    CHECK(e.result["override"] == "add");
    CHECK(ent()["add"]["/components/Camera2D"]["orthoSize"] == 6);

    // component.remove of an inherited component → remove 목록; of an added one → add 에서 제거
    e = bus.execute("component.remove", Json{{"entity", goblin}, {"component", "Camera2D"}});
    REQUIRE(e.ok);
    CHECK_FALSE(ent().contains("add"));
    e = bus.execute("component.remove", Json{{"entity", goblin}, {"component", "EnemyAI"}});
    REQUIRE(e.ok);
    CHECK(ent()["remove"] == Json::array({"/components/EnemyAI"}));
    CHECK_FALSE(t.prj->resolveEntityComponents(goblin)->contains("EnemyAI"));
    // 다시 add → remove 목록에서 빠진다
    e = bus.execute("component.add", Json{{"entity", goblin}, {"component", "EnemyAI"}});
    REQUIRE(e.ok);
    CHECK_FALSE(ent().contains("remove"));
    CHECK(t.prj->resolveEntityComponents(goblin)->contains("EnemyAI"));

    // runtimeOnly / readOnly / unknown component 거부
    CHECK(bus.execute("property.set", Json{{"entity", goblin}, {"component", "Health"}, {"path", "current"}, {"value", 1}}).error.diagnostic.ruleId == "PROPERTY_RUNTIME_ONLY");
    CHECK(bus.execute("property.set", Json{{"entity", goblin}, {"component", "Nope"}, {"path", "x"}, {"value", 1}}).error.diagnostic.ruleId == "COMPONENT_UNKNOWN");
    CHECK(bus.execute("property.set", Json{{"entity", goblin}, {"component", "Health"}, {"path", "max"}, {"value", "many"}}).error.diagnostic.ruleId == "PROPERTY_TYPE_MISMATCH");

    // 전부 undo 하면 파일이 원본과 같다 (7 commits)
    std::string before = readFile(fs::path(sampleProjectDir()) / "Worlds/TestArena.world.json");
    Envelope all = bus.undo(100);
    INFO(all.toJson().dump());
    REQUIRE(all.ok);
    CHECK(readFile(t.dir / "Worlds/TestArena.world.json") == before);
}

TEST_CASE("CommandBus: entity.create/delete/reparent, prefab.create/instantiate, world.create (§8)") {
    TempProject t;
    CommandBus bus(*t.prj, BusOptions{"test"});
    const std::string arena = t.id("path:TestArena/Arena");

    Envelope e = bus.execute("entity.create", Json{{"name", "Crate"}, {"parent", arena}, {"components", Json{{"Transform", Json{{"position", Json::array({1, 2, 0})}}}, {"Collider2D", Json::object()}}}});
    REQUIRE(e.ok);
    std::string crate = e.result["id"];
    CHECK(e.result["path"] == "TestArena/Arena/Crate");
    auto comps = t.prj->resolveEntityComponents(crate);
    CHECK((*comps)["Transform"]["position"] == Json::array({1, 2, 0}));
    CHECK((*comps)["Transform"]["scale"] == Json::array({1, 1, 1}));      // default 채움
    CHECK((*comps)["Collider2D"]["shape"] == "box");

    // Transform 없이 만들어도 Transform 이 붙는다
    e = bus.execute("entity.create", Json{{"name", "Child"}, {"parent", crate}});
    REQUIRE(e.ok);
    std::string child = e.result["id"];
    CHECK(t.prj->resolveEntityComponents(child)->contains("Transform"));

    // cycle 거부
    CHECK(bus.execute("entity.reparent", Json{{"entity", crate}, {"parent", child}}).error.diagnostic.ruleId == "HIERARCHY_CYCLE");
    REQUIRE(bus.execute("entity.reparent", Json{{"entity", child}, {"parent", nullptr}}).ok);
    CHECK(t.prj->entityPath(child) == "TestArena/Child");

    // recursive delete
    REQUIRE(bus.execute("entity.reparent", Json{{"entity", child}, {"parent", crate}}).ok);
    CHECK(bus.execute("entity.delete", Json{{"entity", crate}, {"recursive", false}}).error.diagnostic.ruleId == "ENTITY_HAS_CHILDREN");
    e = bus.execute("entity.delete", Json{{"entity", crate}});
    REQUIRE(e.ok);
    CHECK(e.result["deleted"].size() == 2);
    CHECK_FALSE(t.prj->locate(crate));
    CHECK_FALSE(t.prj->locate(child));
    // undo 가 부모→자식 순으로 되살린다
    REQUIRE(bus.undo().ok);
    CHECK(t.prj->entityPath(child) == "TestArena/Arena/Crate/Child");

    // prefab.create + instantiate + world.create
    e = bus.execute("prefab.create", Json{{"name", "Barrel"}, {"tags", Json::array({"prop"})}, {"components", Json{{"Collider2D", Json{{"shape", "circle"}, {"radius", 0.4}}}}}});
    REQUIRE(e.ok);
    std::string barrel = e.result["id"];
    CHECK(e.result["doc"] == "Prefabs/Barrel.prefab.json");
    CHECK(fs::exists(t.dir / "Prefabs/Barrel.prefab.json"));
    CHECK(t.fileIsCanonical("Prefabs/Barrel.prefab.json"));
    CHECK(bus.execute("prefab.create", Json{{"name", "Barrel"}}).error.diagnostic.ruleId == "DOCUMENT_EXISTS");

    e = bus.execute("prefab.instantiate", Json{{"prefab", "name:Barrel"}, {"parent", arena}, {"position", Json::array({3, 3, 0})}});
    REQUIRE(e.ok);
    std::string inst = e.result["id"];
    CHECK(e.result["path"] == "TestArena/Arena/Barrel");
    comps = t.prj->resolveEntityComponents(inst);
    CHECK((*comps)["Transform"]["position"] == Json::array({3, 3, 0}));
    CHECK((*comps)["Collider2D"]["radius"] == 0.4);

    e = bus.execute("world.create", Json{{"name", "Lobby"}});
    REQUIRE(e.ok);
    CHECK(fs::exists(t.dir / "Worlds/Lobby.world.json"));
    std::string lobby = e.result["id"];
    REQUIRE(bus.execute("entity.create", Json{{"world", lobby}, {"name", "Spawn"}}).ok);
    CHECK(t.prj->document("Worlds/Lobby.world.json")->at("entities").size() == 1);

    // prefab 삭제는 없지만, prefab.create 를 undo 하면 파일이 사라진다 (문서 단위 remove)
    REQUIRE(bus.undo(4).ok);   // entity.create(Spawn), world.create, instantiate, prefab.create
    CHECK_FALSE(fs::exists(t.dir / "Worlds/Lobby.world.json"));
    CHECK_FALSE(fs::exists(t.dir / "Prefabs/Barrel.prefab.json"));
    CHECK_FALSE(t.prj->locate(barrel));
}

TEST_CASE("CommandBus: dry-run leaves nothing behind — validation rejects dangling refs (§50, §29)") {
    TempProject t;
    CommandBus bus(*t.prj, BusOptions{"test"});
    const std::string before = readFile(t.dir / "Worlds/TestArena.world.json");
    const std::string arena = t.id("path:TestArena/Arena");

    ExecOptions dry; dry.dryRun = true;
    Envelope e = bus.execute("entity.create", Json{{"name", "Ghost"}, {"parent", arena}}, dry);
    REQUIRE(e.ok);
    CHECK(e.meta["dryRun"] == true);
    CHECK(e.meta["committed"] == false);
    CHECK(e.changes.size() == 1);
    CHECK_FALSE(t.prj->locate(e.result["id"].get<std::string>()));
    CHECK(readFile(t.dir / "Worlds/TestArena.world.json") == before);
    CHECK(bus.history().entries().empty());

    // 새 오류를 만드는 변경은 거부: RigidBody2D 는 Collider2D 를 요구한다 — 문서 검증(COMPONENT_DEPENDENCY_MISSING)이 commit 을 막는다
    Envelope bad = bus.execute("component.add", Json{{"entity", arena}, {"component", "RigidBody2D"}});
    CHECK_FALSE(bad.ok);
    CHECK(bad.error.diagnostic.ruleId == "VALIDATION_FAILED");
    CHECK(bad.error.details["diagnostics"][0]["ruleId"] == "COMPONENT_DEPENDENCY_MISSING");
    CHECK_FALSE(t.prj->resolveEntityComponents(arena)->contains("RigidBody2D"));
    // --no-validate (validateAfter=false) 면 통과하지만 validate 가 오류를 낸다
    ExecOptions force; force.validateAfter = false;
    CHECK(bus.execute("component.add", Json{{"entity", arena}, {"component", "RigidBody2D"}}, force).ok);
    bool found = false;
    for (const auto& d : t.prj->validate()) if (d.ruleId == "COMPONENT_DEPENDENCY_MISSING") found = true;
    CHECK(found);
    REQUIRE(bus.undo().ok);
    CHECK(readFile(t.dir / "Worlds/TestArena.world.json") == before);
}

TEST_CASE("CommandBus: in-process tx composes one history entry — rollback discards (§9)") {
    TempProject t;
    CommandBus bus(*t.prj, BusOptions{"test"});
    const std::string arena = t.id("path:TestArena/Arena");

    std::string tx = bus.beginTx();
    ExecOptions in; in.tx = tx;
    Envelope a = bus.execute("entity.create", Json{{"name", "A"}, {"parent", arena}}, in);
    REQUIRE(a.ok);
    CHECK(a.meta["committed"] == false);
    std::string aId = a.result["id"];
    // tx 안에서는 자기 변경이 보인다 (fork), 밖에서는 아직 안 보인다
    REQUIRE(bus.execute("entity.rename", Json{{"entity", aId}, {"name", "A2"}}, in).ok);
    CHECK_FALSE(t.prj->locate(aId));

    Envelope c = bus.commitTx(tx);
    REQUIRE(c.ok);
    CHECK(t.prj->entityPath(aId) == "TestArena/Arena/A2");
    CHECK(bus.history().entries().size() == 1);
    CHECK(bus.history().entries()[0].tx == tx);
    CHECK(bus.history().entries()[0].intent.is_array());
    CHECK(bus.history().entries()[0].ops.size() == 2);
    CHECK_FALSE(bus.hasTx(tx));

    std::string tx2 = bus.beginTx();
    in.tx = tx2;
    REQUIRE(bus.execute("entity.delete", Json{{"entity", aId}}, in).ok);
    REQUIRE(bus.rollbackTx(tx2).ok);
    CHECK(t.prj->locate(aId));
    CHECK(bus.history().entries().size() == 1);

    // 하나의 undo 로 tx 전체가 돌아간다
    REQUIRE(bus.undo().ok);
    CHECK_FALSE(t.prj->locate(aId));
}

TEST_CASE("CommandBus: apply batch is atomic, resolves $refs, honours idempotencyKey (§49)") {
    TempProject t;
    CommandBus bus(*t.prj, BusOptions{"test"});
    const std::string arena = t.id("path:TestArena/Arena");

    Json batch = Json::parse(R"({
      "idempotencyKey": "k-1",
      "changes": [
        {"op": "prefab.create", "as": "barrel", "name": "Barrel", "components": {"Collider2D": {"shape": "circle", "radius": 0.4}}},
        {"op": "prefab.instantiate", "as": "b1", "prefab": "$barrel", "name": "Barrel_01", "position": [1, 0, 0]},
        {"op": "prefab.instantiate", "as": "b2", "prefab": "$barrel", "name": "Barrel_02", "position": [2, 0, 0]},
        {"op": "tag.add", "entity": "$b2", "tag": "breakable"},
        {"op": "entity.reparent", "entity": "$b1", "parent": "$b2.id"}
      ]
    })");
    batch["changes"][1]["parent"] = arena;
    batch["changes"][2]["parent"] = arena;
    Envelope e = bus.apply(batch);
    REQUIRE(e.ok);
    CHECK(e.result["count"] == 5);
    CHECK(e.result["results"][0]["as"] == "barrel");
    CHECK(bus.history().entries().size() == 1);            // 한 ChangeSet
    std::string b1 = e.result["results"][1]["result"]["id"];
    std::string b2 = e.result["results"][2]["result"]["id"];
    CHECK(t.prj->entityPath(b1) == "TestArena/Arena/Barrel_02/Barrel_01");
    CHECK(fs::exists(t.dir / "Prefabs/Barrel.prefab.json"));

    // 같은 key → 재실행 없이 같은 응답
    Envelope again = bus.apply(batch);
    CHECK(again.ok);
    CHECK(again.meta["idempotentReplay"] == true);
    CHECK(bus.history().entries().size() == 1);

    // 중간에 실패하면 전부 롤백
    Json bad = Json::parse(R"({"changes": [
        {"op": "entity.create", "as": "x", "name": "X"},
        {"op": "property.set", "entity": "$x", "component": "Health", "path": "max", "value": 1}
    ]})");
    Envelope f = bus.apply(bad);
    CHECK_FALSE(f.ok);
    CHECK(f.error.diagnostic.ruleId == "COMPONENT_NOT_ON_ENTITY");
    CHECK(f.error.details["index"] == 1);
    CHECK(bus.history().entries().size() == 1);
    CHECK(t.prj->resolveSelector("name:X").empty());

    // 알 수 없는 참조
    Envelope g = bus.apply(Json::parse(R"({"changes": [{"op": "tag.add", "entity": "$nope", "tag": "t"}]})"));
    CHECK(g.error.diagnostic.ruleId == "APPLY_BAD_REFERENCE");

    // dry-run apply: 아무것도 안 남는다
    Json dryBatch = Json::parse(R"({"dryRun": true, "changes": [{"op": "world.create", "name": "Dry"}]})");
    Envelope d = bus.apply(dryBatch);
    CHECK(d.ok);
    CHECK(d.changes.size() == 1);
    CHECK_FALSE(fs::exists(t.dir / "Worlds/Dry.world.json"));
}

TEST_CASE("CommandBus: journal recovery completes a commit whose files were never written (§9.2)") {
    TempProject t;
    const std::string arena = t.id("path:TestArena/Arena");
    // 가짜 journal: dry-run 으로 ChangeSet 을 얻고 base 를 채워 journal 에만 쓴다
    ChangeSet cs;
    {
        CommandBus bus(*t.prj, BusOptions{"test"});
        ExecOptions dry; dry.dryRun = true;
        Envelope e = bus.execute("entity.rename", Json{{"entity", arena}, {"name", "Recovered"}}, dry);
        REQUIRE(e.ok);
        cs.id = "cs_journaltest";
        cs.actor = "test";
        cs.createdAt = "2026-08-21T00:00:00Z";
        cs.intent = Json{{"op", "entity.rename"}};
        for (const auto& o : e.changes) cs.ops.push_back(*ChangeOp::fromJson(o));
        cs.finalize();
        cs.base["Worlds/TestArena.world.json"] = Sha256::hexOf(jcsDump(Project::canonicalizeDocument(*t.prj->document("Worlds/TestArena.world.json"))), true);
        REQUIRE(bus.history().writeJournal(cs));
    }
    std::vector<Diagnostic> d;
    auto prj2 = Project::load(t.dir.string(), d);
    CommandBus bus2(*prj2, BusOptions{"test"});
    Json report = bus2.recoverJournal();
    REQUIRE(report.size() == 1);
    CHECK(report[0]["action"] == "completed");
    CHECK(prj2->entityPath(arena) == "TestArena/Recovered");
    CHECK(bus2.history().find("cs_journaltest") != nullptr);
    CHECK_FALSE(fs::exists(t.dir / "Cache/journal/cs_journaltest.json"));
    auto onDisk = readJsonFile((t.dir / "Worlds/TestArena.world.json").string());
    CHECK((*onDisk)["entities"][arena]["name"] == "Recovered");
}

TEST_CASE("CommandBus: editing a prefab changes every instance — derived prefabs get overrides (§34, §78.1)") {
    TempProject t;
    CommandBus bus(*t.prj, BusOptions{"test"});
    const std::string goblinPrefab = t.id("name:Goblin");
    const std::string elite = t.id("name:GoblinElite");

    Envelope e = bus.execute("property.set", Json{{"entity", "name:Goblin"}, {"component", "Movement"}, {"path", "speed"}, {"value", 4.5}});
    REQUIRE(e.ok);
    CHECK(e.result["prefab"] == true);
    CHECK(e.result["override"] == false);
    CHECK(e.changes[0]["doc"] == "Prefabs/Goblin.prefab.json");
    CHECK(e.changes[0]["path"] == "/components/Movement/speed");
    for (const char* sel : {"name:Goblin_01", "name:Goblin_02"})
        CHECK((*t.prj->resolveEntityComponents(t.id(sel)))["Movement"]["speed"] == 4.5);
    // GoblinElite 는 자기 set 으로 4 를 갖고 있으므로 그대로
    CHECK((*t.prj->resolvePrefab(elite))["Movement"]["speed"] == 4);

    // derived prefab 에 set → set 맵; base 값과 같게 하면 override 삭제
    e = bus.execute("property.set", Json{{"entity", elite}, {"component", "Health"}, {"path", "max"}, {"value", 30}});
    REQUIRE(e.ok);
    CHECK(e.result["override"] == "inherited");
    CHECK_FALSE(t.prj->document("Prefabs/GoblinElite.prefab.json")->at("set").contains("/components/Health/max"));
    e = bus.execute("component.add", Json{{"entity", elite}, {"component", "Camera2D"}});
    REQUIRE(e.ok);
    CHECK(t.prj->document("Prefabs/GoblinElite.prefab.json")->at("add").contains("/components/Camera2D"));

    // prefab 의 component 를 지우면 의존하는 component 가 있을 때 거부
    Envelope bad = bus.execute("component.remove", Json{{"entity", goblinPrefab}, {"component", "Movement"}});
    CHECK(bad.error.diagnostic.ruleId == "COMPONENT_DEPENDENCY");

    REQUIRE(bus.undo(3).ok);
    CHECK((*t.prj->resolvePrefab(goblinPrefab))["Movement"]["speed"] == 3);
}

TEST_CASE("Project: COMPONENT_DEPENDENCY_MISSING fix adds the transitive chain and validate --fix style apply closes it (§29)") {
    TempProject t;
    CommandBus bus(*t.prj, BusOptions{"test"});
    const std::string arena = t.id("path:TestArena/Arena");
    ExecOptions force; force.validateAfter = false;
    REQUIRE(bus.execute("component.add", Json{{"entity", arena}, {"component", "EnemyAI"}}, force).ok);
    const Diagnostic* dep = nullptr;
    auto diags = t.prj->validate();
    for (const auto& d : diags) if (d.ruleId == "COMPONENT_DEPENDENCY_MISSING" && d.message.text.find("Movement") != std::string::npos) dep = &d;
    REQUIRE(dep);
    REQUIRE(dep->fixes.size() == 1);
    REQUIRE(dep->fixes[0].commands.size() == 3);          // Movement → RigidBody2D → Collider2D 순으로 요구 → add 는 역순(의존 대상 먼저)
    CHECK(dep->fixes[0].commands[0].args["component"] == "Collider2D");
    CHECK(dep->fixes[0].commands[1].args["component"] == "RigidBody2D");
    CHECK(dep->fixes[0].commands[2].args["component"] == "Movement");
    Json batch = Json{{"changes", Json::array()}};
    for (const auto& c : dep->fixes[0].commands) { Json ch = c.args; ch["op"] = c.op; batch["changes"].push_back(ch); }
    Envelope fixed = bus.apply(batch, force);
    REQUIRE(fixed.ok);
    bool still = false;
    for (const auto& d : t.prj->validate()) if (d.ruleId == "COMPONENT_DEPENDENCY_MISSING") still = true;
    CHECK_FALSE(still);
}

TEST_CASE("CommandBus: tx handles carry a TTL and expire (§9.1)") {
    TempProject t;
    CommandBus bus(*t.prj, BusOptions{"test"});
    std::string tx = bus.beginTx(1);   // 1 ms
    Json info = bus.txInfo(tx);
    CHECK(info["tx"] == tx);
    CHECK(info["ttlMs"] == 1);
    CHECK(bus.txList().size() == 1);
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ExecOptions in; in.tx = tx;
    Envelope e = bus.execute("tag.add", Json{{"entity", "path:TestArena/Arena"}, {"tag", "x"}}, in);
    CHECK_FALSE(e.ok);
    CHECK(e.error.diagnostic.ruleId == "TX_UNKNOWN_OR_EXPIRED");
    CHECK(bus.txList().empty());
    CHECK(bus.commitTx(tx).error.diagnostic.ruleId == "TX_UNKNOWN_OR_EXPIRED");
    // 기본 TTL 은 충분히 길다
    std::string tx2 = bus.beginTx();
    CHECK(bus.txInfo(tx2)["ttlMs"] == 600000);
    CHECK(bus.rollbackTx(tx2).ok);
}
