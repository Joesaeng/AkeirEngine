// CommandBus.cpp — §8 ChangeBuilder/CommandContext, §9 tx/commit, §10 undo/redo, §49 apply, §50 dry-run
#include "akeir/commands/CommandBus.h"

#include "akeir/core/Hash.h"
#include "akeir/core/Id.h"
#include "akeir/core/Log.h"
#include "akeir/core/Time.h"
#include "akeir/serialization/Canonical.h"

#include <filesystem>

namespace fs = std::filesystem;

namespace akeir {

const char* commandKindName(CommandKind k) {
    switch (k) {
        case CommandKind::Mutation: return "Mutation";
        case CommandKind::Query: return "Query";
        case CommandKind::RuntimeControl: return "RuntimeControl";
        case CommandKind::Meta: return "Meta";
    }
    return "?";
}

std::string nextOrderKey(std::string_view lastKey) {
    if (lastKey.empty()) return "a0";
    std::string k(lastKey);
    // 마지막 문자를 0-9a-z 순으로 올린다. 'z' 면 한 자리 더 넓힌다 (단순 fractional indexing, §6)
    char c = k.back();
    if (c >= '0' && c < '9') { k.back() = static_cast<char>(c + 1); return k; }
    if (c == '9') { k.back() = 'a'; return k; }
    if (c >= 'a' && c < 'z') { k.back() = static_cast<char>(c + 1); return k; }
    // 'z' 또는 알 수 없는 문자 → 선두 문자 증가 + "00"
    char head = k.front();
    if (head >= 'a' && head < 'z') return std::string(1, static_cast<char>(head + 1)) + "00";
    return k + "0";
}

// ---------------------------------------------------------------- ChangeBuilder

const Json* ChangeBuilder::get(const std::string& doc, const std::string& path) const {
    auto it = docs_.find(doc);
    if (it == docs_.end()) return nullptr;
    if (path.empty()) return &it->second;
    try {
        Json::json_pointer p(path);
        if (!it->second.contains(p)) return nullptr;
        return &it->second.at(p);
    } catch (...) { return nullptr; }
}

bool ChangeBuilder::push(ChangeOp op) {
    std::vector<Diagnostic> diags;
    if (!applyOps(docs_, {op}, &diags)) {
        errors_.insert(errors_.end(), diags.begin(), diags.end());
        return false;
    }
    ops_.push_back(std::move(op));
    if (project_) project_->reindex();
    return true;
}

bool ChangeBuilder::set(const std::string& doc, const std::string& path, Json value) {
    if (const Json* cur = get(doc, path)) {
        if (*cur == value) return true;   // no-op: op 를 남기지 않는다
        return replace(doc, path, std::move(value));
    }
    return add(doc, path, std::move(value));
}

bool ChangeBuilder::add(const std::string& doc, const std::string& path, Json value) {
    ChangeOp o; o.op = "add"; o.doc = doc; o.path = path; o.value = std::move(value);
    // "/arr/-" (append) 는 역연산이 불가능하므로 구체적 인덱스로 바꿔 기록한다 (§78 규칙 1: self-inverting)
    if (o.path.size() >= 2 && o.path.compare(o.path.size() - 2, 2, "/-") == 0) {
        const Json* arr = get(doc, o.path.substr(0, o.path.size() - 2));
        if (!arr || !arr->is_array()) { errors_.push_back(Diagnostic::error("CHANGESET_PATH_MISSING", "append target is not an array.").in(PhysicalLocation{doc, o.path, std::nullopt})); return false; }
        o.path = o.path.substr(0, o.path.size() - 1) + std::to_string(arr->size());
    }
    return push(std::move(o));
}

bool ChangeBuilder::replace(const std::string& doc, const std::string& path, Json value) {
    const Json* cur = get(doc, path);
    if (!cur) { errors_.push_back(Diagnostic::error("CHANGESET_PATH_MISSING", "replace target does not exist.").in(PhysicalLocation{doc, path, std::nullopt})); return false; }
    ChangeOp o; o.op = "replace"; o.doc = doc; o.path = path; o.before = *cur; o.value = std::move(value);
    return push(std::move(o));
}

bool ChangeBuilder::remove(const std::string& doc, const std::string& path) {
    const Json* cur = get(doc, path);
    if (!cur) { errors_.push_back(Diagnostic::error("CHANGESET_PATH_MISSING", "remove target does not exist.").in(PhysicalLocation{doc, path, std::nullopt})); return false; }
    ChangeOp o; o.op = "remove"; o.doc = doc; o.path = path; o.before = *cur;
    return push(std::move(o));
}

bool ChangeBuilder::move(const std::string& doc, const std::string& from, const std::string& path) {
    const Json* cur = get(doc, from);
    if (!cur) { errors_.push_back(Diagnostic::error("CHANGESET_PATH_MISSING", "move source does not exist.").in(PhysicalLocation{doc, from, std::nullopt})); return false; }
    ChangeOp o; o.op = "move"; o.doc = doc; o.from = from; o.path = path; o.before = *cur;
    return push(std::move(o));
}

bool ChangeBuilder::test(const std::string& doc, const std::string& path, Json value) {
    ChangeOp o; o.op = "test"; o.doc = doc; o.path = path; o.value = std::move(value);
    return push(std::move(o));
}

bool ChangeBuilder::addDocument(const std::string& doc, Json value) {
    ChangeOp o; o.op = "add"; o.doc = doc; o.path = ""; o.value = std::move(value);
    return push(std::move(o));
}

bool ChangeBuilder::removeDocument(const std::string& doc) {
    const Json* cur = get(doc, "");
    if (!cur) { errors_.push_back(Diagnostic::error("CHANGESET_DOC_MISSING", "document does not exist.").in(PhysicalLocation{doc, "", std::nullopt})); return false; }
    ChangeOp o; o.op = "remove"; o.doc = doc; o.path = ""; o.before = *cur;
    return push(std::move(o));
}

// ---------------------------------------------------------------- CommandContext

bool CommandContext::fail(ErrorCategory cat, std::string ruleId, std::string text, Json details) {
    error = CommandError::make(cat, std::move(ruleId), std::move(text), std::move(details));
    return false;
}

bool CommandContext::failDiagnostic(ErrorCategory cat, Diagnostic d, Json details) {
    CommandError e;
    e.diagnostic = std::move(d);
    e.category = cat;
    e.details = std::move(details);
    error = std::move(e);
    return false;
}

namespace {

std::optional<CommandContext::EntityRef> resolveKind(CommandContext& ctx, const std::string& selector, const char* kind, const char* notFoundRule) {
    auto ids = ctx.project.resolveSelector(selector);
    std::vector<std::string> matches;
    for (const auto& id : ids) if (auto loc = ctx.project.locate(id); loc && loc->kind == kind) matches.push_back(id);
    if (matches.empty()) {
        ctx.fail(ErrorCategory::NotFound, notFoundRule, std::string(kind) + " '" + selector + "' not found.", Json{{"selector", selector}});
        return std::nullopt;
    }
    if (matches.size() > 1) {
        ctx.fail(ErrorCategory::Usage, "AMBIGUOUS_SELECTOR", "'" + selector + "' matches " + std::to_string(matches.size()) + " " + kind + "s; use an id or path: selector.", Json{{"selector", selector}, {"candidates", matches}});
        return std::nullopt;
    }
    auto loc = ctx.project.locate(matches[0]);
    return CommandContext::EntityRef{loc->doc, loc->pointer, matches[0]};
}

} // namespace

std::optional<CommandContext::EntityRef> CommandContext::resolveEntity(const std::string& selector) {
    return resolveKind(*this, selector, "entity", "ENTITY_NOT_FOUND");
}

std::optional<CommandContext::EntityRef> CommandContext::resolvePrefab(const std::string& selector) {
    return resolveKind(*this, selector, "prefab", "PREFAB_NOT_FOUND");
}

std::optional<std::string> CommandContext::resolveWorldDoc(const std::string& selector) {
    if (selector.empty()) {
        auto dw = project.defaultWorld();
        if (!dw) { fail(ErrorCategory::Usage, "WORLD_REQUIRED", "No world given and project has no defaultWorld."); return std::nullopt; }
        auto loc = project.locate(*dw);
        if (!loc || loc->kind != "world") { fail(ErrorCategory::NotFound, "WORLD_NOT_FOUND", "defaultWorld " + *dw + " is not loaded."); return std::nullopt; }
        return loc->doc;
    }
    auto r = resolveKind(*this, selector, "world", "WORLD_NOT_FOUND");
    if (!r) return std::nullopt;
    return r->doc;
}

// ---------------------------------------------------------------- CommandBus

CommandBus::CommandBus(Project& project, BusOptions options)
    : project_(project), options_(std::move(options)), history_(project.rootDir()) {
    if (options_.persist) {
        std::string err;
        if (!history_.load(&err)) AKEIR_LOG(Warn, "commands", "history.load_failed", err);
    }
    std::vector<std::string> all;
    for (const auto& [p, _] : project_.documents()) all.push_back(p);
    refreshBaseHashes(all);
    registerBuiltinCommands(*this);
}

void CommandBus::registerCommand(CommandDef def) {
    for (auto& d : defs_) if (d.id == def.id) { d = std::move(def); return; }
    defs_.push_back(std::move(def));
}

const CommandDef* CommandBus::find(std::string_view idOrAlias) const {
    for (const auto& d : defs_) {
        if (d.id == idOrAlias) return &d;
        for (const auto& a : d.aliases) if (a == idOrAlias) return &d;
    }
    return nullptr;
}

Json CommandBus::commandsJson() const {
    Json arr = Json::array();
    for (const auto& d : defs_) {
        Json j = Json::object();
        j["id"] = d.id;
        j["kind"] = commandKindName(d.kind);
        j["description"] = d.description;
        if (!d.aliases.empty()) j["aliases"] = d.aliases;
        j["args"] = d.argsSchema;
        arr.push_back(std::move(j));
    }
    return arr;
}

std::string CommandBus::docHash(const Json& doc) {
    return Sha256::hexOf(jcsDump(Project::canonicalizeDocument(doc)), true);
}

void CommandBus::refreshBaseHashes(const std::vector<std::string>& docs) {
    for (const auto& p : docs) {
        const Json* d = project_.document(p);
        if (d) baseHashes_[p] = docHash(*d);
        else baseHashes_.erase(p);
    }
}

Envelope CommandBus::finish(Envelope env, const ChangeSet* cs, const ExecOptions& opts) {
    if (cs) {
        Json ops = Json::array();
        for (const auto& o : cs->ops) ops.push_back(o.toJson());
        env.changes = std::move(ops);
        if (!cs->id.empty()) env.meta["changeSet"] = cs->id;
        env.meta["touched"] = cs->touched;
    }
    if (opts.dryRun) env.meta["dryRun"] = true;
    if (!opts.tx.empty()) env.meta["tx"] = opts.tx;
    return env;
}

std::optional<Envelope> CommandBus::runMutation(const CommandDef& def, const Json& args, Project& fork, ChangeSet& outCs,
                                                Json& outResult, std::vector<Diagnostic>& outWarnings) {
    ChangeBuilder builder(fork);
    CommandContext ctx{fork, args, builder, options_.actor};
    bool ok = false;
    try {
        ok = def.handler(ctx);
    } catch (const std::exception& e) {
        ctx.fail(ErrorCategory::Internal, "INTERNAL_ERROR", std::string("command threw: ") + e.what());
        ok = false;
    }
    if (!ok) {
        if (!ctx.error) ctx.fail(ErrorCategory::Internal, "COMMAND_FAILED", def.id + " failed without an error object.");
        if (!builder.errors().empty()) {
            Json d = Json::array();
            for (const auto& x : builder.errors()) d.push_back(x.toJson());
            ctx.error->details["changeErrors"] = std::move(d);
        }
        Envelope env = Envelope::failure(def.id, *ctx.error);
        for (auto& w : ctx.warnings) env.withWarning(w);
        return env;
    }
    outCs.ops = builder.take();
    outCs.actor = options_.actor;
    outCs.intent = Json{{"op", def.id}, {"args", args}};
    if (ctx.result.is_object() && ctx.result.contains("id")) outCs.intent["resolved"] = Json{{"id", ctx.result["id"]}};
    outCs.finalize();
    outResult = std::move(ctx.result);
    outWarnings = std::move(ctx.warnings);
    return std::nullopt;
}

std::optional<CommandError> CommandBus::validateFork(const Project& fork, const std::vector<std::string>& touched) {
    if (!baselineComputed_) {
        for (const auto& d : project_.validate()) if (d.level == Severity::Error) baselineErrorFingerprints_.insert(d.fingerprint.empty() ? d.computeFingerprint() : d.fingerprint);
        baselineComputed_ = true;
    }
    std::vector<Diagnostic> fresh;
    for (auto& d : fork.validate()) {
        if (d.level != Severity::Error) continue;
        std::string fp = d.fingerprint.empty() ? d.computeFingerprint() : d.fingerprint;
        if (baselineErrorFingerprints_.count(fp)) continue;
        bool inTouched = !d.physical.has_value();
        if (d.physical) for (const auto& t : touched) if (d.physical->uri == t) inTouched = true;
        if (inTouched) fresh.push_back(std::move(d));
    }
    if (fresh.empty()) return std::nullopt;
    Json diags = Json::array();
    for (const auto& d : fresh) diags.push_back(d.toJson());
    CommandError e = CommandError::make(ErrorCategory::Validation, "VALIDATION_FAILED",
                                        "Change would introduce " + std::to_string(fresh.size()) + " validation error(s); rejected (use --no-validate to force).",
                                        Json{{"diagnostics", diags}});
    e.diagnostic = fresh.front();
    e.diagnostic.ruleId = "VALIDATION_FAILED";
    return e;
}

std::optional<CommandError> CommandBus::commit(ChangeSet& cs, bool pushHistory) {
    if (cs.id.empty()) cs.id = Id::generate("cs").str();
    if (cs.createdAt.empty()) cs.createdAt = WallTime::now().iso8601();
    if (cs.actor.empty()) cs.actor = options_.actor;
    cs.finalize();

    // 1. base 검사 — 디스크가 우리가 로드한 상태와 같은가 (§9.2 optimistic concurrency)
    cs.base = Json::object();
    for (const auto& doc : cs.touched) {
        auto it = baseHashes_.find(doc);
        if (it == baseHashes_.end()) continue;   // 새 문서
        cs.base[doc] = it->second;
        if (!options_.persist) continue;
        fs::path file = fs::path(project_.rootDir()) / doc;
        std::error_code ec;
        if (!fs::exists(file, ec)) continue;       // 로드 후 삭제됨 → 우리가 다시 쓴다 (경고 없음)
        std::string err;
        auto onDisk = readJsonFile(file.string(), &err);
        if (!onDisk) return CommandError::make(ErrorCategory::Conflict, "BASE_UNREADABLE", "Cannot re-read " + doc + " before commit: " + err, Json{{"doc", doc}});
        std::string h = docHash(*onDisk);
        if (h != it->second)
            return CommandError::make(ErrorCategory::Conflict, "BASE_MISMATCH", doc + " changed on disk since it was loaded; reload and retry.",
                                      Json{{"doc", doc}, {"expected", it->second}, {"actual", h}});
    }

    // 2. journal
    std::string err;
    if (options_.persist && !history_.writeJournal(cs, &err))
        return CommandError::make(ErrorCategory::Internal, "JOURNAL_WRITE_FAILED", err);

    // 3. 메모리 적용
    std::vector<Diagnostic> diags;
    if (!applyOps(project_.documentsMut(), cs.ops, &diags)) {
        project_.reindex();
        Json d = Json::array();
        for (const auto& x : diags) d.push_back(x.toJson());
        if (options_.persist) history_.clearJournal(cs.id);
        return CommandError::make(ErrorCategory::Conflict, "CHANGESET_APPLY_FAILED", "ops could not be applied to the live project.", Json{{"diagnostics", d}});
    }
    project_.reindex();

    // 4. 파일 저장 (temp+rename, §5.3 canonical)
    if (options_.persist) {
        for (const auto& doc : cs.touched) {
            if (project_.document(doc)) {
                if (!project_.saveDocument(doc, &err))
                    return CommandError::make(ErrorCategory::Internal, "SAVE_FAILED", "Could not write " + doc + ": " + err + " (journal " + cs.id + " kept for recovery).", Json{{"doc", doc}});
            } else {
                std::error_code ec;
                fs::remove(fs::path(project_.rootDir()) / doc, ec);
            }
        }
        // 5. history + journal 정리
        if (pushHistory && !history_.push(cs, &err))
            return CommandError::make(ErrorCategory::Internal, "HISTORY_WRITE_FAILED", err);
        history_.clearJournal(cs.id);
    } else if (pushHistory) {
        history_.push(cs, nullptr);   // in-memory 만 (persist=false 면 History::push 가 디스크 쓰기 실패를 무시한다)
    }
    refreshBaseHashes(cs.touched);
    AKEIR_LOG(Info, "commands", "changeset.committed", cs.id, Json{{"ops", cs.ops.size()}, {"touched", cs.touched}});
    return std::nullopt;
}

Envelope CommandBus::execute(const std::string& id, const Json& args, const ExecOptions& opts) {
    const CommandDef* def = find(id);
    if (!def) return Envelope::failure(id, CommandError::make(ErrorCategory::Usage, "UNKNOWN_COMMAND", "Unknown command '" + id + "'. See `akeir capabilities`."));
    Json argsObj = args.is_object() ? args : Json::object();

    if (def->kind != CommandKind::Mutation) {
        // Query/Meta: project 를 직접 읽는다. builder 는 쓰이지 않아야 한다 (쓰면 ops 가 버려지고 경고).
        std::map<std::string, Json> scratch;
        ChangeBuilder builder(scratch);
        CommandContext ctx{project_, argsObj, builder, options_.actor};
        bool ok = false;
        try { ok = def->handler(ctx); } catch (const std::exception& e) { ctx.fail(ErrorCategory::Internal, "INTERNAL_ERROR", std::string("command threw: ") + e.what()); }
        Envelope env = ok ? Envelope::success(def->id, ctx.result)
                          : Envelope::failure(def->id, ctx.error.value_or(CommandError::make(ErrorCategory::Internal, "COMMAND_FAILED", def->id + " failed.")));
        for (auto& w : ctx.warnings) env.withWarning(w);
        return env;
    }

    // ---- Mutation ----
    if (!opts.tx.empty()) {
        expireTransactions();
        auto it = txs_.find(opts.tx);
        if (it == txs_.end()) return Envelope::failure(def->id, CommandError::make(ErrorCategory::NotFound, "TX_UNKNOWN_OR_EXPIRED", "Transaction " + opts.tx + " is not open (unknown or expired; begin a new one).", Json{{"tx", opts.tx}}));
        ChangeSet cs; Json result; std::vector<Diagnostic> warnings;
        if (auto fail = runMutation(*def, argsObj, it->second.fork, cs, result, warnings)) return finish(std::move(*fail), nullptr, opts);
        it->second.parts.push_back(cs);
        it->second.results.push_back(result);
        Envelope env = Envelope::success(def->id, result);
        for (auto& w : warnings) env.withWarning(w);
        env.meta["committed"] = false;
        return finish(std::move(env), &cs, opts);
    }

    Project fork = project_;
    ChangeSet cs; Json result; std::vector<Diagnostic> warnings;
    if (auto fail = runMutation(*def, argsObj, fork, cs, result, warnings)) return finish(std::move(*fail), nullptr, opts);
    if (opts.validateAfter && !cs.empty())
        if (auto err = validateFork(fork, cs.touched)) {
            // 거부된 명령: 제안된 ops 는 버려졌다 — changes 를 싣지 않고 rolledBack 만 표시 (fix 가 가리키는 새 id 도 존재하지 않는다)
            Envelope env = Envelope::failure(def->id, *err);
            env.meta["rolledBack"] = true;
            env.meta["committed"] = false;
            env.error.details["proposedOps"] = cs.ops.size();
            return finish(std::move(env), nullptr, opts);
        }
    if (opts.dryRun) {
        Envelope env = Envelope::success(def->id, result);
        for (auto& w : warnings) env.withWarning(w);
        env.meta["committed"] = false;
        return finish(std::move(env), &cs, opts);
    }
    if (!cs.empty()) {
        if (auto err = commit(cs, true)) return finish(Envelope::failure(def->id, *err), &cs, opts);
    }
    Envelope env = Envelope::success(def->id, result);
    for (auto& w : warnings) env.withWarning(w);
    env.meta["committed"] = !cs.empty();
    return finish(std::move(env), &cs, opts);
}

// ---------------------------------------------------------------- tx (§9)

std::string CommandBus::beginTx(long long ttlMs) {
    expireTransactions();
    std::string id = Id::generate("tx").str();
    Tx tx{project_, {}, Json::array()};
    tx.createdAt = std::chrono::steady_clock::now();
    tx.ttlMs = ttlMs;
    tx.expiresAt = tx.createdAt + std::chrono::milliseconds(ttlMs);
    txs_.emplace(id, std::move(tx));
    return id;
}

Json CommandBus::txInfo(const std::string& txId) const {
    auto it = txs_.find(txId);
    if (it == txs_.end()) return Json();
    auto now = std::chrono::steady_clock::now();
    long long left = std::chrono::duration_cast<std::chrono::milliseconds>(it->second.expiresAt - now).count();
    return Json{{"tx", txId}, {"ttlMs", it->second.ttlMs}, {"expiresInMs", left < 0 ? 0 : left}, {"commands", it->second.parts.size()}};
}

Json CommandBus::txList() const {
    Json arr = Json::array();
    for (const auto& [id, _] : txs_) arr.push_back(txInfo(id));
    return arr;
}

std::vector<std::string> CommandBus::expireTransactions() {
    std::vector<std::string> gone;
    auto now = std::chrono::steady_clock::now();
    for (auto it = txs_.begin(); it != txs_.end();) {
        if (it->second.ttlMs > 0 && now >= it->second.expiresAt) { gone.push_back(it->first); it = txs_.erase(it); }
        else ++it;
    }
    return gone;
}

Envelope CommandBus::commitTx(const std::string& txId, const ExecOptions& optsIn) {
    expireTransactions();
    auto it = txs_.find(txId);
    if (it == txs_.end()) return Envelope::failure("tx.commit", CommandError::make(ErrorCategory::NotFound, "TX_UNKNOWN_OR_EXPIRED", "Transaction " + txId + " is not open (unknown or expired).", Json{{"tx", txId}}));
    ChangeSet cs = ChangeSet::compose(it->second.parts, txId);
    Json results = it->second.results;
    ExecOptions opts; opts.tx = txId;
    if (!cs.empty()) {
        if (optsIn.validateAfter) if (auto err = validateFork(it->second.fork, cs.touched)) { txs_.erase(it); return finish(Envelope::failure("tx.commit", *err), &cs, opts); }
        if (auto err = commit(cs, true)) { txs_.erase(it); return finish(Envelope::failure("tx.commit", *err), &cs, opts); }
    }
    txs_.erase(it);
    Envelope env = Envelope::success("tx.commit", Json{{"results", results}, {"count", results.size()}});
    env.meta["committed"] = !cs.empty();
    return finish(std::move(env), &cs, opts);
}

Envelope CommandBus::rollbackTx(const std::string& txId) {
    auto it = txs_.find(txId);
    if (it == txs_.end()) return Envelope::failure("tx.rollback", CommandError::make(ErrorCategory::NotFound, "TX_UNKNOWN_OR_EXPIRED", "Transaction " + txId + " is not open (unknown or expired).", Json{{"tx", txId}}));
    std::size_t n = it->second.parts.size();
    txs_.erase(it);
    return Envelope::success("tx.rollback", Json{{"discarded", n}});
}

// ---------------------------------------------------------------- apply (§49)

namespace {

/// "$name" / "$name.field" 참조를 results 로 치환 (재귀)
bool substituteRefs(Json& v, const std::map<std::string, Json>& named, std::string& err) {
    if (v.is_string()) {
        const std::string s = v.get<std::string>();
        if (s.size() < 2 || s[0] != '$') return true;
        if (s[1] == '$') { v = s.substr(1); return true; }   // "$$x" → 리터럴 "$x"
        std::string name = s.substr(1), field;
        if (auto dot = name.find('.'); dot != std::string::npos) { field = name.substr(dot + 1); name = name.substr(0, dot); }
        auto it = named.find(name);
        if (it == named.end()) { err = "unknown reference '" + s + "' (no earlier change has as: \"" + name + "\")"; return false; }
        if (field.empty()) { v = it->second.is_object() && it->second.contains("id") ? it->second["id"] : it->second; return true; }
        if (!it->second.is_object() || !it->second.contains(field)) { err = "reference '" + s + "': result has no field '" + field + "'"; return false; }
        v = it->second[field];
        return true;
    }
    if (v.is_object()) { for (auto& [k, x] : v.items()) if (!substituteRefs(x, named, err)) return false; }
    else if (v.is_array()) { for (auto& x : v) if (!substituteRefs(x, named, err)) return false; }
    return true;
}

} // namespace

Envelope CommandBus::apply(const Json& batch, const ExecOptions& optsIn) {
    if (!batch.is_object() || !batch.contains("changes") || !batch["changes"].is_array())
        return Envelope::failure("apply", CommandError::make(ErrorCategory::Usage, "APPLY_INVALID", "apply expects { changes: [ {op, ...args, as?} ], atomic?, dryRun?, idempotencyKey? }."));
    ExecOptions opts = optsIn;
    opts.dryRun = opts.dryRun || batch.value("dryRun", false);
    if (opts.idempotencyKey.empty()) opts.idempotencyKey = batch.value("idempotencyKey", "");
    const bool atomic = batch.value("atomic", true);

    if (!opts.idempotencyKey.empty() && options_.persist && !opts.dryRun) {
        if (auto prev = history_.idempotent(opts.idempotencyKey)) {
            Envelope env = Envelope::success("apply", prev->value("result", Json::object()));
            env.changes = prev->value("changes", Json::array());
            env.meta["idempotentReplay"] = true;
            env.meta["changeSet"] = prev->value("meta", Json::object()).value("changeSet", "");
            return env;
        }
    }

    std::map<std::string, Json> named;
    Json results = Json::array();
    std::string txId = atomic ? beginTx() : "";

    auto failAt = [&](std::size_t i, const std::string& op, CommandError err) {
        if (atomic) rollbackTx(txId);
        err.details["index"] = i;
        err.details["op"] = op;
        err.details["results"] = results;
        Envelope env = Envelope::failure("apply", std::move(err));
        return finish(std::move(env), nullptr, opts);
    };

    const Json& changes = batch["changes"];
    for (std::size_t i = 0; i < changes.size(); ++i) {
        const Json& c = changes[i];
        if (!c.is_object() || !c.contains("op") || !c["op"].is_string())
            return failAt(i, "", CommandError::make(ErrorCategory::Usage, "APPLY_INVALID", "changes[" + std::to_string(i) + "] needs a string 'op'."));
        std::string op = c["op"].get<std::string>();
        std::string as = c.value("as", "");
        Json args = Json::object();
        for (const auto& [k, v] : c.items()) if (k != "op" && k != "as") args[k] = v;
        std::string err;
        if (!substituteRefs(args, named, err)) return failAt(i, op, CommandError::make(ErrorCategory::Usage, "APPLY_BAD_REFERENCE", err));

        ExecOptions one = opts;
        one.tx = txId;
        one.idempotencyKey.clear();
        Envelope env = execute(op, args, one);
        if (!env.ok) return failAt(i, op, env.error);
        if (!as.empty()) named[as] = env.result;
        Json r = Json::object();
        r["op"] = op;
        if (!as.empty()) r["as"] = as;
        r["result"] = env.result;
        if (!atomic) r["changes"] = env.changes;
        results.push_back(std::move(r));
    }

    if (!atomic) {
        Envelope env = Envelope::success("apply", Json{{"results", results}, {"count", results.size()}, {"atomic", false}});
        return finish(std::move(env), nullptr, opts);
    }

    Envelope env;
    if (opts.dryRun) {
        auto it = txs_.find(txId);
        ChangeSet cs = ChangeSet::compose(it->second.parts, txId);
        std::optional<CommandError> verr;
        if (opts.validateAfter && !cs.empty()) verr = validateFork(it->second.fork, cs.touched);
        txs_.erase(it);
        env = verr ? Envelope::failure("apply", *verr) : Envelope::success("apply", Json{{"results", results}, {"count", results.size()}, {"atomic", true}});
        env.meta["committed"] = false;
        env = finish(std::move(env), &cs, opts);
    } else {
        Envelope c = commitTx(txId, opts);
        if (!c.ok) { c.command = "apply"; return finish(std::move(c), nullptr, opts); }
        env = Envelope::success("apply", Json{{"results", results}, {"count", results.size()}, {"atomic", true}});
        env.changes = c.changes;
        env.meta = c.meta;
        env.meta.erase("tx");
        if (!opts.idempotencyKey.empty() && options_.persist) history_.rememberIdempotent(opts.idempotencyKey, env.toJson());
    }
    return env;
}

// ---------------------------------------------------------------- undo / redo (§10)

Envelope CommandBus::undo(int steps, const std::string& actorFilter) {
    Json undone = Json::array();
    Json ops = Json::array();
    for (int i = 0; i < steps; ++i) {
        const ChangeSet* target = history_.undoTarget();
        if (!target) {
            if (i == 0) return Envelope::failure("history.undo", CommandError::make(ErrorCategory::Precondition, "NOTHING_TO_UNDO", "History cursor is at the beginning."));
            break;
        }
        if (!actorFilter.empty() && target->actor != actorFilter)
            return Envelope::failure("history.undo", CommandError::make(ErrorCategory::Conflict, "UNDO_ACTOR_MISMATCH",
                                     "Most recent change " + target->id + " belongs to actor '" + target->actor + "', not '" + actorFilter + "'. Undo across actors needs an explicit --actor any.",
                                     Json{{"changeSet", target->id}, {"actor", target->actor}}));
        ChangeSet inv = target->inverse();
        inv.actor = options_.actor;
        if (auto err = commit(inv, false)) {
            if (err->diagnostic.ruleId == "CHANGESET_APPLY_FAILED") {
                err->diagnostic.ruleId = "UNDO_CONFLICT";
                err->diagnostic.message.text = "Cannot undo " + target->id + ": the document no longer matches the recorded 'before' values (§10.2).";
            }
            return Envelope::failure("history.undo", *err);
        }
        std::string err;
        history_.markUndone(&err);
        undone.push_back(target->id);
        for (const auto& o : inv.ops) ops.push_back(o.toJson());
    }
    Envelope env = Envelope::success("history.undo", Json{{"undone", undone}, {"cursor", history_.cursor()}});
    env.changes = std::move(ops);
    return env;
}

Envelope CommandBus::redo(int steps) {
    Json redone = Json::array();
    Json ops = Json::array();
    for (int i = 0; i < steps; ++i) {
        const ChangeSet* target = history_.redoTarget();
        if (!target) {
            if (i == 0) return Envelope::failure("history.redo", CommandError::make(ErrorCategory::Precondition, "NOTHING_TO_REDO", "History cursor is at the end."));
            break;
        }
        ChangeSet again = *target;
        again.id.clear();              // 새 commit id (history 항목은 원본을 유지)
        again.createdAt.clear();
        again.actor = options_.actor;
        if (auto err = commit(again, false)) {
            if (err->diagnostic.ruleId == "CHANGESET_APPLY_FAILED") err->diagnostic.ruleId = "REDO_CONFLICT";
            return Envelope::failure("history.redo", *err);
        }
        std::string err;
        history_.markRedone(&err);
        redone.push_back(target->id);
        for (const auto& o : again.ops) ops.push_back(o.toJson());
    }
    Envelope env = Envelope::success("history.redo", Json{{"redone", redone}, {"cursor", history_.cursor()}});
    env.changes = std::move(ops);
    return env;
}

// ---------------------------------------------------------------- journal recovery (§9.2)

Json CommandBus::recoverJournal() {
    Json report = Json::array();
    for (const auto& cs : history_.pendingJournal()) {
        Json r = Json{{"changeSet", cs.id}, {"touched", cs.touched}};
        // 각 touched 문서: 디스크 == base 면 아직 안 써진 것 → 다시 적용해서 완료. 다르면 이미 써진 것으로 본다.
        bool allBase = true, anyBase = false;
        for (const auto& doc : cs.touched) {
            auto it = cs.base.find(doc);
            const Json* cur = project_.document(doc);
            if (it == cs.base.end()) { allBase = false; continue; }   // 새 문서 → base 없음
            bool same = cur && docHash(*cur) == it->get<std::string>();
            allBase = allBase && same;
            anyBase = anyBase || same;
        }
        if (allBase) {
            ChangeSet again = cs;
            std::vector<Diagnostic> diags;
            if (applyOps(project_.documentsMut(), again.ops, &diags)) {
                project_.reindex();
                std::string err;
                for (const auto& doc : again.touched) { if (project_.document(doc)) project_.saveDocument(doc, &err); }
                if (!history_.find(again.id)) history_.push(again, &err);
                refreshBaseHashes(again.touched);
                r["action"] = "completed";
            } else {
                r["action"] = "discarded";
                r["reason"] = "ops no longer apply";
            }
        } else if (!anyBase) {
            if (!history_.find(cs.id)) { std::string err; history_.push(cs, &err); }
            r["action"] = "already-applied";
        } else {
            r["action"] = "partial";
            r["reason"] = "some touched documents are at base, some are not — resolve by hand (Cache/journal/" + cs.id + ".json)";
            report.push_back(std::move(r));
            continue;   // journal 은 남긴다
        }
        history_.clearJournal(cs.id);
        report.push_back(std::move(r));
    }
    return report;
}

} // namespace akeir
