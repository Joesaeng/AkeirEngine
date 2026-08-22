// Tools/CLI/MutationCommands.cpp — 쓰기 명령. 전부 akeir::CommandBus 를 거친다 (§8 단일 Command API, §11 CLI).
// 설계 문서 §8 (entity/component/property 명령), §9 (commit), §10 (undo/redo/history), §49 (apply), §50 (--dry-run), §78 (envelope.changes = ChangeSet ops)
//
//   akeir entity create <name> [--world W] [--parent P] [--prefab X] [--components JSON] [--set JSON] [--tags a,b]
//   akeir entity delete <selector> [--no-recursive]
//   akeir entity rename <selector> <name>
//   akeir entity reparent <selector> <parent|root> [--order K]
//   akeir component add <selector> <Component> [--value JSON]
//   akeir component remove <selector> <Component>
//   akeir set <selector> <Component>.<prop[/sub]> <value>          value 는 JSON 으로 파싱, 실패하면 문자열
//   akeir tag add|remove <selector> <tag>
//   akeir prefab create <name> [--components JSON] [--base X] [--tags a,b]
//   akeir prefab instantiate <prefab> [--world W] [--name N] [--parent P] [--position x,y,z] [--set JSON]
//   akeir world create <name>
//   akeir apply <file.json | -> [--dry-run] [--idempotency-key K]          §49 batch
//   akeir undo [N] [--actor A] / akeir redo [N] / akeir history [--limit N]
//   akeir cmd <command.id> --args '{json}'                                  임의 command (capabilities.commands[] 참조)
//
//   공통 옵션: --dry-run (§50: fork 에서 실행, 파일 안 건드림, changes 는 돌려줌), --no-validate (commit 전 문서 검증 생략), --actor <id> (history 태깅, 기본 "cli"), --json
//   출력: §12 envelope. ok 면 result + changes[] (ChangeSet ops) + meta.changeSet. 실패면 error (category/ruleId/details).
#include "Commands.h"
#include "akeir/commands/CommandBus.h"
#include "akeir/core/ExitCodes.h"
#include "akeir/runtime/Components.h"
#include "akeir/serialization/Canonical.h"
#include "GameSystems.h"

#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

namespace akeir::cli {

namespace {

/// 프로젝트 + bus 를 연다. Game/ 의 component 도 등록 (CLI 는 샘플 게임을 정적으로 링크한다 — STATUS 기술 부채 참조)
struct Opened {
    std::optional<Project> ownedPrj;
    std::unique_ptr<CommandBus> ownedBus;
    Project* prj = nullptr;
    CommandBus* bus = nullptr;
};

bool open(Context& ctx, Envelope& fail, const std::string& command, Opened& out) {
    game::registerGameComponents();
    if (ctx.residentBus) {   // serve: 단일 writer (§88.1)
        out.prj = ctx.resident;
        out.bus = ctx.residentBus;
        return true;
    }
    if (!ctx.args.getOr("tx", "").empty()) {
        fail = Envelope::failure(command, CommandError::make(ErrorCategory::Precondition, "TX_REQUIRES_SERVE",
            "Multi-call transactions need the resident process: start `akeir serve` in another terminal, then `akeir tx begin`.", Json{{"hint", "akeir serve"}}));
        return false;
    }
    out.ownedPrj = openProject(ctx, fail, command);
    if (!out.ownedPrj) return false;
    out.prj = &*out.ownedPrj;
    BusOptions o;
    o.actor = ctx.args.get("actor").value_or("cli");
    out.ownedBus = std::make_unique<CommandBus>(*out.prj, o);
    out.bus = out.ownedBus.get();
    // §9.2: 이전 프로세스가 commit 도중 죽었으면 journal 이 남아 있다 → 먼저 복구
    Json recovered = out.bus->recoverJournal();
    if (!recovered.empty()) ctx.loadDiagnostics.push_back(Diagnostic::note("JOURNAL_RECOVERED", "Recovered " + std::to_string(recovered.size()) + " pending ChangeSet(s) from Cache/journal."));
    return true;
}

ExecOptions execOptions(Context& ctx) {
    ExecOptions o;
    o.dryRun = ctx.args.has("dry-run");
    o.validateAfter = !ctx.args.has("no-validate");
    o.idempotencyKey = ctx.args.get("idempotency-key").value_or("");
    o.tx = ctx.args.getOr("tx", "");
    return o;
}

Envelope withLoadWarnings(Envelope env, const Context& ctx) {
    for (const auto& d : ctx.loadDiagnostics) env.withWarning(d);
    return env;
}

/// "--components '{...}'" 류 JSON 옵션. 파싱 실패 → nullopt + fail
std::optional<Json> jsonOption(Context& ctx, const char* key, Envelope& fail, const std::string& command) {
    auto v = ctx.args.get(key);
    if (!v) return Json();
    std::string err;
    auto j = parseJson(*v, &err);
    if (!j) {
        fail = Envelope::failure(command, CommandError::make(ErrorCategory::Usage, "USAGE_ERROR", std::string("--") + key + " must be JSON: " + err, Json{{"option", key}, {"value", *v}}));
        return std::nullopt;
    }
    return *j;
}

/// CLI 값 → JSON (숫자/불리언/null/배열/객체는 JSON 으로, 아니면 문자열)
Json literal(const std::string& s) {
    if (auto j = parseJson(s)) return *j;
    return s;
}

Json splitList(const std::string& s) {
    Json arr = Json::array();
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) if (!item.empty()) arr.push_back(item);
    return arr;
}

Envelope usage(const std::string& command, const std::string& text) {
    return Envelope::failure(command, CommandError::make(ErrorCategory::Usage, "USAGE_ERROR", text));
}

// ---------------------------------------------------------------- 한 command 실행 공통

Envelope runOne(Context& ctx, const std::string& id, Json args) {
    Envelope fail;
    Opened o;
    if (!open(ctx, fail, id, o)) return fail;
    return withLoadWarnings(o.bus->execute(id, std::move(args), execOptions(ctx)), ctx);
}

// ---------------------------------------------------------------- entity.*

Envelope cmdEntityCreate(Context& ctx) {
    const std::string name = ctx.args.positional(2);
    if (name.empty()) return usage("entity.create", "akeir entity create <name> [--world W] [--parent P] [--prefab X] [--components JSON] [--set JSON] [--tags a,b]");
    Envelope fail;
    Json a = Json{{"name", name}};
    if (auto v = ctx.args.get("world")) a["world"] = *v;
    if (auto v = ctx.args.get("parent")) a["parent"] = *v;
    if (auto v = ctx.args.get("prefab")) a["prefab"] = *v;
    if (auto v = ctx.args.get("order")) a["order"] = *v;
    if (auto v = ctx.args.get("tags")) a["tags"] = splitList(*v);
    auto comps = jsonOption(ctx, "components", fail, "entity.create"); if (!comps) return fail;
    if (!comps->is_null()) a["components"] = *comps;
    auto set = jsonOption(ctx, "set", fail, "entity.create"); if (!set) return fail;
    if (!set->is_null()) a["set"] = *set;
    return runOne(ctx, "entity.create", a);
}

Envelope cmdEntityDelete(Context& ctx) {
    const std::string sel = ctx.args.positional(2);
    if (sel.empty()) return usage("entity.delete", "akeir entity delete <selector> [--no-recursive]");
    return runOne(ctx, "entity.delete", Json{{"entity", sel}, {"recursive", !ctx.args.has("no-recursive")}});
}

Envelope cmdEntityRename(Context& ctx) {
    const std::string sel = ctx.args.positional(2), name = ctx.args.positional(3);
    if (sel.empty() || name.empty()) return usage("entity.rename", "akeir entity rename <selector> <name>");
    return runOne(ctx, "entity.rename", Json{{"entity", sel}, {"name", name}});
}

Envelope cmdEntityReparent(Context& ctx) {
    const std::string sel = ctx.args.positional(2), parent = ctx.args.positional(3);
    if (sel.empty() || parent.empty()) return usage("entity.reparent", "akeir entity reparent <selector> <parent|root> [--order K]");
    Json a = Json{{"entity", sel}, {"parent", parent == "root" || parent == "null" ? Json() : Json(parent)}};
    if (auto v = ctx.args.get("order")) a["order"] = *v;
    return runOne(ctx, "entity.reparent", a);
}

// ---------------------------------------------------------------- component.* / set / tag

Envelope cmdComponentAdd(Context& ctx) {
    const std::string sel = ctx.args.positional(2), comp = ctx.args.positional(3);
    if (sel.empty() || comp.empty()) return usage("component.add", "akeir component add <selector> <Component> [--value JSON]");
    Envelope fail;
    Json a = Json{{"entity", sel}, {"component", comp}};
    auto v = jsonOption(ctx, "value", fail, "component.add"); if (!v) return fail;
    if (!v->is_null()) a["value"] = *v;
    return runOne(ctx, "component.add", a);
}

Envelope cmdComponentRemove(Context& ctx) {
    const std::string sel = ctx.args.positional(2), comp = ctx.args.positional(3);
    if (sel.empty() || comp.empty()) return usage("component.remove", "akeir component remove <selector> <Component>");
    return runOne(ctx, "component.remove", Json{{"entity", sel}, {"component", comp}});
}

Envelope cmdSet(Context& ctx) {
    // akeir set <selector> <Component>.<path> <value>
    const std::string sel = ctx.args.positional(1), target = ctx.args.positional(2);
    if (sel.empty() || target.empty() || ctx.args.positionals.size() < 4)
        return usage("property.set", "akeir set <selector> <Component>.<prop[/sub]> <value>   e.g. akeir set name:Goblin_01 Health.max 45");
    auto dot = target.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= target.size())
        return usage("property.set", "Target must be <Component>.<prop>, e.g. Transform.position/0");
    std::string comp = target.substr(0, dot), path = target.substr(dot + 1);
    // 나머지 positional 을 합친다 (값에 공백이 있는 JSON 배열 등)
    std::string raw;
    for (std::size_t i = 3; i < ctx.args.positionals.size(); ++i) raw += (i > 3 ? " " : "") + ctx.args.positionals[i];
    return runOne(ctx, "property.set", Json{{"entity", sel}, {"component", comp}, {"path", path}, {"value", literal(raw)}});
}

Envelope cmdTag(Context& ctx, bool add) {
    const std::string sel = ctx.args.positional(2), tag = ctx.args.positional(3);
    const std::string id = add ? "tag.add" : "tag.remove";
    if (sel.empty() || tag.empty()) return usage(id, "akeir tag " + std::string(add ? "add" : "remove") + " <selector> <tag>");
    return runOne(ctx, id, Json{{"entity", sel}, {"tag", tag}});
}

// ---------------------------------------------------------------- prefab.* / world.create

Envelope cmdPrefabCreate(Context& ctx) {
    const std::string name = ctx.args.positional(2);
    if (name.empty()) return usage("prefab.create", "akeir prefab create <name> [--components JSON] [--base X] [--set JSON] [--tags a,b]");
    Envelope fail;
    Json a = Json{{"name", name}};
    if (auto v = ctx.args.get("base")) a["base"] = *v;
    if (auto v = ctx.args.get("tags")) a["tags"] = splitList(*v);
    auto comps = jsonOption(ctx, "components", fail, "prefab.create"); if (!comps) return fail;
    if (!comps->is_null()) a["components"] = *comps;
    auto set = jsonOption(ctx, "set", fail, "prefab.create"); if (!set) return fail;
    if (!set->is_null()) a["set"] = *set;
    return runOne(ctx, "prefab.create", a);
}

Envelope cmdPrefabInstantiate(Context& ctx) {
    const std::string prefab = ctx.args.positional(2);
    if (prefab.empty()) return usage("prefab.instantiate", "akeir prefab instantiate <prefab> [--world W] [--name N] [--parent P] [--position x,y,z] [--set JSON] [--tags a,b]");
    Envelope fail;
    Json a = Json{{"prefab", prefab}};
    if (auto v = ctx.args.get("world")) a["world"] = *v;
    if (auto v = ctx.args.get("name")) a["name"] = *v;
    if (auto v = ctx.args.get("parent")) a["parent"] = *v;
    if (auto v = ctx.args.get("tags")) a["tags"] = splitList(*v);
    if (auto v = ctx.args.get("position")) {
        Json pos = Json::array();
        for (const auto& s : splitList(*v)) pos.push_back(literal(s.get<std::string>()));
        while (pos.size() < 3) pos.push_back(0);
        a["position"] = pos;
    }
    auto set = jsonOption(ctx, "set", fail, "prefab.instantiate"); if (!set) return fail;
    if (!set->is_null()) a["set"] = *set;
    return runOne(ctx, "prefab.instantiate", a);
}

Envelope cmdAssetImport(Context& ctx) {
    const std::string source = ctx.args.positional(2);
    if (source.empty()) return usage("asset.import", "akeir asset import <Assets/…/file.png> [--grid WxH --names a,b,c] [--ppu 16] [--filter nearest|linear] [--pivot 0.5,0.5]");
    Json a = Json{{"source", source}};
    if (auto g = ctx.args.get("grid")) {
        auto x = g->find('x');
        int cw = x == std::string::npos ? 0 : std::atoi(g->substr(0, x).c_str()), ch = x == std::string::npos ? 0 : std::atoi(g->substr(x + 1).c_str());
        if (cw <= 0 || ch <= 0) return Envelope::failure("asset.import", CommandError::make(ErrorCategory::Usage, "USAGE_ERROR", "--grid must look like 16x16 (cell width x height in pixels)."));
        a["grid"] = Json{{"cellWidth", cw}, {"cellHeight", ch}};
    }
    if (auto n = ctx.args.get("names")) a["names"] = splitList(*n);
    if (auto p = ctx.args.getInt("ppu")) a["pixelsPerUnit"] = *p;
    if (auto f = ctx.args.get("filter")) a["filter"] = *f;
    if (auto pv = ctx.args.get("pivot")) { Json parts = splitList(*pv); if (parts.size() == 2) a["pivot"] = Json::array({std::atof(parts[0].get<std::string>().c_str()), std::atof(parts[1].get<std::string>().c_str())}); }
    return runOne(ctx, "asset.import", a);
}

Envelope cmdWorldCreate(Context& ctx) {
    const std::string name = ctx.args.positional(2);
    if (name.empty()) return usage("world.create", "akeir world create <name>");
    return runOne(ctx, "world.create", Json{{"name", name}});
}

// ---------------------------------------------------------------- apply / undo / redo / history / cmd

Envelope cmdApply(Context& ctx) {
    const std::string src = ctx.args.positional(1);
    if (src.empty()) return usage("apply", "akeir apply <batch.json | -> [--dry-run] [--idempotency-key K]   (stdin with '-')");
    std::string text;
    if (src == "-") text.assign((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
    else {
        std::ifstream in(src, std::ios::binary);
        if (!in) return Envelope::failure("apply", CommandError::make(ErrorCategory::NotFound, "FILE_NOT_FOUND", "Cannot read " + src, Json{{"path", src}}));
        text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }
    std::string err;
    auto batch = parseJson(text, &err);
    if (!batch) return Envelope::failure("apply", CommandError::make(ErrorCategory::Usage, "APPLY_INVALID", "Batch is not valid JSON: " + err));
    // 배열만 주어지면 {changes: [...]} 로 감싼다
    if (batch->is_array()) *batch = Json{{"changes", *batch}};
    Envelope fail;
    Opened o;
    if (!open(ctx, fail, "apply", o)) return fail;
    return withLoadWarnings(o.bus->apply(*batch, execOptions(ctx)), ctx);
}

Envelope cmdUndo(Context& ctx) {
    int n = 1;
    if (auto p = ctx.args.positional(1); !p.empty()) n = std::max(1, std::atoi(p.c_str()));
    Envelope fail;
    Opened o;
    if (!open(ctx, fail, "history.undo", o)) return fail;
    std::string actor = ctx.args.get("actor").value_or("");
    // 기본: actor 제한 없음. --actor 를 주면 그 actor 의 항목만 undo (§10 actor 태깅)
    return withLoadWarnings(o.bus->undo(n, actor == "any" ? "" : actor), ctx);
}

Envelope cmdRedo(Context& ctx) {
    int n = 1;
    if (auto p = ctx.args.positional(1); !p.empty()) n = std::max(1, std::atoi(p.c_str()));
    Envelope fail;
    Opened o;
    if (!open(ctx, fail, "history.redo", o)) return fail;
    return withLoadWarnings(o.bus->redo(n), ctx);
}

Envelope cmdHistory(Context& ctx) {
    Envelope fail;
    Opened o;
    if (!open(ctx, fail, "history.list", o)) return fail;
    std::size_t limit = 20;
    if (auto v = ctx.args.get("limit")) limit = static_cast<std::size_t>(std::max(1, std::atoi(v->c_str())));
    Json r = o.bus->historyJson(limit);
    if (auto id = ctx.args.positional(1); !id.empty()) {
        const ChangeSet* cs = o.bus->history().find(id);
        if (!cs) return Envelope::failure("history.list", CommandError::make(ErrorCategory::NotFound, "CHANGESET_NOT_FOUND", "No ChangeSet " + id + " in history.", Json{{"id", id}}));
        r = cs->toJson();
    }
    return withLoadWarnings(Envelope::success("history.list", r), ctx);
}

// ---------------------------------------------------------------- tx (§9.1) — serve 에서만 의미가 있다

Envelope requireServe(const std::string& id) {
    return Envelope::failure(id, CommandError::make(ErrorCategory::Precondition, "TX_REQUIRES_SERVE",
        "Transactions spanning CLI calls need `akeir serve` running for this project (then this command is forwarded automatically).", Json{{"hint", "akeir serve"}}));
}

Envelope cmdTxBegin(Context& ctx) {
    if (!ctx.residentBus) return requireServe("tx.begin");
    long long ttl = ctx.args.getInt("ttl").value_or(600000);
    std::string tx = ctx.residentBus->beginTx(ttl);
    return Envelope::success("tx.begin", ctx.residentBus->txInfo(tx));
}

Envelope cmdTxCommit(Context& ctx) {
    if (!ctx.residentBus) return requireServe("tx.commit");
    std::string tx = ctx.args.positional(2, ctx.args.getOr("tx", ""));
    if (tx.empty()) return usage("tx.commit", "akeir tx commit <tx_id>");
    return ctx.residentBus->commitTx(tx, execOptions(ctx));
}

Envelope cmdTxRollback(Context& ctx) {
    if (!ctx.residentBus) return requireServe("tx.rollback");
    std::string tx = ctx.args.positional(2, ctx.args.getOr("tx", ""));
    if (tx.empty()) return usage("tx.rollback", "akeir tx rollback <tx_id>");
    return ctx.residentBus->rollbackTx(tx);
}

Envelope cmdTxList(Context& ctx) {
    if (!ctx.residentBus) return requireServe("tx.list");
    return Envelope::success("tx.list", Json{{"transactions", ctx.residentBus->txList()}});
}

Envelope cmdGeneric(Context& ctx) {
    const std::string id = ctx.args.positional(1);
    if (id.empty()) return usage("cmd", "akeir cmd <command.id> --args '{json}'   (ids: akeir capabilities --json → result.busCommands)");
    Envelope fail;
    auto args = jsonOption(ctx, "args", fail, id); if (!args) return fail;
    if (args->is_null()) *args = Json::object();
    return runOne(ctx, id, *args);
}

} // namespace

void registerMutationCommands(std::vector<CommandSpec>& t) {
    t.push_back({"entity.create", {"entity", "create"}, "Mutation", "Create an entity", "Plain entity (components JSON, Transform auto-added) or prefab instance (--prefab). Parent/world by selector.",
                 "akeir entity create <name> [--world W] [--parent P] [--prefab X] [--components JSON] [--set JSON] [--tags a,b] [--dry-run]", false, false, false, cmdEntityCreate});
    t.push_back({"entity.delete", {"entity", "delete"}, "Mutation", "Delete an entity (+descendants)", "Removes the entity and, unless --no-recursive, all descendants. Undo restores them.",
                 "akeir entity delete <selector> [--no-recursive] [--dry-run]", false, true, true, cmdEntityDelete});
    t.push_back({"entity.rename", {"entity", "rename"}, "Mutation", "Rename an entity", "", "akeir entity rename <selector> <name>", false, false, true, cmdEntityRename});
    t.push_back({"entity.reparent", {"entity", "reparent"}, "Mutation", "Reparent an entity", "Moves under another entity or to root; rejects cycles.", "akeir entity reparent <selector> <parent|root> [--order K]", false, false, true, cmdEntityReparent});
    t.push_back({"component.add", {"component", "add"}, "Mutation", "Add a component", "Defaults merged with --value. On prefab instances this is an 'add' override.", "akeir component add <selector> <Component> [--value JSON]", false, false, false, cmdComponentAdd});
    t.push_back({"component.remove", {"component", "remove"}, "Mutation", "Remove a component", "On prefab instances this is a 'remove' override. Refuses if another component requires it.", "akeir component remove <selector> <Component>", false, true, true, cmdComponentRemove});
    t.push_back({"property.set", {"set"}, "Mutation", "Set a property", "Validates type/range/enum via reflection. On prefab instances writes a 'set' override (or clears it when equal to the prefab value).",
                 "akeir set <selector> <Component>.<prop[/sub]> <value>", false, false, true, cmdSet});
    t.push_back({"tag.add", {"tag", "add"}, "Mutation", "Add a tag", "", "akeir tag add <selector> <tag>", false, false, true, [](Context& c) { return cmdTag(c, true); }});
    t.push_back({"tag.remove", {"tag", "remove"}, "Mutation", "Remove a tag", "", "akeir tag remove <selector> <tag>", false, false, true, [](Context& c) { return cmdTag(c, false); }});
    t.push_back({"prefab.create", {"prefab", "create"}, "Mutation", "Create a prefab", "Writes Prefabs/<Name>.prefab.json.", "akeir prefab create <name> [--components JSON] [--base X] [--set JSON] [--tags a,b]", false, false, false, cmdPrefabCreate});
    t.push_back({"prefab.instantiate", {"prefab", "instantiate"}, "Mutation", "Instantiate a prefab", "Creates an instance entity referencing the prefab.", "akeir prefab instantiate <prefab> [--world W] [--name N] [--parent P] [--position x,y,z] [--set JSON]", false, false, false, cmdPrefabInstantiate});
    t.push_back({"world.create", {"world", "create"}, "Mutation", "Create a world", "Writes Worlds/<Name>.world.json with no entities.", "akeir world create <name>", false, false, false, cmdWorldCreate});
    t.push_back({"asset.import", {"asset", "import"}, "Mutation", "Import a PNG as a texture asset (§37)", "Creates Assets/<png>.meta.json with a generated asset_ id and sprite sub-assets (--grid WxH --names a,b,c slices a sheet row-major; without --grid the whole image is one sprite). Then set SpriteRenderer.sprite to \"<id>#sprites/<name>\". Undoable like every command.", "akeir asset import <Assets/…/file.png> [--grid 16x16 --names player,goblin] [--ppu 16] [--filter nearest|linear] [--pivot 0.5,0.5]", false, false, false, cmdAssetImport});
    t.push_back({"apply", {"apply"}, "Mutation", "Apply a batch of commands (§49)", "Atomic batch: all commands commit as one ChangeSet or none. '$name' refers to an earlier change's result (as: name). --idempotency-key replays the stored response.",
                 "akeir apply <batch.json|-> [--dry-run] [--idempotency-key K]", false, false, false, cmdApply});
    t.push_back({"history.undo", {"undo"}, "Mutation", "Undo last change(s)", "Applies inverse(ops) of the newest history entry (§10.1). --actor X only undoes X's entries; conflicts if files changed underneath.",
                 "akeir undo [N] [--actor A]", false, false, false, cmdUndo});
    t.push_back({"history.redo", {"redo"}, "Mutation", "Redo undone change(s)", "", "akeir redo [N]", false, false, false, cmdRedo});
    t.push_back({"history.list", {"history"}, "Query", "List history", "Cache/history entries with cursor; `akeir history <cs_id>` shows one ChangeSet in full.", "akeir history [cs_id] [--limit N]", true, false, true, cmdHistory});
    t.push_back({"tx.begin", {"tx", "begin"}, "Mutation", "Begin a multi-call transaction (§9.1)", "Returns an opaque tx handle with a TTL. Pass --tx <id> to mutation commands; nothing is written until `tx commit`. Needs `akeir serve`.",
                 "akeir tx begin [--ttl ms]", false, false, false, cmdTxBegin});
    t.push_back({"tx.commit", {"tx", "commit"}, "Mutation", "Commit a transaction", "Composes the tx's ChangeSets into one history entry and writes the files (§9.2).", "akeir tx commit <tx_id> [--no-validate]", false, false, false, cmdTxCommit});
    t.push_back({"tx.rollback", {"tx", "rollback"}, "Mutation", "Discard a transaction", "", "akeir tx rollback <tx_id>", false, false, true, cmdTxRollback});
    t.push_back({"tx.list", {"tx", "list"}, "Query", "List open transactions", "", "akeir tx list", true, false, true, cmdTxList});
    t.push_back({"cmd", {"cmd"}, "Mutation", "Run any bus command by id", "Escape hatch for commands without CLI sugar (e.g. document.patch). Args as JSON.", "akeir cmd <command.id> --args '{json}'", false, false, false, cmdGeneric});
}

} // namespace akeir::cli
