// Tools/CLI/ProjectCommands.cpp — 프로젝트 문서 명령: project info, validate, fmt, schema, describe, explain(초기), entity list.
// 설계 문서 §11, §14 (schema), §14.1 (wire_format), §18 (explain), §29 (validate 출력 = envelope), §44 (describe), §5.3 (fmt)
#include "Commands.h"
#include "GameSystems.h"
#include "pme/commands/CommandBus.h"
#include "pme/core/ExitCodes.h"
#include "pme/core/Hash.h"
#include "pme/core/Log.h"
#include "pme/reflection/Registry.h"
#include "pme/runtime/Components.h"
#include "pme/runtime/Project.h"
#include "pme/serialization/Canonical.h"

#include <algorithm>
#include <filesystem>
#include <set>
#include <fstream>
#include <iterator>

namespace pme::cli {

std::optional<Project> openProject(Context& ctx, Envelope& fail, const std::string& command) {
    registerBuiltinComponents();
    if (ctx.resident) return *ctx.resident;   // serve: 상주 모델의 복사본 (읽기 명령용; 쓰기는 ctx.residentBus 로)
    if (ctx.projectDir.empty()) {
        fail = Envelope::failure(command, CommandError::make(ErrorCategory::NotFound, "PROJECT_NOT_FOUND",
            "No project.json found in the current directory or its parents. Use --project <dir>.", Json{{"cwd", std::filesystem::current_path().string()}}));
        return std::nullopt;
    }
    std::vector<Diagnostic> diags;
    auto prj = Project::load(ctx.projectDir, diags);
    if (!prj) {
        fail = Envelope::failure(command, CommandError::make(ErrorCategory::NotFound, "PROJECT_NOT_FOUND",
            "Cannot load project from " + ctx.projectDir + ".", Json{{"projectDir", ctx.projectDir}}));
        for (const auto& d : diags) fail.withWarning(d);
        return std::nullopt;
    }
    // 로드 단계 오류(파싱 실패 등)는 warnings 로 싣는다 — 호출자가 envelope 에 복사
    ctx.loadDiagnostics = diags;
    return prj;
}

Json diagArray(const std::vector<Diagnostic>& v) {
    Json a = Json::array();
    for (const auto& d : v) a.push_back(d.toJson());
    return a;
}

namespace {

Envelope cmdProjectInfo(Context& ctx) {
    Envelope fail;
    auto prj = openProject(ctx, fail, "project.info");
    if (!prj) return fail;
    Json r = Json::object();
    r["name"] = prj->name();
    r["rootDir"] = prj->rootDir();
    r["tickRate"] = prj->tickRate();
    r["seed"] = prj->seed();
    r["defaultWorld"] = prj->defaultWorld() ? Json(*prj->defaultWorld()) : Json(nullptr);
    r["worlds"] = Json::array();
    for (const auto& p : prj->worldPaths()) { const Json& d = *prj->document(p); r["worlds"].push_back(Json{{"id", d.value("id", "")}, {"name", d.value("name", "")}, {"path", p}, {"entities", d.contains("entities") ? d["entities"].size() : 0}}); }
    r["prefabs"] = Json::array();
    for (const auto& p : prj->prefabPaths()) { const Json& d = *prj->document(p); r["prefabs"].push_back(Json{{"id", d.value("id", "")}, {"name", d.value("name", "")}, {"path", p}, {"base", d.contains("base") ? d["base"] : Json(nullptr)}}); }
    Json comps = Json::array();
    for (const auto* m : Registry::global().all()) comps.push_back(m->name);
    r["components"] = comps;
    r["fpFlagsHash"] = PME_FP_FLAGS_HASH;
    r["engineVersion"] = PME_VERSION_STRING;
    Envelope env = Envelope::success("project.info", r);
    for (const auto& d : ctx.loadDiagnostics) env.withWarning(d);
    return env;
}

Envelope cmdValidate(Context& ctx) {
    Envelope fail;
    auto prj = openProject(ctx, fail, "validate");
    if (!prj) return fail;
    std::vector<Diagnostic> all = ctx.loadDiagnostics;
    auto v = prj->validate();
    all.insert(all.end(), v.begin(), v.end());

    // --fix (§29, §79): MachineApplicable fix 를 CommandBus 로 적용한다. Command 기반 fix(commands[]) 우선, 없으면 artifactChanges 를 document.patch 로.
    Json fixed = Json::array(), fixFailed = Json::array(), allChanges = Json::array();
    if (ctx.args.has("fix")) {
        BusOptions bo; bo.actor = ctx.args.get("actor").value_or("cli:validate-fix");
        std::unique_ptr<CommandBus> ownedBus;
        if (!ctx.residentBus) ownedBus = std::make_unique<CommandBus>(*prj, bo);
        CommandBus& bus = ctx.residentBus ? *ctx.residentBus : *ownedBus;   // serve 안에서는 단일 writer 를 쓴다
        ExecOptions eo; eo.dryRun = ctx.args.has("dry-run"); eo.validateAfter = false;   // fix 자체가 검증 오류를 고치는 중이므로 commit 전 검증은 끈다
        std::set<std::string> seen;   // 같은 fix 가 여러 진단(예: prefab + 각 instance)에 반복되면 한 번만
        std::vector<std::string> fmtDocs;
        for (const auto& d : all) {
            const Fix* pick = nullptr;
            for (const auto& f : d.fixes) if (f.applicability == Applicability::MachineApplicable && (!pick || f.isPreferred)) pick = &f;
            if (!pick) continue;
            std::string key = (d.physical ? d.physical->uri : "") + "|" + pick->description;
            for (const auto& c : pick->commands) key += "|" + c.op + jcsDump(c.args);
            for (const auto& a : pick->artifactChanges) key += "|" + jcsDump(a);
            if (!seen.insert(key).second) continue;
            if (!pick->commands.empty() && pick->commands[0].op == "project.fmt") {
                // canonical 재직렬화는 JSON 값이 바뀌지 않으므로 ChangeSet 으로 표현되지 않는다 → `akeir fmt` 와 같은 경로 (undo 대상 아님)
                if (d.physical) fmtDocs.push_back(d.physical->uri);
                continue;
            }
            Envelope r;
            if (!pick->commands.empty()) {
                Json batch = Json{{"atomic", true}, {"changes", Json::array()}};
                for (const auto& c : pick->commands) { Json ch = c.args; ch["op"] = c.op; batch["changes"].push_back(ch); }
                r = bus.apply(batch, eo);
            } else if (!pick->artifactChanges.empty() && d.physical) {
                r = bus.execute("document.patch", Json{{"doc", d.physical->uri}, {"ops", pick->artifactChanges}}, eo);
            } else continue;
            Json entry = Json{{"ruleId", d.ruleId}, {"fingerprint", d.fingerprint.empty() ? d.computeFingerprint() : d.fingerprint}, {"fix", pick->description}};
            if (r.ok) { entry["changeSet"] = r.meta.value("changeSet", ""); fixed.push_back(entry); for (const auto& c : r.changes) allChanges.push_back(c); }
            else { entry["error"] = r.error.toJson(); fixFailed.push_back(entry); }
        }
        if (!eo.dryRun) {
            std::sort(fmtDocs.begin(), fmtDocs.end());
            fmtDocs.erase(std::unique(fmtDocs.begin(), fmtDocs.end()), fmtDocs.end());
            for (const auto& doc : fmtDocs) {
                std::string err;
                Json entry = Json{{"ruleId", "JSON_NOT_CANONICAL"}, {"fix", "Rewrite file in canonical form"}, {"doc", doc}};
                if (prj->saveDocument(doc, &err)) fixed.push_back(entry); else { entry["error"] = err; fixFailed.push_back(entry); }
            }
        }
        // 다시 검증. JSON_NOT_CANONICAL 은 파일 바이트를 보므로 디스크에서 다시 읽는다 (serve 면 상주 모델이 이미 갱신되어 있다)
        if (!eo.dryRun) {
            if (ctx.resident) prj = *ctx.resident;
            else { std::vector<Diagnostic> reload; if (auto again = Project::load(ctx.projectDir, reload)) { prj = std::move(again); ctx.loadDiagnostics = reload; } }
        }
        all = ctx.loadDiagnostics;
        v = prj->validate();
        all.insert(all.end(), v.begin(), v.end());
    }

    DiagnosticSummary sum = summarize(all);
    Json body = Json{{"diagnostics", diagArray(all)}, {"summary", sum.toJson()}};
    if (ctx.args.has("fix")) { body["fixed"] = fixed; body["fixFailed"] = fixFailed; }
    if (sum.error == 0) {
        // §29: error 없으면 ok:true, result 에 진단과 요약
        Envelope env = Envelope::success("validate", body);
        env.changes = allChanges;
        if (ctx.args.has("dry-run")) env.meta["dryRun"] = true;
        return env;
    }
    Json details = body;
    Envelope env = Envelope::failure("validate", CommandError::make(ErrorCategory::Validation, "VALIDATION_FAILED",
        "Project has " + std::to_string(sum.error) + " error(s). Apply the MachineApplicable fixes with `akeir validate --fix` or fix the files.", details));
    env.changes = allChanges;
    return env;
}

Envelope cmdFmt(Context& ctx) {
    Envelope fail;
    auto prj = openProject(ctx, fail, "project.fmt");
    if (!prj) return fail;
    std::string only = ctx.args.positional(1, "");   // akeir fmt [path]
    std::vector<std::string> written, unchanged, failed;
    Json changes = Json::array();
    for (const auto& [path, doc] : prj->documents()) {
        if (!only.empty() && path != only) continue;
        std::filesystem::path f = std::filesystem::path(prj->rootDir()) / path;
        std::string before;
        { std::ifstream in(f, std::ios::binary); before.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()); }
        auto canon = canonicalDump(Project::canonicalizeDocument(doc));
        if (!canon) { failed.push_back(path + ": NaN/Inf"); continue; }
        if (*canon == before) { unchanged.push_back(path); continue; }
        if (ctx.args.has("dry-run")) { written.push_back(path); continue; }
        std::string err;
        if (!prj->saveDocument(path, &err)) { failed.push_back(path + ": " + err); continue; }
        written.push_back(path);
        changes.push_back(Json{{"op", "file.replace"}, {"doc", path}, {"beforeBlob", Sha256::hexOf(before)}, {"blob", Sha256::hexOf(*canon)}});
    }
    Json r = Json{{"written", written}, {"unchanged", unchanged}, {"failed", failed}};
    Envelope env = failed.empty()
        ? Envelope::success("project.fmt", r)
        : Envelope::failure("project.fmt", CommandError::make(ErrorCategory::Internal, "FMT_FAILED", "Some files could not be rewritten.", r));
    env.withChanges(changes);
    if (ctx.args.has("dry-run")) env.withMeta("dryRun", true);
    return env;
}

Envelope cmdSchema(Context& ctx) {
    // akeir schema component <Name> | akeir schema --all | akeir schema wire-format <Name>
    registerBuiltinComponents();
    std::string sub = ctx.args.positional(1, "");
    std::string name = ctx.args.positional(2, "");
    if (sub == "component" && !name.empty()) {
        const ComponentMeta* m = Registry::global().find(name);
        if (!m) {
            Json known = Json::array(); for (const auto* k : Registry::global().all()) known.push_back(k->name);
            return Envelope::failure("schema.describe", CommandError::make(ErrorCategory::NotFound, "COMPONENT_UNKNOWN", "Unknown component '" + name + "'.", Json{{"known", known}}));
        }
        return Envelope::success("schema.describe", Json{{"schema", m->toSchema()}, {"wireFormat", m->toWireFormat()}});
    }
    if (sub == "wire-format" && !name.empty()) {
        const ComponentMeta* m = Registry::global().find(name);
        if (!m) return Envelope::failure("schema.describe", CommandError::make(ErrorCategory::NotFound, "COMPONENT_UNKNOWN", "Unknown component '" + name + "'.", Json::object()));
        return Envelope::success("schema.describe", m->toWireFormat());
    }
    // 전체 목록
    Json comps = Json::object();
    for (const auto* m : Registry::global().all())
        comps[m->name] = ctx.args.has("all") ? m->toSchema() : Json{{"description", m->description}, {"requires", m->requiresComponents}, {"properties", m->props.size()}, {"$id", "game://schema/component/" + m->name + "/" + std::to_string(m->version)}};
    return Envelope::success("schema.describe", Json{{"components", comps}, {"hint", "akeir schema component <Name> for the full JSON Schema + wire format; --all for every schema"}});
}

Envelope cmdEntityList(Context& ctx) {
    Envelope fail;
    auto prj = openProject(ctx, fail, "entity.list");
    if (!prj) return fail;
    long long limit = ctx.args.getInt("limit").value_or(50);
    Json rows = Json::array();
    bool truncated = false;
    for (const auto& p : prj->worldPaths()) {
        const Json& w = *prj->document(p);
        if (!w.contains("entities")) continue;
        for (auto it = w["entities"].begin(); it != w["entities"].end(); ++it) {
            if (static_cast<long long>(rows.size()) >= limit) { truncated = true; break; }
            const Json& e = it.value();
            Json row = Json{{"id", it.key()}, {"name", e.value("name", "")}, {"path", prj->entityPath(it.key()).value_or("")}, {"world", w.value("id", "")}};
            if (e.contains("prefab")) row["prefab"] = e["prefab"];
            Json comps = Json::array();
            auto resolved = prj->resolveEntityComponents(it.key());
            if (resolved) for (auto c = resolved->begin(); c != resolved->end(); ++c) comps.push_back(c.key());
            row["components"] = comps;
            rows.push_back(row);
        }
    }
    Envelope env = Envelope::success("entity.list", Json{{"rows", rows}});
    if (truncated) env.withMeta("truncated", true);
    return env;
}

Envelope cmdExplain(Context& ctx) {
    // §18 explain <selector> — Phase 1 범위: 위치, prefab 체인, resolved components, 계층
    Envelope fail;
    auto prj = openProject(ctx, fail, "explain");
    if (!prj) return fail;
    std::string sel = ctx.args.positional(1, "");
    auto ids = prj->resolveSelector(sel);
    if (ids.empty()) return Envelope::failure("explain", CommandError::make(ErrorCategory::NotFound, "ENTITY_NOT_FOUND", "No entity/prefab/world matches '" + sel + "'.", Json{{"selector", sel}}));
    if (ids.size() > 1) return Envelope::failure("explain", CommandError::make(ErrorCategory::Usage, "AMBIGUOUS_SELECTOR", "Selector matches several ids; use a full id.", Json{{"candidates", ids}}));
    const std::string id = ids.front();
    auto loc = *prj->locate(id);
    const Json& doc = *prj->document(loc.doc);
    const Json& obj = loc.pointer.empty() ? doc : doc.at(JsonPointer(loc.pointer));
    Json r = Json::object();
    r["id"] = id; r["kind"] = loc.kind; r["name"] = obj.value("name", "");
    r["source"] = Json{{"doc", loc.doc}, {"pointer", loc.pointer}};
    if (loc.kind == "entity") {
        r["path"] = prj->entityPath(id).value_or("");
        r["parent"] = obj.contains("parent") ? obj["parent"] : Json(nullptr);
        Json children = Json::array();
        for (auto it = doc["entities"].begin(); it != doc["entities"].end(); ++it)
            if (it.value().contains("parent") && it.value()["parent"].is_string() && it.value()["parent"].get<std::string>() == id) children.push_back(it.key());
        r["children"] = children;
        if (obj.contains("prefab")) {
            Json chain = Json::array();
            std::string cur = obj["prefab"].get<std::string>();
            for (int i = 0; i < 16 && !cur.empty(); ++i) {
                auto pl = prj->locate(cur);
                if (!pl) { chain.push_back(Json{{"id", cur}, {"missing", true}}); break; }
                const Json& pd = *prj->document(pl->doc);
                chain.push_back(Json{{"id", cur}, {"name", pd.value("name", "")}, {"doc", pl->doc}});
                cur = pd.contains("base") && pd["base"].is_string() ? pd["base"].get<std::string>() : "";
            }
            r["prefabChain"] = chain;
            r["overrides"] = Json{{"set", obj.value("set", Json::object())}, {"add", obj.value("add", Json::object())}, {"remove", obj.value("remove", Json::array())}};
        }
    }
    std::vector<Diagnostic> diags;
    std::optional<Json> comps = (loc.kind == "entity") ? prj->resolveEntityComponents(id, &diags) : (loc.kind == "prefab" ? prj->resolvePrefab(id, &diags) : std::nullopt);
    if (comps) {
        r["components"] = *comps;
        Json lifecycle = Json::object();
        for (auto it = comps->begin(); it != comps->end(); ++it) if (const auto* m = Registry::global().find(it.key())) lifecycle[it.key()] = Json{{"requires", m->requiresComponents}, {"lifecycle", m->lifecycle}};
        r["lifecycle"] = lifecycle;
    }
    Envelope env = Envelope::success("explain", r);
    for (const auto& d : diags) env.withWarning(d);
    return env;
}

} // namespace

Envelope cmdRefs(Context& ctx) {
    // akeir refs <selector> [--out]  — 누가 이 객체를 가리키는가 (§19). --out 이면 이 객체가 가리키는 것.
    Envelope fail;
    game::registerGameComponents();
    auto prj = openProject(ctx, fail, "refs");
    if (!prj) return fail;
    std::string sel = ctx.args.positional(1, "");
    if (sel.empty()) return Envelope::failure("refs", CommandError::make(ErrorCategory::Usage, "USAGE_ERROR", "akeir refs <id|name:X|path:A/B> [--out] [--json]"));
    auto ids = prj->resolveSelector(sel);
    if (ids.size() != 1) return Envelope::failure("refs", CommandError::make(ids.empty() ? ErrorCategory::NotFound : ErrorCategory::Usage, ids.empty() ? "OBJECT_NOT_FOUND" : "AMBIGUOUS_SELECTOR",
        ids.empty() ? "Nothing matches '" + sel + "'." : "'" + sel + "' matches several objects.", Json{{"selector", sel}, {"candidates", ids}}));
    const std::string& id = ids.front();
    auto loc = prj->locate(id);
    Json r = Json{{"id", id}, {"kind", loc ? loc->kind : ""}, {"doc", loc ? loc->doc : ""}};
    if (auto path = prj->entityPath(id)) r["path"] = *path;
    Json in = Json::array(), out = Json::array();
    for (const auto& ref : prj->referencesTo(id)) in.push_back(ref.toJson());
    for (const auto& ref : prj->referencesFrom(id)) out.push_back(ref.toJson());
    r["referencedBy"] = in;
    r["references"] = out;
    r["referencedByCount"] = in.size();
    Envelope env = Envelope::success("refs", r);
    for (const auto& d : ctx.loadDiagnostics) env.withWarning(d);
    return env;
}

void registerProjectCommands(std::vector<CommandSpec>& table) {
    table.push_back({"project.info", {"project", "info"}, "Query", "Project summary",
        "Name, tick rate, seed, worlds, prefabs, registered components.", "akeir project info [--project DIR] [--json]", true, false, true, cmdProjectInfo});
    table.push_back({"validate", {"validate"}, "Query", "Validate project data",
        "Runs all §29 checks (ids, schemas, requires, refs, prefab chains, canonical form). Errors → exit 3 with diagnostics + fixes. --fix applies MachineApplicable fixes through the CommandBus (undoable).",
        "akeir validate [--fix [--dry-run]] [--project DIR] [--json]", true, false, true, cmdValidate});
    table.push_back({"project.fmt", {"fmt"}, "Mutation", "Rewrite files in canonical form",
        "Rewrites world/prefab files with the §5.3 canonical serializer (temp+rename). --dry-run lists files that would change.",
        "akeir fmt [path] [--dry-run] [--json]", false, false, true, cmdFmt});
    table.push_back({"schema.describe", {"schema"}, "Query", "Component schemas",
        "JSON Schema 2020-12 (+x-*) and wire format for components (§14, §14.1).",
        "akeir schema [component <Name> | wire-format <Name>] [--all] [--json]", true, false, true, cmdSchema});
    table.push_back({"entity.list", {"entity", "list"}, "Query", "List entities",
        "Entities across worlds with path, prefab and resolved component names.", "akeir entity list [--limit N] [--json]", true, false, true, cmdEntityList});
    table.push_back({"refs", {"refs"}, "Query", "Reference graph of an object (§19)",
        "Who points at this id (prefab instances, children via parent, Ref properties, overrides, defaultWorld) and what it points at. Authoring documents only.",
        "akeir refs <id|name:X|path:A/B> [--json]", true, false, true, cmdRefs});
    table.push_back({"explain", {"explain"}, "Query", "Explain an object (§18)",
        "Where it lives, prefab chain, overrides, children, resolved components and lifecycle info.",
        "akeir explain <id|name:X|path:A/B> [--json]", true, false, true, cmdExplain});
}

} // namespace pme::cli
