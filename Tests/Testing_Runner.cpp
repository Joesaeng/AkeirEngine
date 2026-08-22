// Testing_Runner.cpp — 설계 문서 §23 (setup/inputs/assert 의미론), §22.2 (run-twice), §24 (results/JUnit). 샘플 Game/ + Game/Source systems 사용.
#include <doctest/doctest.h>
#include "GameSystems.h"
#include "akeir/runtime/Components.h"
#include "akeir/testing/TestRunner.h"

#include <filesystem>

using namespace akeir;
namespace fs = std::filesystem;

namespace {
std::string sampleDir() {
    // frozen fixture (Tests/Fixtures/TestArena, ADR-0036) — never the user's Game/, which may be any game
    return std::string(AKEIR_TEST_FIXTURES) + "/TestArena";
}
WorldFactory factory() {
    return [](const Project& prj, const std::string& worldId, const PlayWorldConfig& cfg, std::vector<Diagnostic>& d) {
        auto w = PlayWorld::build(prj, worldId, cfg, d);
        if (w) game::registerGameSystems(*w);
        return w;
    };
}
struct Fixture {
    std::optional<Project> prj;
    std::vector<Diagnostic> diags;
    fs::path results;
    Fixture() {
        registerBuiltinComponents();
        game::registerGameComponents();
        prj = Project::load(sampleDir(), diags);
        REQUIRE(prj);
        results = fs::temp_directory_path() / "akeir_test_results";
        std::error_code ec;
        fs::remove_all(results, ec);
    }
    ~Fixture() { std::error_code ec; fs::remove_all(results, ec); }
    TestRunner runner() { TestRunnerOptions o; o.resultsDir = results.string(); return TestRunner(*prj, factory(), o); }
};
} // namespace

TEST_CASE("TestRunner: scenario with setup/inputs/assert passes — results.json + JUnit written (§23, §24)") {
    Fixture f;
    Json j = Json::parse(R"json({
      "name": "PlayerMovesGoblinsChase",
      "seed": 1024,
      "setup": [ { "entity": "path:TestArena/Arena/Player", "as": "player" },
                 { "spawn": "name:Goblin", "as": "g", "position": [-6, 0, 0], "set": {"/components/Health/max": 5} } ],
      "inputs": [ { "tick": 0, "hold": {"MoveX": 1.0}, "untilTick": 60 } ],
      "run": { "ticks": 240 },
      "determinism": { "runs": 2, "hashEvery": 30 },
      "assert": [
        { "id": "player-alive",   "expr": "player.Health.current > 0", "always": true },
        { "id": "moved-right",    "expr": "player.Transform.position[0] > 2", "at": 60 },
        { "id": "spawned-health", "expr": "g.Health.max == 5 && g.Health.current == 5", "at": 1 },
        { "id": "goblin-chases",  "expr": "g.EnemyAI.state in [\"chase\", \"attack\"]", "eventually": { "withinTicks": 240 } },
        { "id": "no-orphans",     "expr": "world.entities.all(e, !has(e.components.EnemyAI) || has(e.components.Collider2D))", "at": "end" },
        { "id": "entity-count",   "expr": "size(world.entities) == 8", "at": "end" }
      ]
    })json");
    TestScenario sc = TestScenario::fromJson(j, "Tests/Combat/PlayerMoves.test.json");
    std::string firstProblem = sc.problems.empty() ? std::string() : sc.problems.front();
    REQUIRE_MESSAGE(sc.problems.empty(), firstProblem);
    CHECK(sc.asserts.size() == 6);
    CHECK(sc.asserts[0].when == TestAssert::When::Always);
    CHECK(sc.asserts[3].when == TestAssert::When::Eventually);

    TestRunner r = f.runner();
    TestReport rep = r.runAll({sc});
    const TestResult& t = rep.tests.at(0);
    INFO(t.toJson().dump(1));
    CHECK(t.status == "passed");
    CHECK(t.failures.empty());
    CHECK(t.ticksRun == 240);
    CHECK(t.seed == 1024);
    CHECK(t.determinism["passed"] == true);
    CHECK(t.determinism["runs"] == 2);
    CHECK(rep.summary()["passed"] == 1);
    CHECK(rep.allPassed());
    CHECK(fs::exists(f.results / "results.json"));
    std::string xml = rep.junitXml();
    CHECK(xml.find("<testsuite name=\"Tests.Combat\"") != std::string::npos);
    CHECK(xml.find("name=\"PlayerMovesGoblinsChase\"") != std::string::npos);
}

TEST_CASE("TestRunner: failures carry tick, bindings and a snapshot artifact — always aborts the run (§23.1, §24)") {
    Fixture f;
    Json j = Json::parse(R"json({
      "name": "FailingExpectations",
      "setup": [ { "entity": "path:TestArena/Arena/Player", "as": "player" } ],
      "run": { "ticks": 300 },
      "assert": [
        { "id": "never-hurt",  "expr": "player.Health.current == player.Health.max", "always": true },
        { "id": "typo",        "expr": "player.Helth.current > 0", "at": "end" },
        { "id": "too-fast",    "expr": "player.Transform.position[0] > 100", "eventually": { "withinTicks": 10 } },
        { "id": "late-tick",   "expr": "true", "at": 299 }
      ]
    })json");
    TestScenario sc = TestScenario::fromJson(j, "Tests/Fail.test.json");
    REQUIRE(sc.problems.empty());
    TestRunner r = f.runner();
    TestResult t = r.run(sc);
    INFO(t.toJson().dump(1));
    CHECK(t.status == "failed");
    REQUIRE(t.abortedAt);
    CHECK(*t.abortedAt < 300);           // 고블린이 때리는 순간 always 위반 → 중단
    CHECK(t.ticksRun == *t.abortedAt);
    std::map<std::string, TestFailure> by;
    for (const auto& x : t.failures) by[x.assertId] = x;
    REQUIRE(by.count("never-hurt"));
    CHECK(by["never-hurt"].bindings["player.Health.current"].get<double>() < 100);
    CHECK(by["never-hurt"].bindings["player.Health.max"] == 100);
    REQUIRE(by.count("typo"));
    CHECK(by["typo"].note.find("undefined") != std::string::npos);
    CHECK(by["typo"].bindings["player.Helth.current"] == "<undefined>");
    REQUIRE(by.count("too-fast"));
    CHECK(by["too-fast"].tick == 10);
    CHECK(by["too-fast"].note.find("within 10") != std::string::npos);
    REQUIRE(by.count("late-tick"));
    CHECK(by["late-tick"].note.find("never reached") != std::string::npos);
    bool snapshotSaved = false;
    for (const auto& a : t.artifacts) if (a["kind"] == "snapshot") snapshotSaved = true;
    CHECK(snapshotSaved);
    Json failJson = t.failures.front().toJson();
    CHECK(failJson["diagnostic"]["ruleId"] == "TEST_ASSERTION_FAILED");
}

TEST_CASE("TestRunner: expectedFinalHash mismatch and parse problems are reported, diffSnapshots finds the property (§22.2)") {
    Fixture f;
    Json j = Json::parse(R"json({ "name": "HashPinned", "run": { "ticks": 30 }, "determinism": { "runs": 1, "hashEvery": 10, "expectedFinalHash": "0x0000000000000001" },
                              "assert": [ { "id": "ok", "expr": "world.tick == 30" } ] })json");
    TestScenario sc = TestScenario::fromJson(j, "Tests/Hash.test.json");
    TestRunner r = f.runner();
    TestResult t = r.run(sc);
    CHECK(t.status == "failed");
    CHECK(t.failures.empty());
    CHECK(t.determinism["passed"] == false);
    CHECK(t.determinism["reason"].get<std::string>().find("expectedFinalHash") != std::string::npos);
    // 실제 hash 를 고정하면 통과
    j["determinism"]["expectedFinalHash"] = t.determinism["finalHash"];
    TestResult t2 = r.run(TestScenario::fromJson(j, "Tests/Hash.test.json"));
    CHECK(t2.status == "passed");

    TestScenario bad = TestScenario::fromJson(Json::parse(R"json({"name":"Bad","assert":[{"id":"x","expr":"player.Health.current >"}]})json"), "Tests/Bad.test.json");
    CHECK_FALSE(bad.problems.empty());
    TestResult tb = r.run(bad);
    CHECK(tb.status == "errored");
    CHECK(tb.error.find("unexpected end") != std::string::npos);

    Json a = Json::parse(R"json({"entities":[{"id":"e1","components":{"Transform":{"position":[1,2,0]}}},{"id":"e2","components":{}}],"rng":{"x":["0x1"]}})json");
    Json b = Json::parse(R"json({"entities":[{"id":"e1","components":{"Transform":{"position":[1,2.5,0]}}},{"id":"e3","components":{}}],"rng":{"x":["0x1"]}})json");
    Json d = TestRunner::diffSnapshots(a, b);
    REQUIRE(d.size() == 3);
    CHECK(d[0]["entity"] == "e1");
    CHECK(d[0]["path"] == "/Transform/position/1");
    CHECK(d[0]["a"] == 2);
    CHECK(d[0]["b"] == 2.5);
}

TEST_CASE("TestRunner: discover finds <fixture>/Tests/**/*.test.json and the frozen scenarios pass") {
    Fixture f;
    TestRunner r = f.runner();
    auto all = r.discover("Tests");
    REQUIRE_FALSE(all.empty());
    TestReport rep = r.runAll(all);
    for (const auto& t : rep.tests) { INFO(t.toJson().dump(1)); CHECK((t.status == "passed" || t.status == "skipped")); }   // Visual/* 는 renderer 가 없으면 skipped
}

TEST_CASE("TestRunner: pointer steps persist until the next one, carry the scenario viewport and derived edges (ADR-0045)") {
    Fixture f;
    Json j = Json::parse(R"json({
      "name": "PointerClick",
      "viewport": [640, 360],
      "inputs": [ { "tick": 2, "pointer": {"x": 100, "y": 50} },
                  { "tick": 4, "pointer": {"x": 110, "y": 55, "buttons": ["left"], "wheel": 1} },
                  { "tick": 6, "pointer": {"x": 110, "y": 55} },
                  { "tick": 6, "press": "Attack" } ],
      "run": { "ticks": 10 },
      "assert": [ { "id": "ok", "expr": "true" } ]
    })json");
    TestScenario sc = TestScenario::fromJson(j, "Tests/Input/PointerClick.test.json");
    REQUIRE(sc.problems.empty());
    std::map<std::int64_t, InputFrame> seen;
    WorldFactory probe = [&](const Project& prj, const std::string& worldId, const PlayWorldConfig& cfg, std::vector<Diagnostic>& d) {
        auto w = PlayWorld::build(prj, worldId, cfg, d);
        if (w) w->addSystem("Probe.Input", [&](PlayWorld&, const InputFrame& in, const SimTime&) { seen[in.tick] = in; });
        return w;
    };
    TestRunnerOptions o; o.resultsDir = f.results.string();
    TestRunner r(*f.prj, probe, o);
    TestReport rep = r.runAll({sc});
    REQUIRE(rep.tests.at(0).status == "passed");
    REQUIRE(seen.size() == 10);
    CHECK_FALSE(seen[1].pointer.present);
    CHECK(seen[2].pointer.present);
    CHECK(seen[2].pointer.x == 100);
    CHECK(seen[2].pointer.viewportW == 640);
    CHECK(seen[3].pointer.x == 100);                 // persists
    CHECK(seen[4].pointer.justPressed(kPointerLeft));
    CHECK(seen[4].pointer.wheel == 1.f);
    CHECK(seen[5].pointer.held(kPointerLeft));
    CHECK_FALSE(seen[5].pointer.justPressed(kPointerLeft));
    CHECK(seen[5].pointer.wheel == 0.f);
    CHECK(seen[6].pointer.justReleased(kPointerLeft));
    CHECK(seen[6].justPressed("Attack"));
    CHECK(seen[7].justReleased("Attack"));
    CHECK(seen[9].pointer.present);
}
