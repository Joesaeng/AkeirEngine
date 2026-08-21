// ChangeSet.cpp — §78 ChangeSet 직렬화 / inverse / compose / applyOps
#include "akeir/commands/ChangeSet.h"

#include "akeir/serialization/Canonical.h"

#include <algorithm>
#include <set>

namespace akeir {

std::string escapeToken(std::string_view token) {
    std::string out;
    out.reserve(token.size());
    for (char c : token) {
        if (c == '~') out += "~0";
        else if (c == '/') out += "~1";
        else out += c;
    }
    return out;
}

std::string unescapeToken(std::string_view token) {
    std::string out;
    out.reserve(token.size());
    for (std::size_t i = 0; i < token.size(); ++i) {
        if (token[i] == '~' && i + 1 < token.size()) {
            if (token[i + 1] == '0') { out += '~'; ++i; continue; }
            if (token[i + 1] == '1') { out += '/'; ++i; continue; }
        }
        out += token[i];
    }
    return out;
}

// ---------------------------------------------------------------- ChangeOp

Json ChangeOp::toJson() const {
    Json j = Json::object();
    j["op"] = op;
    j["doc"] = doc;
    j["path"] = path;
    if (op == "move") j["from"] = from;
    if (op == "add" || op == "replace" || op == "test") j["value"] = value;
    if (op == "remove" || op == "replace" || op == "move") j["before"] = before;
    if (isFileOp()) {
        if (!blob.empty()) j["blob"] = blob;
        if (!beforeBlob.empty()) j["beforeBlob"] = beforeBlob;
    }
    return j;
}

std::optional<ChangeOp> ChangeOp::fromJson(const Json& j) {
    if (!j.is_object() || !j.contains("op") || !j["op"].is_string()) return std::nullopt;
    ChangeOp o;
    o.op = j["op"].get<std::string>();
    o.doc = j.value("doc", "");
    o.path = j.value("path", "");
    o.from = j.value("from", "");
    if (j.contains("value")) o.value = j["value"];
    if (j.contains("before")) o.before = j["before"];
    o.blob = j.value("blob", "");
    o.beforeBlob = j.value("beforeBlob", "");
    return o;
}

Json ChangeOp::toRfc6902() const {
    Json j = Json::object();
    j["op"] = op;
    j["path"] = path;
    if (op == "move") j["from"] = from;
    if (op == "add" || op == "replace" || op == "test") j["value"] = value;
    return j;
}

// ---------------------------------------------------------------- ChangeSet

Json ChangeSet::toJson() const {
    Json j = Json::object();
    j["changeSetVersion"] = changeSetVersion;
    j["id"] = id;
    if (!tx.empty()) j["tx"] = tx;
    j["actor"] = actor;
    j["createdAt"] = createdAt;
    j["intent"] = intent;
    j["base"] = base;
    Json opsJ = Json::array();
    for (const auto& o : ops) opsJ.push_back(o.toJson());
    j["ops"] = std::move(opsJ);
    j["touched"] = touched;
    j["summary"] = summary();
    j["lossy"] = lossy;
    if (!diagnostics.empty()) {
        Json d = Json::array();
        for (const auto& x : diagnostics) d.push_back(x.toJson());
        j["diagnostics"] = std::move(d);
    }
    return j;
}

std::optional<ChangeSet> ChangeSet::fromJson(const Json& j) {
    if (!j.is_object() || !j.contains("ops") || !j["ops"].is_array()) return std::nullopt;
    ChangeSet cs;
    cs.changeSetVersion = j.value("changeSetVersion", 1);
    cs.id = j.value("id", "");
    cs.tx = j.value("tx", "");
    cs.actor = j.value("actor", "");
    cs.createdAt = j.value("createdAt", "");
    if (j.contains("intent")) cs.intent = j["intent"];
    if (j.contains("base")) cs.base = j["base"];
    for (const auto& oj : j["ops"]) {
        auto o = ChangeOp::fromJson(oj);
        if (!o) return std::nullopt;
        cs.ops.push_back(std::move(*o));
    }
    cs.lossy = j.value("lossy", false);
    if (j.contains("diagnostics") && j["diagnostics"].is_array())
        for (const auto& d : j["diagnostics"]) if (auto x = Diagnostic::fromJson(d)) cs.diagnostics.push_back(*x);
    cs.finalize();
    return cs;
}

void ChangeSet::finalize() {
    std::set<std::string> t;
    for (const auto& o : ops) t.insert(o.doc);
    touched.assign(t.begin(), t.end());
}

Json ChangeSet::summary() const {
    Json s = Json::object();
    int add = 0, remove = 0, replace = 0, move = 0, test = 0, file = 0;
    for (const auto& o : ops) {
        if (o.isFileOp()) ++file;
        else if (o.op == "add") ++add;
        else if (o.op == "remove") ++remove;
        else if (o.op == "replace") ++replace;
        else if (o.op == "move") ++move;
        else if (o.op == "test") ++test;
    }
    s["ops"] = static_cast<int>(ops.size());
    s["add"] = add; s["remove"] = remove; s["replace"] = replace; s["move"] = move; s["test"] = test; s["file"] = file;
    s["docs"] = static_cast<int>(touched.size());
    return s;
}

ChangeSet ChangeSet::inverse() const {
    ChangeSet inv;
    inv.actor = actor;
    inv.lossy = lossy;
    inv.intent = Json{{"op", "history.undo"}, {"of", id}};
    for (auto it = ops.rbegin(); it != ops.rend(); ++it) {
        const ChangeOp& o = *it;
        ChangeOp r;
        r.doc = o.doc;
        if (o.op == "add") { r.op = "remove"; r.path = o.path; r.before = o.value; }
        else if (o.op == "remove") { r.op = "add"; r.path = o.path; r.value = o.before; }
        else if (o.op == "replace") { r.op = "replace"; r.path = o.path; r.value = o.before; r.before = o.value; }
        else if (o.op == "move") { r.op = "move"; r.from = o.path; r.path = o.from; r.before = o.before; }
        else if (o.op == "test") { r.op = "test"; r.path = o.path; r.value = o.value; }
        else if (o.op == "file.add") { r.op = "file.remove"; r.path = o.path; r.beforeBlob = o.blob; }
        else if (o.op == "file.remove") { r.op = "file.add"; r.path = o.path; r.blob = o.beforeBlob; }
        else if (o.op == "file.replace") { r.op = "file.replace"; r.path = o.path; r.blob = o.beforeBlob; r.beforeBlob = o.blob; }
        else { r = o; inv.lossy = true; }
        inv.ops.push_back(std::move(r));
    }
    inv.finalize();
    return inv;
}

ChangeSet ChangeSet::compose(const std::vector<ChangeSet>& parts, const std::string& txId) {
    ChangeSet out;
    out.tx = txId;
    out.intent = Json::array();
    for (const auto& p : parts) {
        if (out.actor.empty()) out.actor = p.actor;
        out.intent.push_back(p.intent);
        for (const auto& [doc, h] : p.base.items()) if (!out.base.contains(doc)) out.base[doc] = h;  // 첫 번째(가장 이른) base 유지
        out.ops.insert(out.ops.end(), p.ops.begin(), p.ops.end());
        out.lossy = out.lossy || p.lossy;
        out.diagnostics.insert(out.diagnostics.end(), p.diagnostics.begin(), p.diagnostics.end());
    }
    out.finalize();
    return out;
}

// ---------------------------------------------------------------- applyOps

namespace {

bool fail(std::vector<Diagnostic>* diags, const std::string& rule, const std::string& msg, const ChangeOp& o) {
    if (diags) diags->push_back(Diagnostic::error(rule, msg).in(PhysicalLocation{o.doc, o.path, std::nullopt}));
    return false;
}

} // namespace

bool applyOps(std::map<std::string, Json>& docs, const std::vector<ChangeOp>& ops, std::vector<Diagnostic>* diagnostics) {
    for (const auto& o : ops) {
        if (o.isFileOp()) return fail(diagnostics, "CHANGESET_FILE_OP_UNSUPPORTED", "file.* ops need an asset store (Phase 2+); cannot apply '" + o.op + "'.", o);
        if (o.op != "add" && o.op != "remove" && o.op != "replace" && o.op != "move" && o.op != "test")
            return fail(diagnostics, "CHANGESET_OP_UNKNOWN", "Unknown op '" + o.op + "'.", o);

        // 문서 전체를 add 하는 경우 (path "") — 새 문서 생성
        if (o.path.empty() && o.op == "add") {
            if (docs.count(o.doc)) return fail(diagnostics, "CHANGESET_DOC_EXISTS", "Document already exists.", o);
            docs[o.doc] = o.value;
            continue;
        }
        auto it = docs.find(o.doc);
        if (it == docs.end()) return fail(diagnostics, "CHANGESET_DOC_MISSING", "Document '" + o.doc + "' is not loaded.", o);
        if (o.path.empty() && o.op == "remove") { docs.erase(it); continue; }
        if (o.path.empty() && o.op == "replace") { it->second = o.value; continue; }

        try {
            Json patch = Json::array({o.toRfc6902()});
            if (o.op == "remove" || o.op == "replace") {
                // self-inverting 보장: before 가 실제 값과 같아야 한다 (§78 규칙 2, §10.2 conflict 검출)
                // 값 비교는 JCS 로 (키 순서 무시): 파일로 갔다 온 문서는 §5.3 순서로 재정렬되어 있어 ordered_json == 가 실패한다
                const Json& cur = it->second.at(Json::json_pointer(o.path));
                if (!o.before.is_null() && cur != o.before && jcsDump(cur) != jcsDump(o.before))
                    return fail(diagnostics, "CHANGESET_BEFORE_MISMATCH", "Current value differs from op.before (concurrent edit?).", o);
            }
            it->second.patch_inplace(patch);   // 문서 객체를 통째로 바꾸지 않는다 (큰 world 에서 복사 방지)
        } catch (const std::exception& e) {
            return fail(diagnostics, "CHANGESET_APPLY_FAILED", std::string("Patch failed: ") + e.what(), o);
        }
    }
    return true;
}

} // namespace akeir
