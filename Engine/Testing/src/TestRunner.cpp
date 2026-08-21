// TestRunner.cpp — §23 시나리오 파싱/실행, §23.1 assertion 평가, §22.2 run-twice 결정성, §24 results.json / JUnit
#include "pme/testing/TestRunner.h"

#include "pme/core/Hash.h"
#include "pme/core/Id.h"
#include "pme/core/Log.h"
#include "pme/core/Time.h"
#include "pme/runtime/Application.h"
#include "pme/serialization/Canonical.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace pme {

// ---------------------------------------------------------------- parsing

TestScenario TestScenario::fromJson(const Json& j, const std::string& file) {
    TestScenario s;
    s.file = file;
    if (!j.is_object()) { s.problems.push_back("test file is not a JSON object"); return s; }
    s.name = j.value("name", fs::path(file).stem().stem().string());
    s.world = j.value("world", "");
    if (j.contains("seed") && j["seed"].is_number()) { s.seed = j["seed"].get<std::uint64_t>(); s.hasSeed = true; }
    if (j.contains("setup")) {
        if (!j["setup"].is_array()) s.problems.push_back("'setup' must be an array");
        else for (const auto& st : j["setup"]) {
            TestSetupStep step;
            step.spawn = st.value("spawn", "");
            step.entity = st.value("entity", "");
            step.as = st.value("as", "");
            step.name = st.value("name", "");
            if (st.contains("position")) step.position = st["position"];
            if (st.contains("set") && st["set"].is_object()) step.set = st["set"];
            if (st.contains("tags") && st["tags"].is_array()) for (const auto& t : st["tags"]) if (t.is_string()) step.tags.push_back(t.get<std::string>());
            if (step.spawn.empty() && step.entity.empty()) s.problems.push_back("setup step needs 'spawn' (prefab) or 'entity' (selector)");
            if (step.as.empty()) s.problems.push_back("setup step needs 'as' (binding name)");
            s.setup.push_back(std::move(step));
        }
    }
    if (j.contains("inputs")) {
        if (!j["inputs"].is_array()) s.problems.push_back("'inputs' must be an array");
        else for (const auto& in : j["inputs"]) {
            TestInputStep step;
            step.tick = in.value("tick", 0);
            if (in.contains("untilTick")) step.untilTick = in["untilTick"].get<std::int64_t>();
            if (in.contains("hold") && in["hold"].is_object()) step.hold = in["hold"];
            if (in.contains("axis") && in["axis"].is_object()) step.axis = in["axis"];
            step.press = in.value("press", "");
            step.release = in.value("release", "");
            if (step.hold.empty() && step.axis.empty() && step.press.empty() && step.release.empty()) s.problems.push_back("inputs[" + std::to_string(s.inputs.size()) + "] needs hold/axis/press/release");
            s.inputs.push_back(std::move(step));
        }
    }
    if (j.contains("requires") && j["requires"].is_array()) for (const auto& r : j["requires"]) if (r.is_string()) s.requirements.push_back(r.get<std::string>());
    if (j.contains("run") && j["run"].is_object()) {
        s.ticks = j["run"].value("ticks", 600);
        s.tickRate = j["run"].value("tickRate", 0);
    }
    if (j.contains("determinism") && j["determinism"].is_object()) {
        const Json& d = j["determinism"];
        s.determinismRuns = std::max(1, d.value("runs", 1));
        s.hashEvery = std::max<std::int64_t>(1, d.value("hashEvery", 60));
        if (d.contains("expectedFinalHash") && d["expectedFinalHash"].is_string()) s.expectedFinalHash = d["expectedFinalHash"].get<std::string>();
    }
    if (j.contains("assert")) {
        if (!j["assert"].is_array()) s.problems.push_back("'assert' must be an array");
        else for (const auto& a : j["assert"]) {
            TestAssert as;
            as.id = a.value("id", "assert-" + std::to_string(s.asserts.size()));
            as.exprText = a.value("expr", "");
            if (a.contains("capture") && a["capture"].is_object()) as.capture = a["capture"];
            if (as.exprText.empty() && as.capture.is_null()) { s.problems.push_back(as.id + ": needs 'expr' or 'capture'"); continue; }
            if (a.contains("always") && a["always"].is_boolean() && a["always"].get<bool>()) as.when = TestAssert::When::Always;
            else if (a.contains("eventually")) { as.when = TestAssert::When::Eventually; if (a["eventually"].is_object()) as.withinTicks = a["eventually"].value("withinTicks", 0); }
            else if (a.contains("at") && a["at"].is_number_integer()) { as.when = TestAssert::When::AtTick; as.tick = a["at"].get<std::int64_t>(); }
            else as.when = TestAssert::When::AtEnd;
            if (!as.capture.is_null()) {
                if (as.when == TestAssert::When::Always || as.when == TestAssert::When::Eventually) { s.problems.push_back(as.id + ": capture assertions support only 'at' (tick or \"end\")"); continue; }
                s.asserts.push_back(std::move(as));
                continue;
            }
            expr::ParseError pe;
            as.expr = expr::Expr::parse(as.exprText, &pe);
            if (!as.expr) { as.parseError = pe.message + " at offset " + std::to_string(pe.offset); s.problems.push_back(as.id + ": " + as.parseError); }
            s.asserts.push_back(std::move(as));
        }
    }
    if (s.asserts.empty() && s.determinismRuns < 2 && !s.expectedFinalHash) s.problems.push_back("test has no assertions and no determinism check");
    return s;
}

// ---------------------------------------------------------------- results

Json TestFailure::toJson() const {
    Json j = Json{{"assertId", assertId}, {"expr", expr}, {"tick", tick}, {"expected", expected}, {"actual", actual}};
    if (!note.empty()) j["note"] = note;
    j["bindings"] = bindings;
    Diagnostic d = Diagnostic::error("TEST_ASSERTION_FAILED", assertId + " @ tick " + std::to_string(tick) + ": " + expr + (note.empty() ? "" : " — " + note));
    j["diagnostic"] = d.toJson();
    return j;
}

Json TestResult::toJson() const {
    Json j = Json::object();
    j["name"] = name; j["file"] = file; j["status"] = status; j["durationMs"] = durationMs; j["ticksRun"] = ticksRun; j["seed"] = seed;
    if (abortedAt) { j["abortedAt"] = *abortedAt; j["abortReason"] = abortReason; }
    if (!error.empty()) j["error"] = error;
    Json f = Json::array();
    for (const auto& x : failures) f.push_back(x.toJson());
    j["failures"] = f;
    if (!determinism.is_null()) j["determinism"] = determinism;
    j["artifacts"] = artifacts;
    return j;
}

Json TestReport::summary() const {
    int passed = 0, failed = 0, errored = 0, skipped = 0;
    for (const auto& t : tests) { if (t.status == "passed") ++passed; else if (t.status == "failed") ++failed; else if (t.status == "errored") ++errored; else ++skipped; }
    return Json{{"total", tests.size()}, {"passed", passed}, {"failed", failed}, {"errored", errored}, {"skipped", skipped}};
}

bool TestReport::allPassed() const { for (const auto& t : tests) if (t.status == "failed" || t.status == "errored") return false; return true; }

Json TestReport::toJson() const {
    Json j = Json::object();
    j["schemaVersion"] = 1;
    Json r = run;
    r["id"] = id; r["startedAt"] = startedAt; r["durationMs"] = durationMs;
    j["run"] = r;
    j["summary"] = summary();
    Json t = Json::array();
    for (const auto& x : tests) t.push_back(x.toJson());
    j["tests"] = t;
    return j;
}

namespace {
std::string xmlEscape(const std::string& s) {
    std::string o;
    for (char c : s) switch (c) { case '&': o += "&amp;"; break; case '<': o += "&lt;"; break; case '>': o += "&gt;"; break; case '"': o += "&quot;"; break; default: o += c; }
    return o;
}
} // namespace

std::string TestReport::junitXml() const {
    // testsuite = 테스트 파일의 디렉터리 (Tests/Combat → "Tests.Combat")
    std::map<std::string, std::vector<const TestResult*>> suites;
    for (const auto& t : tests) {
        std::string dir = fs::path(t.file).parent_path().generic_string();
        std::replace(dir.begin(), dir.end(), '/', '.');
        suites[dir.empty() ? "Tests" : dir].push_back(&t);
    }
    std::ostringstream x;
    x << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<testsuites name=\"" << xmlEscape(id) << "\" time=\"" << durationMs / 1000.0 << "\">\n";
    for (const auto& [suite, list] : suites) {
        int fails = 0, errs = 0;
        double time = 0;
        for (const auto* t : list) { if (t->status == "failed") ++fails; if (t->status == "errored") ++errs; time += t->durationMs; }
        x << "  <testsuite name=\"" << xmlEscape(suite) << "\" tests=\"" << list.size() << "\" failures=\"" << fails << "\" errors=\"" << errs << "\" time=\"" << time / 1000.0 << "\">\n";
        for (const auto* t : list) {
            x << "    <testcase classname=\"" << xmlEscape(suite) << "\" name=\"" << xmlEscape(t->name) << "\" file=\"" << xmlEscape(t->file) << "\" time=\"" << t->durationMs / 1000.0 << "\">\n";
            for (const auto& f : t->failures)
                x << "      <failure message=\"" << xmlEscape(f.assertId + " @ tick " + std::to_string(f.tick)) << "\" type=\"TEST_ASSERTION_FAILED\">" << xmlEscape(f.expr + "\n" + f.bindings.dump() + (f.note.empty() ? "" : "\n" + f.note)) << "</failure>\n";
            if (!t->determinism.is_null() && t->determinism.is_object() && t->determinism.value("passed", true) == false)
                x << "      <failure message=\"determinism\" type=\"DETERMINISM_DIVERGENCE\">" << xmlEscape(t->determinism.dump()) << "</failure>\n";
            if (t->status == "errored") x << "      <error message=\"" << xmlEscape(t->error) << "\"/>\n";
            if (!t->artifacts.empty()) {
                x << "      <system-out>";
                for (const auto& a : t->artifacts) x << "[[ATTACHMENT|" << xmlEscape(a.value("path", "")) << "]]\n";
                x << "</system-out>\n";
            }
            x << "    </testcase>\n";
        }
        x << "  </testsuite>\n";
    }
    x << "</testsuites>\n";
    return x.str();
}

// ---------------------------------------------------------------- runner

TestRunner::TestRunner(const Project& project, WorldFactory factory, TestRunnerOptions options)
    : project_(project), factory_(std::move(factory)), options_(std::move(options)) {}

std::vector<TestScenario> TestRunner::discover(const std::string& dir, const std::string& filter) const {
    std::vector<TestScenario> out;
    fs::path root = fs::path(project_.rootDir()) / dir;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return out;
    std::vector<fs::path> files;
    for (const auto& e : fs::recursive_directory_iterator(root, ec)) {
        if (!e.is_regular_file()) continue;
        std::string fn = e.path().filename().string();
        if (fn.size() > 10 && fn.compare(fn.size() - 10, 10, ".test.json") == 0) files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        std::string rel = fs::relative(f, project_.rootDir(), ec).generic_string();
        std::string err;
        auto j = readJsonFile(f.string(), &err);
        TestScenario s;
        if (!j) { s.file = rel; s.name = f.stem().stem().string(); s.problems.push_back("cannot parse: " + err); }
        else s = TestScenario::fromJson(*j, rel);
        if (!filter.empty() && s.name.find(filter) == std::string::npos && rel.find(filter) == std::string::npos) continue;
        out.push_back(std::move(s));
    }
    return out;
}

namespace {

struct SimSetup {
    std::unique_ptr<PlayWorld> world;
    std::map<std::string, std::string> bindings;   // as → entity id
    std::map<std::int64_t, InputFrame> frames;
    std::string error;
};

/// world build + setup + inputs 준비 (결정적: 같은 시나리오 → 같은 world)
SimSetup prepare(const Project& project, const WorldFactory& factory, const TestScenario& sc, std::uint64_t seed, int tickRate) {
    SimSetup out;
    // world 선택
    std::string worldId;
    if (sc.world.empty()) { auto dw = project.defaultWorld(); if (!dw) { out.error = "no 'world' and project has no defaultWorld"; return out; } worldId = *dw; }
    else {
        std::vector<std::string> ws;
        for (const auto& id : project.resolveSelector(sc.world)) if (auto l = project.locate(id); l && l->kind == "world") ws.push_back(id);
        if (ws.size() != 1) { out.error = "world '" + sc.world + "' does not match exactly one world"; return out; }
        worldId = ws[0];
    }
    PlayWorldConfig cfg;
    cfg.seed = seed;
    cfg.tickRate = tickRate;
    std::vector<Diagnostic> diags;
    out.world = factory(project, worldId, cfg, diags);
    if (!out.world) { out.error = "world build failed: " + (diags.empty() ? std::string("?") : diags.front().message.text); return out; }

    // setup
    for (const auto& st : sc.setup) {
        if (!st.entity.empty()) {
            std::vector<std::string> ids;
            for (const auto& id : project.resolveSelector(st.entity)) if (out.world->hasEntity(id)) ids.push_back(id);
            if (ids.size() != 1) { out.error = "setup '" + st.as + "': entity '" + st.entity + "' does not match exactly one entity in the world"; return out; }
            out.bindings[st.as] = ids[0];
            continue;
        }
        std::vector<std::string> ps;
        for (const auto& id : project.resolveSelector(st.spawn)) if (auto l = project.locate(id); l && l->kind == "prefab") ps.push_back(id);
        if (ps.size() != 1) { out.error = "setup '" + st.as + "': prefab '" + st.spawn + "' does not match exactly one prefab"; return out; }
        auto comps = project.resolvePrefab(ps[0], &diags);
        if (!comps) { out.error = "setup '" + st.as + "': prefab resolve failed"; return out; }
        Json root = Json::object();
        root["components"] = *comps;
        if (st.position) {
            if (!root["components"].contains("Transform")) root["components"]["Transform"] = Json::object();
            root["components"]["Transform"]["position"] = *st.position;
        }
        for (const auto& [ptr, v] : st.set.items()) {
            try { root[Json::json_pointer(ptr)] = v; } catch (const std::exception& e) { out.error = "setup '" + st.as + "': bad set pointer " + ptr + ": " + e.what(); return out; }
        }
        const Json* pdoc = project.document(project.locate(ps[0])->doc);
        std::vector<std::string> tags = st.tags;
        if (pdoc && pdoc->contains("tags") && (*pdoc)["tags"].is_array()) for (const auto& t : (*pdoc)["tags"]) if (t.is_string()) tags.push_back(t.get<std::string>());
        std::string name = !st.name.empty() ? st.name : (!st.as.empty() ? st.as : pdoc ? pdoc->value("name", "spawned") : "spawned");
        std::string id = out.world->spawn(name, root["components"], tags);
        if (id.empty()) { out.error = "setup '" + st.as + "': spawn failed"; return out; }
        out.bindings[st.as] = id;
    }

    // inputs → frames (hold/axis 범위, press 한 tick, release 로 종료)
    std::map<std::string, std::vector<std::pair<std::int64_t, std::int64_t>>> releases;   // 미리 release tick 수집
    for (const auto& in : sc.inputs) if (!in.release.empty()) releases[in.release].push_back({in.tick, in.tick});
    auto releaseAfter = [&](const std::string& action, std::int64_t from) -> std::int64_t {
        std::int64_t best = sc.ticks;
        auto it = releases.find(action);
        if (it != releases.end()) for (const auto& [t, _] : it->second) if (t > from && t < best) best = t;
        return best;
    };
    auto setRange = [&](const std::string& action, float value, std::int64_t from, std::int64_t to) {
        for (std::int64_t t = std::max<std::int64_t>(0, from); t < std::min(to, sc.ticks); ++t) { auto& f = out.frames[t]; f.tick = t; f.actions[action] = value; }
    };
    for (const auto& in : sc.inputs) {
        for (const auto& [action, v] : in.hold.items()) setRange(action, v.is_number() ? v.get<float>() : 1.0f, in.tick, in.untilTick ? *in.untilTick : releaseAfter(action, in.tick));
        for (const auto& [action, v] : in.axis.items()) setRange(action, v.is_number() ? v.get<float>() : 1.0f, in.tick, in.untilTick ? *in.untilTick : releaseAfter(action, in.tick));
        if (!in.press.empty()) setRange(in.press, 1.0f, in.tick, in.tick + 1);
    }
    return out;
}

void tickOnce(PlayWorld& w, std::map<std::int64_t, InputFrame>& frames, SimTime& st) {
    auto it = frames.find(st.tick);
    InputFrame f;
    if (it != frames.end()) f = it->second; else f.tick = st.tick;
    w.tick(f, st);
    st.advance();
}

std::map<std::string, Json> makeBindings(const Json& snapshot, const std::map<std::string, std::string>& ids) {
    std::map<std::string, Json> b;
    b["world"] = snapshot;
    for (const auto& [as, id] : ids) {
        Json comps = Json();
        for (const auto& e : snapshot["entities"]) if (e.value("id", "") == id) { comps = e.value("components", Json::object()); break; }
        b[as] = comps;   // despawn 되었으면 null → 멤버 접근은 undefined
    }
    return b;
}

} // namespace

Json TestRunner::diffSnapshots(const Json& a, const Json& b, std::size_t limit) {
    Json out = Json::array();
    std::map<std::string, const Json*> ea, eb;
    if (a.contains("entities")) for (const auto& e : a["entities"]) ea[e.value("id", "")] = &e;
    if (b.contains("entities")) for (const auto& e : b["entities"]) eb[e.value("id", "")] = &e;
    std::function<void(const std::string&, const std::string&, const Json&, const Json&)> walk = [&](const std::string& entity, const std::string& path, const Json& x, const Json& y) {
        if (out.size() >= limit) return;
        if (x.is_object() && y.is_object()) {
            for (const auto& [k, v] : x.items()) { if (!y.contains(k)) out.push_back(Json{{"entity", entity}, {"path", path + "/" + k}, {"a", v}, {"b", nullptr}}); else walk(entity, path + "/" + k, v, y[k]); }
            for (const auto& [k, v] : y.items()) if (!x.contains(k)) out.push_back(Json{{"entity", entity}, {"path", path + "/" + k}, {"a", nullptr}, {"b", v}});
            return;
        }
        if (x.is_array() && y.is_array() && x.size() == y.size()) { for (std::size_t i = 0; i < x.size(); ++i) walk(entity, path + "/" + std::to_string(i), x[i], y[i]); return; }
        if (x != y) out.push_back(Json{{"entity", entity}, {"path", path}, {"a", x}, {"b", y}});
    };
    for (const auto& [id, e] : ea) {
        auto it = eb.find(id);
        if (it == eb.end()) { out.push_back(Json{{"entity", id}, {"path", ""}, {"a", "present"}, {"b", "missing"}}); continue; }
        walk(id, "", e->value("components", Json::object()), it->second->value("components", Json::object()));
    }
    for (const auto& [id, e] : eb) if (!ea.count(id)) out.push_back(Json{{"entity", id}, {"path", ""}, {"a", "missing"}, {"b", "present"}});
    if (a.value("rng", Json()) != b.value("rng", Json())) out.push_back(Json{{"entity", "<rng>"}, {"path", "/rng"}, {"a", a.value("rng", Json())}, {"b", b.value("rng", Json())}});
    return out;
}

TestResult TestRunner::run(const TestScenario& sc) {
    TestResult r;
    r.name = sc.name;
    r.file = sc.file;
    Stopwatch sw;
    if (!sc.problems.empty()) {
        r.status = "errored";
        r.error = sc.problems.front() + (sc.problems.size() > 1 ? " (+" + std::to_string(sc.problems.size() - 1) + " more)" : "");
        r.durationMs = sw.elapsedMs();
        return r;
    }
    for (const auto& req : sc.requirements) {
        if (req == "renderer" && !options_.capture) { r.status = "skipped"; r.error = "requires renderer (no CaptureHook: build with PME_WITH_SDL=ON)"; r.durationMs = sw.elapsedMs(); return r; }
        if (req != "renderer") { r.status = "skipped"; r.error = "unknown requirement '" + req + "'"; r.durationMs = sw.elapsedMs(); return r; }
    }
    const std::uint64_t seed = sc.hasSeed ? sc.seed : project_.seed();
    const int tickRate = sc.tickRate > 0 ? sc.tickRate : project_.tickRate();
    r.seed = seed;

    fs::path artDir;
    if (!options_.resultsDir.empty() && options_.saveArtifacts) {
        artDir = fs::path(options_.resultsDir) / "artifacts" / sc.name;
        std::error_code ec;
        fs::create_directories(artDir, ec);
    }
    auto saveSnapshot = [&](const Json& snap, std::int64_t tick, const char* suffix) {
        if (artDir.empty()) return;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "tick_%04lld%s.snapshot.json", static_cast<long long>(tick), suffix);
        fs::path p = artDir / buf;
        std::string err;
        if (writeCanonicalFile(p.string(), snap, &err)) {
            std::error_code ec;
            r.artifacts.push_back(Json{{"kind", "snapshot"}, {"path", fs::relative(p, options_.resultsDir, ec).generic_string()}, {"tick", tick}});
        }
    };

    // ---- 본 실행 (assertion 평가)
    SimSetup sim = prepare(project_, factory_, sc, seed, tickRate);
    if (!sim.world) { r.status = "errored"; r.error = sim.error; r.durationMs = sw.elapsedMs(); return r; }

    struct Pending { const TestAssert* a; bool done = false; };
    std::vector<Pending> pend;
    for (const auto& a : sc.asserts) pend.push_back({&a});
    const bool perTick = std::any_of(sc.asserts.begin(), sc.asserts.end(), [](const TestAssert& a) { return a.when == TestAssert::When::Always || a.when == TestAssert::When::Eventually; });

    std::vector<std::uint64_t> hashes;   // hashEvery 마다
    SimTime st;
    st.tickRate = tickRate;
    bool aborted = false;

    auto captureAssert = [&](Pending& p, std::int64_t tick, const std::string& note) -> bool {
        const Json& spec = p.a->capture;
        const int w = spec.value("width", 512), h = spec.value("height", 512);
        auto failWith = [&](const std::string& why, Json actual) {
            TestFailure f;
            f.assertId = p.a->id; f.expr = "capture " + spec.dump(); f.tick = tick; f.expected = true; f.actual = std::move(actual); f.note = why + (note.empty() ? "" : " (" + note + ")");
            r.failures.push_back(std::move(f));
            return false;
        };
        if (!options_.capture) return failWith("TEST_CAPTURE_REQUIRES_RENDERER: this build/run has no renderer (build with PME_WITH_SDL=ON; msvc-debug preset)", nullptr);
        if (artDir.empty()) return failWith("capture needs a resultsDir for artifacts", nullptr);
        char buf[96];
        std::snprintf(buf, sizeof(buf), "%s_%dx%d.png", p.a->id.c_str(), w, h);
        fs::path actual = artDir / buf;
        std::string err;
        if (!options_.capture(*sim.world, w, h, actual.string(), &err)) return failWith("capture failed: " + err, nullptr);
        std::error_code ec;
        r.artifacts.push_back(Json{{"kind", "screenshot"}, {"path", fs::relative(actual, options_.resultsDir, ec).generic_string()}, {"tick", tick}});
        std::string golden = spec.value("golden", "");
        if (golden.empty()) return true;   // capture 만 (vision 검사용 artifact)
        std::snprintf(buf, sizeof(buf), "%s_%dx%d.png", golden.c_str(), w, h);
        fs::path goldenPath = fs::path(project_.rootDir()) / options_.goldenDir / sc.name / buf;
        if (!fs::exists(goldenPath, ec)) {
            if (options_.updateGolden) {
                fs::create_directories(goldenPath.parent_path(), ec);
                fs::copy_file(actual, goldenPath, fs::copy_options::overwrite_existing, ec);
                return true;
            }
            return failWith("golden image missing: " + goldenPath.generic_string() + " (run with --update-golden to create it from this capture)", Json{{"actual", actual.generic_string()}});
        }
        if (!options_.compare) return failWith("no compare hook", nullptr);
        fs::path diffPath = artDir / (p.a->id + "_diff.png");
        Json cmp = options_.compare(goldenPath.string(), actual.string(), spec.value("tolerance", Json::object()), diffPath.string());
        if (cmp.value("ok", false)) return true;
        if (options_.updateGolden) { fs::copy_file(actual, goldenPath, fs::copy_options::overwrite_existing, ec); return true; }
        r.artifacts.push_back(Json{{"kind", "diff"}, {"path", fs::relative(diffPath, options_.resultsDir, ec).generic_string()}, {"tick", tick}});
        r.artifacts.push_back(Json{{"kind", "golden"}, {"path", goldenPath.generic_string()}});
        return failWith("golden mismatch: " + std::to_string(cmp.value("mismatchedPixels", 0LL)) + " px (ratio " + std::to_string(cmp.value("ratio", 0.0)) + ")" + (cmp.contains("error") ? " " + cmp["error"].get<std::string>() : ""), cmp);
    };

    auto evaluate = [&](Pending& p, const Json& snap, const std::map<std::string, Json>& b, std::int64_t tick, const std::string& note) -> bool {
        // true = 만족
        if (!p.a->capture.is_null()) return captureAssert(p, tick, note);
        try {
            bool ok = p.a->expr->evalBool(b);
            if (ok) return true;
            if (p.a->when == TestAssert::When::Eventually && note.empty()) return false;   // 아직 창 안
            TestFailure f;
            f.assertId = p.a->id; f.expr = p.a->exprText; f.tick = tick; f.expected = true; f.actual = false; f.note = note;
            f.bindings = p.a->expr->probeBindings(b);
            r.failures.push_back(std::move(f));
            if (options_.snapshotOnFailure) saveSnapshot(snap, tick, "");
            return false;
        } catch (const expr::EvalError& e) {
            TestFailure f;
            f.assertId = p.a->id; f.expr = p.a->exprText; f.tick = tick; f.expected = true; f.actual = nullptr;
            f.note = "evaluation error: " + e.message + (note.empty() ? "" : " (" + note + ")");
            f.bindings = p.a->expr->probeBindings(b);
            r.failures.push_back(std::move(f));
            if (options_.snapshotOnFailure) saveSnapshot(snap, tick, "");
            return false;
        }
    };

    // tick 0 상태 (setup 직후) 에 대한 at:0 / eventually 평가는 첫 tick 뒤에 한다 — "tick N 의 snapshot" = N tick 을 돌린 뒤
    for (std::int64_t i = 0; i < sc.ticks && !aborted; ++i) {
        tickOnce(*sim.world, sim.frames, st);
        const std::int64_t tick = st.tick;   // 지금까지 돌린 tick 수 = snapshot.tick
        if ((tick % sc.hashEvery) == 0) hashes.push_back(sim.world->hash());
        bool need = perTick;
        for (const auto& p : pend) if (!p.done && p.a->when == TestAssert::When::AtTick && p.a->tick == tick) need = true;
        if (!need) continue;
        Json snap = sim.world->snapshot();
        auto b = makeBindings(snap, sim.bindings);
        for (auto& p : pend) {
            if (p.done) continue;
            switch (p.a->when) {
                case TestAssert::When::Always:
                    if (!evaluate(p, snap, b, tick, "")) { p.done = true; aborted = true; r.abortedAt = tick; r.abortReason = "always-assertion '" + p.a->id + "' violated"; }
                    break;
                case TestAssert::When::Eventually: {
                    std::int64_t window = p.a->withinTicks > 0 ? p.a->withinTicks : sc.ticks;
                    if (evaluate(p, snap, b, tick, "")) p.done = true;
                    else if (tick >= window) { evaluate(p, snap, b, tick, "not satisfied within " + std::to_string(window) + " ticks"); p.done = true; }
                    break;
                }
                case TestAssert::When::AtTick:
                    if (p.a->tick == tick) { evaluate(p, snap, b, tick, ""); p.done = true; }
                    break;
                case TestAssert::When::AtEnd: break;
            }
            if (aborted) break;
        }
    }
    r.ticksRun = st.tick;
    {
        Json snap = sim.world->snapshot();
        auto b = makeBindings(snap, sim.bindings);
        for (auto& p : pend) {
            if (p.done) continue;
            std::string note = aborted ? "evaluated at abort tick " + std::to_string(st.tick) : "";
            if (p.a->when == TestAssert::When::AtEnd || p.a->when == TestAssert::When::Always) { evaluate(p, snap, b, st.tick, note); p.done = true; }
            else if (p.a->when == TestAssert::When::Eventually) {
                bool already = false;
                for (const auto& f : r.failures) if (f.assertId == p.a->id) already = true;   // 중단 tick 에서 이미 평가 오류로 기록됐으면 중복 보고하지 않는다
                if (!already) evaluate(p, snap, b, st.tick, aborted ? "run aborted before the eventually window closed" : "not satisfied within the run");
                p.done = true;
            }
            else if (p.a->when == TestAssert::When::AtTick) {
                // 도달하지 못한 at:N 은 식의 값과 무관하게 실패 (평가할 snapshot 이 없다)
                TestFailure f;
                f.assertId = p.a->id; f.expr = p.a->exprText; f.tick = st.tick; f.expected = true; f.actual = nullptr;
                f.note = "at:" + std::to_string(p.a->tick) + " was never reached (run ended at " + std::to_string(st.tick) + ")";
                r.failures.push_back(std::move(f));
                p.done = true;
            }
        }
        const std::uint64_t finalHash = sim.world->hash();
        Json det = Json::object();
        det["runs"] = sc.determinismRuns;
        det["hashEvery"] = sc.hashEvery;
        det["finalHash"] = toHex64(finalHash);
        det["passed"] = true;
        if (sc.expectedFinalHash) {
            det["expectedFinalHash"] = *sc.expectedFinalHash;
            if (*sc.expectedFinalHash != toHex64(finalHash)) { det["passed"] = false; det["reason"] = "finalHash differs from expectedFinalHash"; }
        }
        // ---- run-twice (§22.2 T0)
        if (sc.determinismRuns >= 2 && !aborted) {
            SimSetup simB = prepare(project_, factory_, sc, seed, tickRate);
            if (!simB.world) { det["passed"] = false; det["reason"] = "second run could not be prepared: " + simB.error; }
            else {
                SimTime stB; stB.tickRate = tickRate;
                std::vector<std::uint64_t> hashesB;
                std::int64_t divergent = -1;
                for (std::int64_t i = 0; i < sc.ticks; ++i) {
                    tickOnce(*simB.world, simB.frames, stB);
                    if ((stB.tick % sc.hashEvery) == 0) {
                        hashesB.push_back(simB.world->hash());
                        if (hashesB.size() <= hashes.size() && hashesB.back() != hashes[hashesB.size() - 1]) { divergent = stB.tick; break; }
                    }
                }
                det["runA"] = Json{{"threads", 1}, {"hash", toHex64(finalHash)}};
                if (divergent < 0) {
                    std::uint64_t fb = simB.world->hash();
                    det["runB"] = Json{{"threads", 1}, {"hash", toHex64(fb)}};
                    if (fb != finalHash) { divergent = stB.tick; }
                }
                if (divergent >= 0) {
                    det["passed"] = false;
                    det["firstDivergentTick"] = divergent;
                    det["runB"] = Json{{"threads", 1}, {"hash", toHex64(simB.world->hash())}};
                    // A 를 divergent tick 까지 다시 돌려 snapshot 비교
                    SimSetup simA2 = prepare(project_, factory_, sc, seed, tickRate);
                    if (simA2.world) {
                        SimTime stA; stA.tickRate = tickRate;
                        for (std::int64_t i = 0; i < divergent; ++i) tickOnce(*simA2.world, simA2.frames, stA);
                        Json sa = simA2.world->snapshot(), sb = simB.world->snapshot();
                        det["diff"] = diffSnapshots(sa, sb);
                        Json sysA = sa.value("systemHashes", Json::object()), sysB = sb.value("systemHashes", Json::object());
                        for (const auto& [k, v] : sysA.items()) if (sysB.contains(k) && sysB[k] != v) { det["firstDivergentSystem"] = k; break; }
                        saveSnapshot(sa, divergent, "");
                        saveSnapshot(sb, divergent, "_b");
                    }
                    if (r.abortReason.empty()) { r.abortedAt = divergent; r.abortReason = "determinism divergence (runA vs runB)"; }
                }
            }
        }
        r.determinism = det;
        if (det["passed"] == false) r.status = "failed";
    }
    if (!r.failures.empty()) r.status = "failed";
    r.durationMs = sw.elapsedMs();
    return r;
}

TestReport TestRunner::runAll(const std::vector<TestScenario>& scenarios) {
    TestReport rep;
    rep.id = Id::generate("run").str();
    rep.startedAt = WallTime::now().iso8601();
    rep.run = options_.runInfo;
    rep.run["threads"] = 1;
    rep.run["videoDriver"] = "dummy";
    Stopwatch sw;
    for (const auto& s : scenarios) {
        PME_LOG(Info, "test", "start", s.name, Json{{"file", s.file}});
        rep.tests.push_back(run(s));
        PME_LOG(Info, "test", "done", s.name, Json{{"status", rep.tests.back().status}, {"ms", rep.tests.back().durationMs}});
    }
    rep.durationMs = sw.elapsedMs();
    if (!options_.resultsDir.empty()) {
        std::error_code ec;
        fs::create_directories(options_.resultsDir, ec);
        std::string err;
        writeCanonicalFile((fs::path(options_.resultsDir) / "results.json").string(), rep.toJson(), &err);
    }
    return rep;
}

} // namespace pme
