// Tools/CLI/ResidentRunCommands.cpp — `akeir run open|step|inspect|query|snapshot|close|list` (ADR-0041).
// Only inside a resident process (`akeir serve`, `akeir mcp`): the one-shot CLI has nowhere to keep the world.
#include "Commands.h"
#include "GameSystems.h"
#include "ResidentRun.h"
#include "akeir/core/ExitCodes.h"
#include "akeir/core/Hash.h"
#include "akeir/core/Id.h"
#include "akeir/core/Log.h"
#include "akeir/runtime/Components.h"
#include "akeir/runtime/Input.h"
#include "akeir/serialization/Canonical.h"

#include <algorithm>

namespace akeir::cli {

namespace {

Envelope requiresServe(const char* cmd) {
    return Envelope::failure(cmd, CommandError::make(ErrorCategory::Precondition, "RUN_REQUIRES_SERVE",
        "Resident runs live in `akeir serve` / the MCP server; this one-shot process cannot keep a world alive. Start `akeir serve` (then every command is forwarded) or use the MCP `play` tool.", Json{{"hint", "akeir serve"}}));
}

ResidentRun* findRun(Context& ctx, const char* cmd, const std::string& id, Envelope& fail) {
    if (!ctx.residentRuns) { fail = requiresServe(cmd); return nullptr; }
    if (id.empty()) { fail = Envelope::failure(cmd, CommandError::make(ErrorCategory::Usage, "ARG_REQUIRED", std::string(cmd) + " needs a run id (from `run open`; `run list` shows the open ones).")); return nullptr; }
    auto it = ctx.residentRuns->find(id);
    if (it == ctx.residentRuns->end()) {
        Json open = Json::array(); for (const auto& [k, v] : *ctx.residentRuns) open.push_back(k);
        fail = Envelope::failure(cmd, CommandError::make(ErrorCategory::NotFound, "RUN_UNKNOWN_OR_EXPIRED", "No open run " + id + ".", Json{{"run", id}, {"open", open}}));
        return nullptr;
    }
    return it->second.get();
}

Json runSummary(const ResidentRun& r) {
    return Json{{"run", r.id}, {"world", r.worldId}, {"seed", r.seed}, {"tick", r.ticksRun}, {"entities", r.world->entityIds().size()}, {"hash", toHex64(r.world->hash())}, {"openedAt", r.openedAt}};
}

std::vector<std::string> resolveInRun(ResidentRun& r, const std::string& sel) {
    std::vector<std::string> out;
    if (r.world->hasEntity(sel)) { out.push_back(sel); return out; }           // runtime-spawned ids are not in the authoring index
    for (const auto& id : r.project->resolveSelector(sel)) if (r.world->hasEntity(id)) out.push_back(id);
    if (out.empty()) {                                                          // bare name against live entities (incl. spawned)
        std::string want = sel.rfind("name:", 0) == 0 ? sel.substr(5) : sel;
        for (const auto& id : r.world->entityIds()) if (r.world->name(id) == want) out.push_back(id);
    }
    return out;
}

Envelope cmdRunOpen(Context& ctx) {
    if (!ctx.residentRuns) return requiresServe("run.open");
    if (!ctx.resident) return Envelope::failure("run.open", CommandError::make(ErrorCategory::NotFound, "PROJECT_NOT_FOUND", "No resident project."));
    registerBuiltinComponents();
    game::registerGameComponents();
    auto run = std::make_unique<ResidentRun>();
    run->project = *ctx.resident;   // snapshot of the authoring model at open time
    std::string sel = ctx.args.getOr("world", "");
    if (sel.empty()) { auto dw = run->project->defaultWorld(); if (!dw) return Envelope::failure("run.open", CommandError::make(ErrorCategory::NotFound, "WORLD_NOT_FOUND", "Project has no worlds.")); run->worldId = *dw; }
    else {
        std::vector<std::string> worlds;
        for (const auto& id : run->project->resolveSelector(sel)) if (auto l = run->project->locate(id); l && l->kind == "world") worlds.push_back(id);
        if (worlds.size() != 1) return Envelope::failure("run.open", CommandError::make(ErrorCategory::NotFound, "WORLD_NOT_FOUND", "--world '" + sel + "' does not match exactly one world.", Json{{"candidates", worlds}}));
        run->worldId = worlds.front();
    }
    PlayWorldConfig cfg;
    auto seed = ctx.args.getInt("seed");
    cfg.seed = seed ? static_cast<std::uint64_t>(*seed) : run->project->seed();
    cfg.tickRate = run->project->tickRate();
    std::vector<Diagnostic> bd;
    run->world = PlayWorld::build(*run->project, run->worldId, cfg, bd);
    if (!run->world) {
        Json arr = Json::array(); for (auto& d : bd) arr.push_back(d.toJson());
        return Envelope::failure("run.open", CommandError::make(ErrorCategory::Validation, "WORLD_BUILD_FAILED", "The world could not be built. Run `akeir validate`.", Json{{"diagnostics", arr}}));
    }
    game::registerGameSystems(*run->world);
    run->seed = cfg.seed;
    run->sim.tickRate = cfg.tickRate;
    run->id = Id::generate("run").str();
    run->openedAt = WallTime::now().iso8601();
    Json r = runSummary(*run);
    r["systems"] = run->world->systemNames();
    r["tickRate"] = cfg.tickRate;
    AKEIR_LOG(Info, "runtime", "run_open", "Resident run opened.", Json{{"run", run->id}, {"game.world", run->worldId}, {"game.seed", cfg.seed}});
    (*ctx.residentRuns)[run->id] = std::move(run);
    return Envelope::success("run.open", r);
}

Envelope cmdRunStep(Context& ctx) {
    Envelope fail;
    ResidentRun* run = findRun(ctx, "run.step", ctx.args.positional(2, ""), fail);
    if (!run) return fail;
    long long ticks = ctx.args.getInt("ticks").value_or(1);
    if (ticks < 0 || ticks > 1000000) return Envelope::failure("run.step", CommandError::make(ErrorCategory::Usage, "USAGE_ERROR", "--ticks must be 0..1000000."));
    InputFrame frame;
    if (auto in = ctx.args.get("input")) {
        std::string err;
        auto j = parseJson(*in, &err);
        if (!j || !j->is_object()) return Envelope::failure("run.step", CommandError::make(ErrorCategory::Usage, "ARG_TYPE", "--input must be a JSON object {action: value}, e.g. {\"MoveX\": 1}: " + err));
        for (const auto& [k, v] : j->items()) { if (!v.is_number()) return Envelope::failure("run.step", CommandError::make(ErrorCategory::Usage, "ARG_TYPE", "input action '" + k + "' must be a number.")); frame.actions[k] = v.get<float>(); }
    }
    const std::uint64_t before = run->world->hash();
    for (long long i = 0; i < ticks; ++i) {
        frame.tick = run->sim.tick;
        run->world->tick(frame, run->sim);
        run->sim.advance();
        ++run->ticksRun;
    }
    Json r = runSummary(*run);
    r["stepped"] = ticks;
    r["hashBefore"] = toHex64(before);
    r["contactEventsLastTick"] = run->world->contactEvents().size();
    if (ctx.args.has("profile")) { r["profile"] = run->world->profileJson(); run->world->resetProfile(); }   // ADR-0044: profile of the ticks since the last profiled step
    return Envelope::success("run.step", r);
}

Envelope cmdRunInspect(Context& ctx) {
    Envelope fail;
    ResidentRun* run = findRun(ctx, "run.inspect", ctx.args.positional(2, ""), fail);
    if (!run) return fail;
    std::string sel = ctx.args.positional(3, "");
    if (sel.empty()) return Envelope::failure("run.inspect", CommandError::make(ErrorCategory::Usage, "ARG_REQUIRED", "akeir run inspect <run> <id|name|name:X|path:A/B>"));
    auto ids = resolveInRun(*run, sel);
    if (ids.size() != 1) return Envelope::failure("run.inspect", CommandError::make(ids.empty() ? ErrorCategory::NotFound : ErrorCategory::Usage, ids.empty() ? "ENTITY_NOT_FOUND" : "AMBIGUOUS_SELECTOR", "'" + sel + "' matches " + std::to_string(ids.size()) + " live entities.", Json{{"candidates", ids}}));
    Json d = run->world->dumpEntity(ids.front());
    d["run"] = run->id;
    d["tick"] = run->ticksRun;
    return Envelope::success("run.inspect", d);
}

Envelope cmdRunQuery(Context& ctx) {
    Envelope fail;
    ResidentRun* run = findRun(ctx, "run.query", ctx.args.positional(2, ""), fail);
    if (!run) return fail;
    auto split = [](const std::string& s) { std::vector<std::string> out; std::string cur; for (char c : s) { if (c == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); } else cur += c; } if (!cur.empty()) out.push_back(cur); return out; };
    std::vector<std::string> with = split(ctx.args.getOr("with", "")), without = split(ctx.args.getOr("without", ""));
    auto ids = run->world->query(with, without);
    long long limit = ctx.args.getInt("limit").value_or(100);
    Json rows = Json::array();
    for (const auto& id : ids) {
        if (static_cast<long long>(rows.size()) >= limit) break;
        Json row = Json{{"id", id}, {"name", run->world->name(id)}, {"tags", run->world->tags(id)}};
        if (ctx.args.has("components")) row["components"] = run->world->dumpEntity(id)["components"];
        rows.push_back(std::move(row));
    }
    return Envelope::success("run.query", Json{{"run", run->id}, {"tick", run->ticksRun}, {"rows", rows}, {"total", ids.size()}, {"with", with}, {"without", without}});
}

Envelope cmdRunSnapshot(Context& ctx) {
    Envelope fail;
    ResidentRun* run = findRun(ctx, "run.snapshot", ctx.args.positional(2, ""), fail);
    if (!run) return fail;
    Json snap = run->world->snapshot();
    if (auto out = ctx.args.get("out")) {
        std::string err;
        if (!writeCanonicalFile(*out, snap, &err)) return Envelope::failure("run.snapshot", CommandError::make(ErrorCategory::Internal, "SAVE_FAILED", "Cannot write " + *out + ": " + err));
        return Envelope::success("run.snapshot", Json{{"run", run->id}, {"tick", run->ticksRun}, {"file", *out}, {"hash", toHex64(run->world->hash())}, {"entities", run->world->entityIds().size()}});
    }
    return Envelope::success("run.snapshot", Json{{"run", run->id}, {"tick", run->ticksRun}, {"snapshot", snap}});
}

Envelope cmdRunClose(Context& ctx) {
    Envelope fail;
    ResidentRun* run = findRun(ctx, "run.close", ctx.args.positional(2, ""), fail);
    if (!run) return fail;
    Json r = runSummary(*run);
    r["state"] = "closed";
    std::string id = run->id;
    ctx.residentRuns->erase(id);
    AKEIR_LOG(Info, "runtime", "run_close", "Resident run closed.", Json{{"run", id}});
    return Envelope::success("run.close", r);
}

Envelope cmdRunList(Context& ctx) {
    if (!ctx.residentRuns) return requiresServe("run.list");
    Json arr = Json::array();
    for (const auto& [k, v] : *ctx.residentRuns) arr.push_back(runSummary(*v));
    return Envelope::success("run.list", Json{{"runs", arr}});
}

} // namespace

void registerResidentRunCommands(std::vector<CommandSpec>& t) {
    t.push_back({"run.open", {"run", "open"}, "RuntimeControl", "Open a resident play world (ADR-0041)", "Builds the world once inside `akeir serve` / the MCP server and keeps it alive; returns a run id for run step/inspect/query/snapshot/close. Requires the resident process (RUN_REQUIRES_SERVE otherwise).", "akeir run open [--world W] [--seed S] [--json]", false, false, false, cmdRunOpen});
    t.push_back({"run.step", {"run", "step"}, "RuntimeControl", "Advance a resident run", "Simulates --ticks N (default 1) with the same scripted input on every tick (--input '{\"MoveX\":1,\"Attack\":1}' — action names from Config/input.json). Returns tick, hash, contact event count.", "akeir run step <run> [--ticks N] [--input '{\"MoveX\":1}'] [--profile] [--json]", false, false, false, cmdRunStep});
    t.push_back({"run.inspect", {"run", "inspect"}, "Query", "Dump a live entity of a resident run", "Runtime state (runtimeOnly values included) of one entity at the current tick. Selector: id (also runtime-spawned), name, name:X, path:A/B.", "akeir run inspect <run> <selector> [--json]", true, false, true, cmdRunInspect});
    t.push_back({"run.query", {"run", "query"}, "Query", "Query live entities of a resident run", "--with A,#tag --without B over the live world (runtime-spawned entities included); --components adds dumps.", "akeir run query <run> [--with A,B] [--without C] [--components] [--limit N] [--json]", true, false, true, cmdRunQuery});
    t.push_back({"run.snapshot", {"run", "snapshot"}, "Query", "Snapshot of a resident run (§26.1)", "The full §26.1 snapshot of the live world; --out writes it canonically instead of returning it.", "akeir run snapshot <run> [--out f.json] [--json]", true, false, true, cmdRunSnapshot});
    t.push_back({"run.close", {"run", "close"}, "RuntimeControl", "Close a resident run", "Frees the world. Open runs also die with the resident process.", "akeir run close <run> [--json]", false, false, false, cmdRunClose});
    t.push_back({"run.list", {"run", "list"}, "Query", "List resident runs", "Open runs of this resident process with tick and hash.", "akeir run list [--json]", true, false, true, cmdRunList});
}

} // namespace akeir::cli
