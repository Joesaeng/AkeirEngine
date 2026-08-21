// akeir/commands/CommandBus.h — 단일 Command API. 설계 문서 §8 (CommandKind, Command 레지스트리, ChangeBuilder), §8.1 (command id = <noun>.<verb>),
// §9 (Transaction: fork → execute → compose → commit), §9.2 (commit 절차), §10 (undo/redo = inverse(ops)), §49 (apply batch, $name 참조, idempotencyKey),
// §50 (dry-run = fork 에서 실행, 저장 없이 ChangeSet 반환), §78 (ChangeSet), §88.1 (Phase 3 tx 는 in-process).
//
//   CLI / Editor / MCP 는 전부 이 클래스만 호출한다. 파일을 직접 고치는 다른 엔진 경로는 없다 (`akeir fmt` 제외).
//
//   실행 모델 (Mutation):
//     1. fork = project 문서 맵의 복사본             (Query 는 fork 없이 project 를 읽는다)
//     2. handler(ctx) 가 ChangeBuilder 로 ops 를 emit — builder 가 fork 에 즉시 적용하므로 handler 는 자기 변경을 바로 읽는다
//     3. handler 가 실패하면 fork 를 버린다 (atomic)
//     4. dryRun 이면 ChangeSet 만 envelope.changes 로 돌려준다. 아니면 commit():
//        base 검사(디스크의 touched 문서 hash == 로드 시점 hash) → journal 기록 → project 에 ops 적용 → touched 문서 temp+rename 저장 → history push → journal 삭제
//   tx (in-process): beginTx() 가 fork 를 잡고, execute(…, {tx}) 는 그 fork 위에서 누적, commitTx() 가 ChangeSet::compose 로 하나의 history 항목으로 commit.
//   undo/redo: history 의 항목을 inverse() 해서 같은 commit 경로로 적용 (history 에는 push 하지 않고 cursor 만 움직인다).
#pragma once

#include "akeir/commands/ChangeSet.h"
#include "akeir/commands/History.h"
#include "akeir/core/Envelope.h"
#include "akeir/runtime/Project.h"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace akeir {

enum class CommandKind { Mutation, Query, RuntimeControl, Meta };
const char* commandKindName(CommandKind k);

/// 문서 맵 위에서 ops 를 기록하며 즉시 적용한다 (§8 ChangeBuilder). `before` 는 현재 값에서 자동으로 채운다.
class ChangeBuilder {
public:
    explicit ChangeBuilder(std::map<std::string, Json>& docs) : docs_(docs) {}
    /// Project 위에서: 매 op 뒤 reindex() 하므로 handler 가 locate()/resolveSelector() 로 자기 변경을 바로 본다
    explicit ChangeBuilder(Project& project) : docs_(project.documentsMut()), project_(&project) {}

    /// RFC 6902 add (객체 키 생성 또는 배열 삽입; 이미 있는 객체 키면 replace 로 기록한다 = "set" 의미)
    bool set(const std::string& doc, const std::string& path, Json value);
    bool add(const std::string& doc, const std::string& path, Json value);
    bool replace(const std::string& doc, const std::string& path, Json value);
    bool remove(const std::string& doc, const std::string& path);
    bool move(const std::string& doc, const std::string& from, const std::string& path);
    bool test(const std::string& doc, const std::string& path, Json value);
    /// 문서 생성/삭제 (path "")
    bool addDocument(const std::string& doc, Json value);
    bool removeDocument(const std::string& doc);

    const Json* get(const std::string& doc, const std::string& path) const;   // 없으면 nullptr
    bool exists(const std::string& doc, const std::string& path) const { return get(doc, path) != nullptr; }
    std::map<std::string, Json>& docs() { return docs_; }

    const std::vector<ChangeOp>& ops() const { return ops_; }
    std::vector<ChangeOp> take() { auto o = std::move(ops_); ops_.clear(); return o; }
    const std::vector<Diagnostic>& errors() const { return errors_; }

private:
    std::map<std::string, Json>& docs_;
    Project* project_ = nullptr;
    std::vector<ChangeOp> ops_;
    std::vector<Diagnostic> errors_;
    bool push(ChangeOp op);
};

struct CommandContext {
    Project& project;            // Mutation: fork (변경은 changes 로만) / Query: 실제 project
    const Json& args;
    ChangeBuilder& changes;
    std::string actor;
    Json result = Json::object();
    std::vector<Diagnostic> warnings;
    std::optional<CommandError> error;

    /// 실패 보고 (handler 는 return false 와 함께 사용)
    bool fail(ErrorCategory cat, std::string ruleId, std::string text, Json details = Json::object());
    bool fail(CommandError err) { error = std::move(err); return false; }
    bool failDiagnostic(ErrorCategory cat, Diagnostic d, Json details = Json::object());

    // ---- 공통 resolve 도우미 (§7.4 selector) ----
    /// entity selector → (doc, pointer "/entities/<id>", id). 실패하면 error 를 채우고 nullopt
    struct EntityRef { std::string doc; std::string pointer; std::string id; };
    std::optional<EntityRef> resolveEntity(const std::string& selector);
    std::optional<EntityRef> resolvePrefab(const std::string& selector);
    std::optional<std::string> resolveWorldDoc(const std::string& selectorOrEmpty);   // 비어 있으면 defaultWorld
};

struct CommandDef {
    std::string id;                       // "<noun>.<verb>"
    CommandKind kind = CommandKind::Mutation;
    std::string description;
    Json argsSchema = Json::object();      // JSON Schema (capabilities 에 노출)
    std::vector<std::string> aliases;     // BRP 이름 등 (§8.1)
    std::function<bool(CommandContext&)> handler;
};

struct BusOptions {
    std::string actor = "cli";
    bool persist = true;                  // false = 디스크에 쓰지 않는다 (테스트 / in-memory 전용)
};

struct ExecOptions {
    bool dryRun = false;
    std::string tx;                       // beginTx() 가 준 id
    std::string idempotencyKey;
    bool validateAfter = true;            // commit 전에 touched 문서 검증 (error 면 거부)
};

class CommandBus {
public:
    CommandBus(Project& project, BusOptions options = {});

    void registerCommand(CommandDef def);
    const CommandDef* find(std::string_view idOrAlias) const;
    const std::vector<CommandDef>& commands() const { return defs_; }
    Json commandsJson() const;            // capabilities.commands[]

    /// 한 command 실행. Mutation 은 위 실행 모델, Query/Meta 는 handler 결과를 그대로 envelope 으로.
    Envelope execute(const std::string& id, const Json& args, const ExecOptions& opts = {});
    /// §49 apply: { atomic?, dryRun?, idempotencyKey?, changes:[{op, ...args, as?}] }. atomic 이면 하나의 ChangeSet 으로 commit.
    Envelope apply(const Json& batch, const ExecOptions& opts = {});

    // ---- transaction (§9, §9.1: opaque handle + TTL) ----
    std::string beginTx(long long ttlMs = 600000);
    Json txInfo(const std::string& txId) const;     // {tx, ttlMs, expiresAt, commands}
    Json txList() const;
    /// 만료된 tx 를 버린다 (execute/commitTx 가 먼저 호출). 버린 id 목록.
    std::vector<std::string> expireTransactions();
    Envelope commitTx(const std::string& txId, const ExecOptions& opts = {});   // opts.validateAfter 만 본다
    Envelope rollbackTx(const std::string& txId);
    bool hasTx(const std::string& txId) const { return txs_.count(txId) > 0; }

    // ---- history (§10) ----
    Envelope undo(int steps = 1, const std::string& actorFilter = "");
    Envelope redo(int steps = 1);
    Json historyJson(std::size_t limit = 20) const { return history_.listJson(limit); }
    History& history() { return history_; }
    /// §9.2 crash 복구: journal 에 남은 ChangeSet 을 검사해 완료하거나 버린다. 처리 내역을 돌려준다.
    Json recoverJournal();

    Project& project() { return project_; }
    const BusOptions& options() const { return options_; }
    void setActor(std::string actor) { options_.actor = std::move(actor); }   // serve: 요청마다 호출자의 actor 로 (§10 actor 태깅)

private:
    struct Tx {
        Project fork;
        std::vector<ChangeSet> parts;
        Json results = Json::array();
        std::chrono::steady_clock::time_point createdAt{};
        std::chrono::steady_clock::time_point expiresAt{};
        long long ttlMs = 0;
    };

    Project& project_;
    BusOptions options_;
    History history_;
    std::vector<CommandDef> defs_;
    std::map<std::string, Tx> txs_;
    std::map<std::string, std::string> baseHashes_;   // doc → sha256 (로드 시점). commit 때 디스크와 비교

    /// fork 위에서 handler 실행 → ChangeSet(ops, intent). 실패 시 envelope 을 돌려준다.
    std::optional<Envelope> runMutation(const CommandDef& def, const Json& args, Project& fork,
                                        ChangeSet& outCs, Json& outResult, std::vector<Diagnostic>& outWarnings);
    /// commit 절차 (§9.2). 성공하면 cs.id/createdAt/base 가 채워진다.
    std::optional<CommandError> commit(ChangeSet& cs, bool pushHistory);
    std::optional<CommandError> validateFork(const Project& fork, const std::vector<std::string>& touched);
    std::set<std::string> baselineErrorFingerprints_;   // commit 전 검증: 이미 있던 오류는 새 오류로 세지 않는다
    bool baselineComputed_ = false;
    void refreshBaseHashes(const std::vector<std::string>& docs);
    static std::string docHash(const Json& doc);
    Envelope finish(Envelope env, const ChangeSet* cs, const ExecOptions& opts);
};

/// 내장 Mutation/Query command 를 등록한다 (entity.*, component.*, property.set, prefab.*, world.create, tag.*). CommandBus 생성자가 호출.
void registerBuiltinCommands(CommandBus& bus);

/// 형제 뒤에 오는 fractional-index order key (§6). "a0" → "a1" … "a9" → "aa" … "az" → "b00"
std::string nextOrderKey(std::string_view lastKey);

} // namespace akeir
