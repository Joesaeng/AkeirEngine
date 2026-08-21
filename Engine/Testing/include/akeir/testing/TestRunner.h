// akeir/testing/TestRunner.h — 데이터화된 테스트 시나리오 러너. 설계 문서 §23 (Test Scenario: setup/inputs/run/determinism/assert), §23.1 (assertion 의미론),
// §24 (results.json + JUnit), §22.2 (determinism verification: run-twice), §26.1 (snapshot 위에서 평가).
//
//   Game/Tests/**/*.test.json:
//   { "$schema": "game://schema/test/1", "name": "GoblinBasicCombat", "world": "<id|name:X>", "seed": 1024,
//     "setup":  [ { "spawn": "<prefab selector>", "as": "goblin", "position": [5,0,0], "set": {"/components/Health/max": 5}, "name": "G" },
//                 { "entity": "name:Player", "as": "player" } ],
//     "inputs": [ { "tick": 0, "hold": {"MoveX": 1.0}, "untilTick": 120 }, { "tick": 130, "press": "Attack" }, { "tick": 200, "axis": {"MoveY": -1}, "untilTick": 260 }, { "tick": 300, "release": "MoveX" } ],
//     "run":    { "ticks": 3600, "tickRate": 60 },
//     "determinism": { "runs": 2, "hashEvery": 60, "expectedFinalHash": null },
//     "assert": [ { "id": "player-alive", "expr": "player.Health.current > 0", "always": true },
//                 { "id": "goblin-dies",  "expr": "goblin.EnemyAI.state == \"Dead\"", "eventually": { "withinTicks": 3600 } },
//                 { "id": "at-120",      "expr": "player.Transform.position[0] > 3", "at": 120 },
//                 { "id": "end",         "expr": "world.entities.all(e, !has(e.components.EnemyAI) || has(e.components.Collider2D))", "at": "end" } ] }
//
//   의미론 (§23.1): 표현식은 그 tick 의 frame snapshot 위에서 평가된다. `as` 이름 → 그 entity 의 components 객체, `world` → snapshot 전체.
//   `always` 는 매 tick 검사, 첫 위반에서 run 을 중단(abortedAt). `eventually` 는 창 안에서 한 번 참이면 통과. `at` 은 해당 tick(또는 end) 한 번.
//   capture assertion: { "id": "golden-end", "capture": { "width": 512, "height": 512, "golden": "combat_end", "tolerance": {...} }, "at": "end" } — CaptureHook 으로 그려 Tests/Golden/<test>/<golden>_<WxH>.png 과 비교 (§27.1).
//   "requires": ["renderer"] 를 선언한 테스트는 CaptureHook 이 없는 빌드(msvc-headless)에서 skipped 로 보고된다.
//   determinism.runs ≥ 2 면 같은 시나리오를 다시 돌려 hashEvery 마다 world hash 를 비교하고, 어긋나면 첫 divergent tick 의 snapshot 을 entity/path 단위로 diff 한다.
//   러너는 Game/ 을 모른다 — world 를 만드는(system 등록 포함) WorldFactory 를 호출자가 준다.
#pragma once

#include "akeir/core/Diagnostic.h"
#include "akeir/core/Json.h"
#include "akeir/ecs/PlayWorld.h"
#include "akeir/runtime/Project.h"
#include "akeir/testing/Expr.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace akeir {

struct TestSetupStep {
    std::string spawn;        // prefab selector (spawn) — 또는
    std::string entity;       // 기존 entity selector (binding 만)
    std::string as;           // binding 이름
    std::string name;         // spawn 된 entity 이름 (기본 = as 또는 prefab 이름)
    std::optional<Json> position;
    Json set = Json::object();   // {"/components/X/y": value}
    std::vector<std::string> tags;
};

struct TestInputStep {
    std::int64_t tick = 0;
    std::optional<std::int64_t> untilTick;   // hold/axis 의 끝 (exclusive)
    Json hold = Json::object();              // {action: value}
    Json axis = Json::object();              // hold 와 같다 (의미상 축)
    std::string press;                       // 한 tick 1.0
    std::string release;                     // 이후 0 (hold 종료)
};

struct TestAssert {
    enum class When { AtEnd, AtTick, Always, Eventually };
    std::string id;
    std::string exprText;             // 비어 있으면 capture assertion
    Json capture;                     // {width, height, golden?, tolerance?{perPixel, maxMismatchRatio}} (§27.1). 평가 시점은 when 과 같다 (at/end 만 허용)
    When when = When::AtEnd;
    std::int64_t tick = 0;            // AtTick
    std::int64_t withinTicks = 0;     // Eventually (0 = run.ticks)
    std::optional<expr::Expr> expr;   // parse 결과
    std::string parseError;
};

struct TestScenario {
    std::string name;
    std::string file;                 // 프로젝트 상대 경로
    std::string world;                // selector ("" = defaultWorld)
    std::uint64_t seed = 0;
    bool hasSeed = false;
    std::vector<TestSetupStep> setup;
    std::vector<TestInputStep> inputs;
    std::int64_t ticks = 600;
    int tickRate = 0;                 // 0 = project.tickRate
    int determinismRuns = 1;
    std::int64_t hashEvery = 60;
    std::optional<std::string> expectedFinalHash;
    std::vector<TestAssert> asserts;
    std::vector<std::string> requirements;   // "requires": ["renderer"] — 환경이 제공하지 못하면 errored 대신 skipped (§20: dummy 에서 capture 는 불가)
    std::vector<std::string> problems;   // 파싱 문제 (있으면 errored)

    static TestScenario fromJson(const Json& j, const std::string& file);
};

struct TestFailure {
    std::string assertId, expr;
    std::int64_t tick = -1;
    Json expected = true, actual;
    std::string note;
    Json bindings = Json::object();
    Json toJson() const;
};

struct TestResult {
    std::string name, file;
    std::string status = "passed";    // passed | failed | errored | skipped
    double durationMs = 0;
    std::int64_t ticksRun = 0;
    std::uint64_t seed = 0;
    std::optional<std::int64_t> abortedAt;
    std::string abortReason;
    std::vector<TestFailure> failures;
    Json determinism;                 // {passed, runs, finalHash, firstDivergentTick?, runA, runB, diff[]}
    Json artifacts = Json::array();   // [{kind, path, tick?}]
    std::string error;                // errored 일 때
    Json toJson() const;
};

struct TestReport {
    std::string id, startedAt;
    double durationMs = 0;
    Json run = Json::object();        // engineVersion, fpFlagsHash, platform, threads …
    std::vector<TestResult> tests;
    Json summary() const;
    Json toJson() const;
    std::string junitXml() const;     // §24: testsuite = 디렉터리, [[ATTACHMENT|path]]
    bool allPassed() const;
};

using WorldFactory = std::function<std::unique_ptr<PlayWorld>(const Project&, const std::string& worldId, const PlayWorldConfig&, std::vector<Diagnostic>&)>;
/// capture assertion 용 (렌더 레이어는 호출자가 준다 — Testing 은 SDL 을 모른다). world 를 width×height 로 그려 outPng 에 저장.
using CaptureHook = std::function<bool(const PlayWorld& world, int width, int height, const std::string& outPng, std::string* error)>;
/// golden 비교: {ok, mismatchedPixels, ratio, error?} 를 돌려준다 (akeir::compareCaptures 의 toJson 과 같은 모양)
using CompareHook = std::function<Json(const std::string& expectedPng, const std::string& actualPng, const Json& tolerance, const std::string& diffPngOut)>;

struct TestRunnerOptions {
    std::string resultsDir;           // 비어 있으면 artifact 를 쓰지 않는다
    bool saveArtifacts = true;
    bool snapshotOnFailure = true;
    Json runInfo = Json::object();    // engineVersion 등 (보고서 run 블록에 복사)
    CaptureHook capture;              // 없으면 capture assertion 은 errored (TEST_CAPTURE_REQUIRES_RENDERER)
    CompareHook compare;
    bool updateGolden = false;        // golden 이 없거나 다르면 actual 로 덮어쓴다 (--update-golden)
    std::string goldenDir = "Tests/Golden";   // <project>/Tests/Golden/<test>/<golden>_<WxH>.png
};

class TestRunner {
public:
    TestRunner(const Project& project, WorldFactory factory, TestRunnerOptions options = {});

    /// <project>/<dir>/**/*.test.json 을 이름순으로 읽는다. filter 는 name/file 부분 문자열.
    std::vector<TestScenario> discover(const std::string& dir = "Tests", const std::string& filter = "") const;
    TestResult run(const TestScenario& scenario);
    TestReport runAll(const std::vector<TestScenario>& scenarios);

    /// snapshot 두 개의 entity/component/property 단위 차이 (§24 determinism.diff). 최대 limit 개.
    static Json diffSnapshots(const Json& a, const Json& b, std::size_t limit = 32);

private:
    const Project& project_;
    WorldFactory factory_;
    TestRunnerOptions options_;
};

} // namespace akeir
