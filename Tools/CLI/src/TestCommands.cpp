// Tools/CLI/TestCommands.cpp — `akeir test`: Game/Tests/**/*.test.json 을 실행해 results.json(+JUnit) 을 쓴다. 설계 문서 §23, §23.1, §24, §22.2.
//
//   akeir test [filter] [--junit results.xml] [--results-dir DIR] [--no-artifacts] [--update-golden] [--list] [--json]
//   결과 디렉터리 기본값: <project>/Tests/.results/<run_id>/  (results.json, artifacts/<test>/tick_NNNN.snapshot.json)
//   exit code: 전부 통과 0, 실패/오류 있으면 3 (§13 findings), 테스트가 없으면 5 (not found)
#include "Commands.h"
#include "GameSystems.h"
#include "akeir/core/ExitCodes.h"
#include "akeir/core/FpEnv.h"
#include "akeir/runtime/Components.h"
#include "akeir/serialization/Canonical.h"
#include "akeir/testing/Expr.h"
#include "akeir/testing/TestRunner.h"

#include <filesystem>
#include <fstream>

namespace akeir::cli {

namespace {

Envelope cmdTest(Context& ctx) {
    Envelope fail;
    game::registerGameComponents();
    auto prj = openProject(ctx, fail, "test");
    if (!prj) return fail;
    normalizeFpEnv();

    const std::string filter = ctx.args.positional(1, "");
    WorldFactory factory = [](const Project& p, const std::string& worldId, const PlayWorldConfig& cfg, std::vector<Diagnostic>& d) {
        auto w = PlayWorld::build(p, worldId, cfg, d);
        if (w) game::registerGameSystems(*w);
        return w;
    };
    TestRunnerOptions opts;
    opts.saveArtifacts = !ctx.args.has("no-artifacts");
    opts.updateGolden = ctx.args.has("update-golden");
    installCaptureHooks(opts);   // SDL 빌드에서만 capture assertion 이 동작 (SdlCommands.cpp)
    opts.runInfo = Json{{"engineVersion", AKEIR_VERSION_STRING}, {"fpFlagsHash", AKEIR_FP_FLAGS_HASH}, {"platform", "win-x64"}, {"projectDir", prj->rootDir()}};
    TestRunner runner(*prj, factory, opts);   // resultsDir 는 discover 뒤에 정한다 (run id 가 필요)

    auto scenarios = runner.discover("Tests", filter);
    if (ctx.args.has("list")) {
        Json arr = Json::array();
        for (const auto& s : scenarios) arr.push_back(Json{{"name", s.name}, {"file", s.file}, {"asserts", s.asserts.size()}, {"ticks", s.ticks}, {"determinismRuns", s.determinismRuns}, {"problems", s.problems}});
        return Envelope::success("test.list", Json{{"tests", arr}, {"count", arr.size()}});
    }
    if (scenarios.empty())
        return Envelope::failure("test", CommandError::make(ErrorCategory::NotFound, "TESTS_NOT_FOUND",
            filter.empty() ? "No Tests/**/*.test.json in the project." : "No test matches '" + filter + "'.", Json{{"dir", (std::filesystem::path(prj->rootDir()) / "Tests").string()}, {"filter", filter}}));

    std::string resultsDir = ctx.args.getOr("results-dir", "");
    if (resultsDir.empty()) resultsDir = (std::filesystem::path(prj->rootDir()) / "Tests" / ".results" / Id::generate("run").str()).string();
    opts.resultsDir = resultsDir;
    TestRunner runner2(*prj, factory, opts);
    TestReport rep = runner2.runAll(scenarios);

    Json result = rep.toJson();
    result["resultsDir"] = resultsDir;
    result["resultsFile"] = (std::filesystem::path(resultsDir) / "results.json").string();
    if (auto junit = ctx.args.get("junit")) {
        std::error_code ec;
        if (std::filesystem::path(*junit).has_parent_path()) std::filesystem::create_directories(std::filesystem::path(*junit).parent_path(), ec);
        std::ofstream out(*junit, std::ios::binary | std::ios::trunc);
        if (out) { out << rep.junitXml(); result["junit"] = *junit; }
        else result["junitError"] = "cannot write " + *junit;
    }
    Envelope env;
    if (rep.allPassed()) env = Envelope::success("test", result);
    else {
        Json sum = rep.summary();
        env = Envelope::failure("test", CommandError::make(ErrorCategory::Validation, "TEST_FAILED",
            std::to_string(sum["failed"].get<int>()) + " failed, " + std::to_string(sum["errored"].get<int>()) + " errored of " + std::to_string(sum["total"].get<std::size_t>()) +
            ". See error.details.tests[].failures (tick, bindings) and the snapshot artifacts in " + resultsDir + ".", result));
    }
    for (const auto& d : ctx.loadDiagnostics) env.withWarning(d);
    return env;
}

} // namespace

namespace {
std::vector<std::string> splitCsv(const std::string& s) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (c == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); } else cur += c; }
    if (!cur.empty()) out.push_back(cur);
    return out;
}
} // namespace

// akeir test explain "<expr>" [--snapshot f.json --as name=entity_id …] — parse an assertion expression, show what it
// references, and (with a snapshot from `run --snapshot-out`) evaluate it. ADR-0039: typos are caught before a test runs.
Envelope cmdTestExplain(Context& ctx) {
    const std::string text = ctx.args.positional(2);
    if (text.empty()) return Envelope::failure("test.explain", CommandError::make(ErrorCategory::Usage, "USAGE_ERROR", "akeir test explain \"<expr>\" [--snapshot Cache/snap.json] [--as player=<entity id>]", Json{{"reference", expr::Expr::reference()}}));
    expr::ParseError perr;
    auto e = expr::Expr::parse(text, &perr);
    if (!e) {
        std::string caret(perr.offset, ' ');
        return Envelope::failure("test.explain", CommandError::make(ErrorCategory::Usage, "EXPR_PARSE_ERROR", perr.message + " at offset " + std::to_string(perr.offset),
                                 Json{{"expr", text}, {"offset", perr.offset}, {"pointer", text + "\n" + caret + "^"}, {"functions", expr::Expr::functionNames()}, {"hint", "akeir schema test --json -> expression (grammar, functions, examples)"}}));
    }
    Json r = Json{{"expr", text}, {"ok", true}, {"roots", e->roots()}};
    if (auto snap = ctx.args.get("snapshot")) {
        std::string err;
        auto sj = readJsonFile(*snap, &err);
        if (!sj) return Envelope::failure("test.explain", CommandError::make(ErrorCategory::NotFound, "SNAPSHOT_UNREADABLE", "Cannot read snapshot " + *snap + ": " + err));
        std::map<std::string, Json> bindings;
        bindings["world"] = *sj;
        // --as name=entity_id binds an entity's components like a setup step would
        if (auto as = ctx.args.get("as")) {
            for (const auto& pair : splitCsv(*as)) {
                auto eq = pair.find('=');
                if (eq == std::string::npos) continue;
                std::string name = pair.substr(0, eq), id = pair.substr(eq + 1);
                Json comps = nullptr;
                if (sj->contains("entities") && (*sj)["entities"].is_array())
                    for (const auto& ent : (*sj)["entities"]) if (ent.value("id", "") == id) comps = ent.value("components", Json::object());
                bindings[name] = comps;
            }
        }
        try {
            Json value = e->eval(bindings);
            r["value"] = value;
            r["bindings"] = e->probeBindings(bindings);
        } catch (const expr::EvalError& ev) {
            r["error"] = ev.message;
            r["bindings"] = e->probeBindings(bindings);
            r["hint"] = "an undefined path outside has() is an error, not false — check the binding names (roots) and the snapshot";
        }
    } else {
        r["hint"] = "add --snapshot <file from run --snapshot-out> [--as name=entity_id] to evaluate it";
    }
    return Envelope::success("test.explain", r);
}

void registerTestCommands(std::vector<CommandSpec>& t) {
    t.push_back({"test.explain", {"test", "explain"}, "Query", "Parse / evaluate an assertion expression (§23.1)",
                 "Parses an assertion expression, reports what it references (roots), and with --snapshot evaluates it against a snapshot from `akeir run --headless --snapshot-out f`. Unknown functions and syntax errors come back with the offset and a 'did you mean'. The language reference: `akeir schema test`.",
                 "akeir test explain \"player.Health.current > 0\" [--snapshot Cache/snap.json] [--as player=<entity id>] [--json]", true, false, true, cmdTestExplain});
    t.push_back({"test", {"test"}, "Query", "Run data-driven test scenarios (§23)",
                 "Runs Tests/**/*.test.json: setup (spawn/bind), scripted inputs, assertions on frame snapshots (always/eventually/at), run-twice determinism. Writes results.json (+ --junit). Exit 3 on failures.",
                 "akeir test [filter] [--junit out.xml] [--results-dir DIR] [--no-artifacts] [--update-golden] [--list] [--json]", true, false, true, cmdTest});
}

} // namespace akeir::cli
