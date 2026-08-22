// Tools/CLI/RunCommands.cpp — 프로젝트 World 를 돌리는 RuntimeControl 명령: run, dump, query, snapshot.
// 설계 문서 §16 (query), §20.1 (headless run), §22.3 (hashes.jsonl), §25 (dump), §26.1 (snapshot), §88.1 (Phase 1: one-shot — 매 명령이 world 를 새로 build 한다; `akeir serve` 는 Phase 4)
#include "Commands.h"
#include "GameSystems.h"
#include "akeir/core/Hash.h"
#include "akeir/core/Log.h"
#include "akeir/ecs/PlayWorld.h"
#include "akeir/runtime/Components.h"
#include "akeir/runtime/DemoSimulation.h"
#include "akeir/runtime/Project.h"
#include "akeir/serialization/Canonical.h"

#include <fstream>

namespace akeir::cli {

namespace {

struct Opened {
    std::optional<Project> project;
    std::unique_ptr<PlayWorld> world;
    std::string worldId;
};

/// 프로젝트 + world 를 연다. --world <id|name:X> 없으면 defaultWorld. 실패 시 envelope 을 채우고 nullopt.
std::optional<Opened> openWorld(Context& ctx, const std::string& command, Envelope& fail, std::uint64_t seedOverride, bool hasSeed) {
    registerBuiltinComponents();
    game::registerGameComponents();
    Opened o;
    if (ctx.projectDir.empty()) {
        fail = Envelope::failure(command, CommandError::make(ErrorCategory::NotFound, "PROJECT_NOT_FOUND", "No project.json found. Use --project <dir> (or run a demo with `akeir run --headless --demo`).", Json::object()));
        return std::nullopt;
    }
    std::vector<Diagnostic> diags;
    if (ctx.resident) o.project = *ctx.resident;   // serve: 상주 authoring 모델의 복사본으로 play world 를 만든다 (§88.2)
    else o.project = Project::load(ctx.projectDir, diags);
    if (!o.project) { fail = Envelope::failure(command, CommandError::make(ErrorCategory::NotFound, "PROJECT_NOT_FOUND", "Cannot load project.", Json{{"projectDir", ctx.projectDir}})); for (auto& d : diags) fail.withWarning(d); return std::nullopt; }
    std::string sel = ctx.args.getOr("world", "");
    if (sel.empty()) { auto dw = o.project->defaultWorld(); if (!dw) { fail = Envelope::failure(command, CommandError::make(ErrorCategory::NotFound, "WORLD_NOT_FOUND", "Project has no worlds.", Json::object())); return std::nullopt; } o.worldId = *dw; }
    else {
        auto ids = o.project->resolveSelector(sel);
        std::vector<std::string> worlds;
        for (const auto& id : ids) if (auto l = o.project->locate(id); l && l->kind == "world") worlds.push_back(id);
        if (worlds.size() != 1) { fail = Envelope::failure(command, CommandError::make(ErrorCategory::NotFound, "WORLD_NOT_FOUND", "--world '" + sel + "' does not match exactly one world.", Json{{"candidates", worlds}})); return std::nullopt; }
        o.worldId = worlds.front();
    }
    PlayWorldConfig cfg;
    cfg.seed = hasSeed ? seedOverride : o.project->seed();
    cfg.tickRate = o.project->tickRate();
    std::vector<Diagnostic> bd;
    o.world = PlayWorld::build(*o.project, o.worldId, cfg, bd);
    if (!o.world) {
        Json details = Json::object();
        Json arr = Json::array(); for (auto& d : bd) arr.push_back(d.toJson());
        details["diagnostics"] = arr;
        fail = Envelope::failure(command, CommandError::make(ErrorCategory::Validation, "WORLD_BUILD_FAILED", "The world could not be built from the authoring data. Run `akeir validate`.", details));
        return std::nullopt;
    }
    game::registerGameSystems(*o.world);
    return o;
}

Json hashesJson(const RunResult& rr) {
    Json arr = Json::array();
    for (const auto& h : rr.hashes) { Json e = Json{{"tick", h.tick}, {"world", toHex64(h.world)}}; if (!h.systems.empty()) e["systems"] = h.systems; arr.push_back(e); }
    return arr;
}

Envelope cmdRun(Context& ctx) {
    const Args& a = ctx.args;
    if (!a.has("headless") && !a.has("demo")) return runWindowed(ctx);   // 창 모드 (SdlCommands.cpp; SDL 없는 빌드면 FEATURE_UNAVAILABLE)
    RunConfig cfg;
    if (auto t = a.getInt("ticks")) cfg.ticks = *t; else if (auto f = a.getInt("frames")) cfg.ticks = *f;
    if (auto he = a.getInt("hash-every")) cfg.hashEvery = *he;
    bool hasSeed = a.getInt("seed").has_value();
    std::uint64_t seed = hasSeed ? static_cast<std::uint64_t>(*a.getInt("seed")) : 0;
    if (cfg.ticks < 0 || cfg.hashEvery < 0)
        return Envelope::failure("run.start", CommandError::make(ErrorCategory::Usage, "USAGE_ERROR", "--ticks and --hash-every must be >= 0.", Json{{"ticks", cfg.ticks}, {"hashEvery", cfg.hashEvery}}));
    std::string hashOut = a.getOr("hash-out", "");
    std::string snapshotOut = a.getOr("snapshot-out", "");
    std::string replayIn = a.getOr("replay", "");

    // 입력: --replay <inputs.jsonl> 또는 없음
    ScriptedInputSource scripted;
    NullInputSource none;
    IInputSource* input = &none;
    if (!replayIn.empty()) {
        std::ifstream in(replayIn);
        if (!in) return Envelope::failure("run.start", CommandError::make(ErrorCategory::NotFound, "REPLAY_NOT_FOUND", "Cannot open --replay file.", Json{{"path", replayIn}}));
        std::string line; int n = 0;
        while (std::getline(in, line)) { if (line.empty()) continue; std::string err; auto j = parseJson(line, &err); if (!j) return Envelope::failure("run.start", CommandError::make(ErrorCategory::Validation, "REPLAY_PARSE_ERROR", "Bad line in replay: " + err, Json{{"line", n}})); scripted.add(InputFrame::fromJson(*j)); ++n; }
        input = &scripted;
    }

    Json r;
    RunResult rr;
    if (a.has("demo") || ctx.projectDir.empty()) {
        cfg.seed = seed; cfg.tickRate = static_cast<std::int32_t>(a.getInt("tick-rate").value_or(60));
        if (cfg.tickRate <= 0) return Envelope::failure("run.start", CommandError::make(ErrorCategory::Usage, "USAGE_ERROR", "--tick-rate must be > 0.", Json::object()));
        DemoSimulation sim(cfg.seed);
        rr = Application::runHeadless(cfg, sim, *input);
        r = rr.toJson(rr.hashes.size() <= 64 && hashOut.empty());
        r["simulation"] = "demo";
    } else {
        Envelope fail;
        auto o = openWorld(ctx, "run.start", fail, seed, hasSeed);
        if (!o) return fail;
        cfg.seed = o->world->seed();
        cfg.tickRate = o->project->tickRate();
        Logger::global().setRunId(Id::generate("run").str());
        AKEIR_LOG(Info, "runtime", "run_start", "Headless run starting.", Json{{"game.world", o->worldId}, {"game.seed", cfg.seed}, {"game.ticks", cfg.ticks}});
        rr = Application::runHeadless(cfg, *o->world, *input);
        AKEIR_LOG(Info, "runtime", "run_end", "Headless run finished.", Json{{"game.ticks_run", rr.ticksRun}, {"game.final_hash", toHex64(rr.finalHash)}});
        r = rr.toJson(rr.hashes.size() <= 64 && hashOut.empty());
        r["simulation"] = "project";
        r["world"] = o->worldId;
        r["entities"] = o->world->entityIds().size();
        r["systems"] = o->world->systemNames();
        r["contactEventsLastTick"] = o->world->contactEvents().size();
        if (a.has("profile")) r["profile"] = o->world->profileJson();   // ADR-0044
        if (!snapshotOut.empty()) {
            std::string err;
            if (writeCanonicalFile(snapshotOut, o->world->snapshot(), &err)) r["snapshotFile"] = snapshotOut;
            else r["snapshotError"] = err;
        }
    }
    if (!hashOut.empty()) { std::ofstream out(hashOut, std::ios::binary); for (const auto& h : hashesJson(rr)) out << h.dump() << '\n'; r["hashesFile"] = hashOut; }
    r["videoDriver"] = "none";
    r["fpFlagsHash"] = AKEIR_FP_FLAGS_HASH;
    {
        // run handle (§46.2: run.start / run.status 쌍). serve 안에서는 registry 에 남아 `akeir run status <id>` 로 다시 조회된다
        std::string runId = Id::generate("run").str();
        r["run"] = runId;
        r["state"] = "finished";
        if (ctx.runRegistry) { Json entry = r; entry["command"] = "run.start"; (*ctx.runRegistry)[runId] = entry; }
    }
    Envelope env = Envelope::success("run.start", r);
    if (rr.hashes.size() > 64 && hashOut.empty()) { env.withMeta("truncated", true); env.withMeta("truncatedFields", Json::array({"hashes"})); env.withMeta("hint", "per-tick hashes omitted (> 64); pass --hash-out <file> to write them all"); }
    return env;
}

Envelope cmdRunStatus(Context& ctx) {
    // akeir run status [run_id]  — serve 의 registry 에서. one-shot 에서는 기록이 없다
    std::string id = ctx.args.positional(2, "");
    if (!ctx.runRegistry) return Envelope::failure("run.status", CommandError::make(ErrorCategory::Precondition, "RUN_STATUS_REQUIRES_SERVE",
        "Run handles live in the resident process; start `akeir serve` and run there.", Json{{"hint", "akeir serve"}}));
    if (id.empty()) {
        Json arr = Json::array();
        for (const auto& [k, v] : ctx.runRegistry->items()) arr.push_back(Json{{"run", k}, {"state", v.value("state", "")}, {"ticksRun", v.value("ticksRun", 0)}, {"finalHash", v.value("finalHash", "")}});
        return Envelope::success("run.status", Json{{"runs", arr}});
    }
    if (!ctx.runRegistry->contains(id)) return Envelope::failure("run.status", CommandError::make(ErrorCategory::NotFound, "RUN_UNKNOWN_OR_EXPIRED", "No run " + id + " in this serve session.", Json{{"run", id}}));
    return Envelope::success("run.status", (*ctx.runRegistry)[id]);
}

Envelope cmdDump(Context& ctx) {
    // akeir dump <selector> [--ticks N]  — world 를 build 하고 (선택) N tick 돌린 뒤 entity 를 dump (§25)
    Envelope fail;
    auto o = openWorld(ctx, "dump", fail, 0, false);
    if (!o) return fail;
    std::string sel = ctx.args.positional(1, "");
    if (sel == "world") {
        if (!ctx.args.has("all")) return Envelope::failure("dump", CommandError::make(ErrorCategory::Usage, "OUTPUT_TOO_LARGE", "Dumping the whole world needs --all (or use `akeir snapshot --out file`).", Json{{"entities", o->world->entityIds().size()}}));
        if (auto t = ctx.args.getInt("ticks"); t && *t > 0) { RunConfig cfg; cfg.ticks = *t; cfg.seed = o->world->seed(); cfg.hashEvery = 0; NullInputSource in; Application::runHeadless(cfg, *o->world, in); }
        return Envelope::success("dump", o->world->snapshot());
    }
    auto ids = o->project->resolveSelector(sel);
    std::vector<std::string> ents;
    for (const auto& id : ids) if (o->world->hasEntity(id)) ents.push_back(id);
    if (ents.empty()) return Envelope::failure("dump", CommandError::make(ErrorCategory::NotFound, "ENTITY_NOT_FOUND", "No entity in this world matches '" + sel + "'.", Json{{"selector", sel}}));
    if (ents.size() > 1) return Envelope::failure("dump", CommandError::make(ErrorCategory::Usage, "AMBIGUOUS_SELECTOR", "Selector matches several entities.", Json{{"candidates", ents}}));
    if (auto t = ctx.args.getInt("ticks"); t && *t > 0) { RunConfig cfg; cfg.ticks = *t; cfg.seed = o->world->seed(); cfg.hashEvery = 0; NullInputSource in; Application::runHeadless(cfg, *o->world, in); }
    Json d = o->world->dumpEntity(ents.front());
    d["path"] = o->project->entityPath(ents.front()).value_or("");
    d["tick"] = o->world->currentTick();
    return Envelope::success("dump", d);
}

Envelope cmdQuery(Context& ctx) {
    // akeir query --with A,B --without C [--ticks N] [--fields id,name,path] (§16 구조화 형태)
    Envelope fail;
    auto o = openWorld(ctx, "query", fail, 0, false);
    if (!o) return fail;
    auto split = [](const std::string& s) { std::vector<std::string> out; std::string cur; for (char c : s) { if (c == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); } else cur.push_back(c); } if (!cur.empty()) out.push_back(cur); return out; };
    auto with = split(ctx.args.getOr("with", ""));
    auto without = split(ctx.args.getOr("without", ""));
    if (auto t = ctx.args.getInt("ticks"); t && *t > 0) { RunConfig cfg; cfg.ticks = *t; cfg.seed = o->world->seed(); cfg.hashEvery = 0; NullInputSource in; Application::runHeadless(cfg, *o->world, in); }
    auto ids = o->world->query(with, without);
    long long limit = ctx.args.getInt("limit").value_or(50);
    Json rows = Json::array();
    for (const auto& id : ids) {
        if (static_cast<long long>(rows.size()) >= limit) break;
        Json row = Json{{"id", id}, {"name", o->world->name(id)}, {"path", o->project->entityPath(id).value_or("")}, {"tags", o->world->tags(id)}};
        if (ctx.args.has("components")) row["components"] = o->world->dumpEntity(id)["components"];
        rows.push_back(row);
    }
    Envelope env = Envelope::success("query", Json{{"rows", rows}, {"total", ids.size()}, {"with", with}, {"without", without}, {"tick", o->world->currentTick()}});
    if (static_cast<long long>(ids.size()) > limit) { env.withMeta("truncated", true); env.withMeta("hint", "raise limit (CLI: --limit) to see all rows; there is no cursor yet"); }
    return env;
}

} // namespace

void registerRunCommands(std::vector<CommandSpec>& table) {
    table.push_back({"run.start", {"run"}, "RuntimeControl", "Run the simulation (headless or windowed)",
        "--headless: builds the project world (or --demo), runs N fixed ticks (§20.1), returns hashes (§22.2); --replay inputs.jsonl plays recorded input; --snapshot-out writes §26.1 snapshot; --hash-out writes hashes.jsonl; --profile adds per-system/physics/query timings and entity counts (ADR-0044). "
        "Without --headless (SDL build): opens a window, keyboard via Config/input.json, fixed tick + accumulator; --record inputs.jsonl saves the InputFrames for headless replay.",
        "akeir run --headless [--ticks N] [--seed S] [--world ID] [--hash-every K] [--hash-out f] [--snapshot-out f] [--replay f] [--profile] [--demo] [--json]  |  akeir run [--ticks N] [--record f] [--width W --height H]", false, false, false, cmdRun});
    table.push_back({"run.status", {"run", "status"}, "Query", "Status/result of a run handle (§46.2)", "Lists or returns runs started in this `akeir serve` session (run.start returns result.run).", "akeir run status [run_id]", true, false, true, cmdRunStatus});
    table.push_back({"dump", {"dump"}, "RuntimeControl", "Dump an entity's runtime state (§25)",
        "Builds the world, optionally advances --ticks N, and dumps the entity (runtimeOnly values included). `akeir dump world --all` returns the full snapshot.",
        "akeir dump <id|name:X|path:A/B|world --all> [--ticks N] [--json]", true, false, true, cmdDump});
    table.push_back({"query", {"query"}, "Query", "Query entities (§16)",
        "Structured query: --with A,B --without C. Prefix # for tags (#enemy). Optionally advance --ticks N first. --components includes dumps.",
        "akeir query --with EnemyAI,Transform --without Collider2D [--ticks N] [--components] [--limit N] [--json]", true, false, true, cmdQuery});
}

} // namespace akeir::cli
