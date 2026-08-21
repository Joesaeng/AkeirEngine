// History.cpp — §9.2 journal, §10 history/cursor, §49 idempotency 캐시
#include "akeir/commands/History.h"

#include "akeir/serialization/Canonical.h"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace akeir {

History::History(std::string projectRoot) : root_(std::move(projectRoot)) {}

std::string History::historyDir() const { return (fs::path(root_) / "Cache" / "history").string(); }
std::string History::journalDir() const { return (fs::path(root_) / "Cache" / "journal").string(); }

namespace {

bool writeTextAtomic(const fs::path& path, const std::string& text, std::string* error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) { if (error) *error = "cannot open " + tmp.string(); return false; }
        out << text;
        if (!out) { if (error) *error = "write failed " + tmp.string(); return false; }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(path, ec);
        fs::rename(tmp, path, ec);
        if (ec) { if (error) *error = "rename failed: " + ec.message(); return false; }
    }
    return true;
}

std::optional<std::string> readText(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

bool History::load(std::string* error) {
    entries_.clear();
    cursor_ = 0;
    fs::path dir = historyDir();
    if (auto text = readText(dir / "history.jsonl")) {
        std::istringstream in(*text);
        std::string line;
        std::size_t lineNo = 0;
        while (std::getline(in, line)) {
            ++lineNo;
            if (line.empty()) continue;
            std::string err;
            auto j = parseJson(line, &err);
            if (!j) { if (error) *error = "history.jsonl line " + std::to_string(lineNo) + ": " + err; return false; }
            auto cs = ChangeSet::fromJson(*j);
            if (!cs) { if (error) *error = "history.jsonl line " + std::to_string(lineNo) + ": not a ChangeSet"; return false; }
            entries_.push_back(std::move(*cs));
        }
    }
    cursor_ = entries_.size();
    if (auto text = readText(dir / "cursor.json")) {
        if (auto j = parseJson(*text)) {
            if (j->is_object() && j->contains("cursor") && (*j)["cursor"].is_number_unsigned()) {
                auto c = (*j)["cursor"].get<std::size_t>();
                if (c <= entries_.size()) cursor_ = c;
            }
        }
    }
    return true;
}

bool History::writeAll(std::string* error) const {
    std::string text;
    for (const auto& cs : entries_) {
        text += jcsDump(cs.toJson());
        text += '\n';
    }
    return writeTextAtomic(fs::path(historyDir()) / "history.jsonl", text, error) && writeCursor(error);
}

bool History::writeCursor(std::string* error) const {
    Json c = Json::object();
    c["cursor"] = cursor_;
    c["entries"] = entries_.size();
    return writeTextAtomic(fs::path(historyDir()) / "cursor.json", jcsDump(c) + "\n", error);
}

bool History::push(const ChangeSet& cs, std::string* error) {
    bool truncated = cursor_ < entries_.size();
    if (truncated) entries_.resize(cursor_);
    entries_.push_back(cs);
    cursor_ = entries_.size();
    if (truncated) return writeAll(error);
    // append-only fast path
    fs::path p = fs::path(historyDir()) / "history.jsonl";
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    {
        std::ofstream out(p, std::ios::binary | std::ios::app);
        if (!out) { if (error) *error = "cannot append " + p.string(); return false; }
        out << jcsDump(cs.toJson()) << '\n';
        if (!out) { if (error) *error = "append failed " + p.string(); return false; }
    }
    return writeCursor(error);
}

bool History::markUndone(std::string* error) {
    if (!canUndo()) { if (error) *error = "nothing to undo"; return false; }
    --cursor_;
    return writeCursor(error);
}

bool History::markRedone(std::string* error) {
    if (!canRedo()) { if (error) *error = "nothing to redo"; return false; }
    ++cursor_;
    return writeCursor(error);
}

Json History::listJson(std::size_t limit) const {
    Json arr = Json::array();
    std::size_t n = entries_.size();
    std::size_t start = n > limit ? n - limit : 0;
    for (std::size_t i = start; i < n; ++i) {
        const auto& cs = entries_[i];
        Json e = Json::object();
        e["index"] = i;
        e["id"] = cs.id;
        if (!cs.tx.empty()) e["tx"] = cs.tx;
        e["actor"] = cs.actor;
        e["createdAt"] = cs.createdAt;
        e["applied"] = i < cursor_;
        e["intent"] = cs.intent.is_array() ? Json(cs.intent.size() > 0 ? cs.intent[0].value("op", "") + (cs.intent.size() > 1 ? " +" + std::to_string(cs.intent.size() - 1) : "") : "")
                                           : Json(cs.intent.value("op", ""));
        e["touched"] = cs.touched;
        e["summary"] = cs.summary();
        arr.push_back(std::move(e));
    }
    Json out = Json::object();
    out["entries"] = std::move(arr);
    out["cursor"] = cursor_;
    out["total"] = n;
    out["canUndo"] = canUndo();
    out["canRedo"] = canRedo();
    return out;
}

const ChangeSet* History::find(std::string_view id) const {
    for (const auto& e : entries_) if (e.id == id) return &e;
    return nullptr;
}

// ---- journal ----

bool History::writeJournal(const ChangeSet& cs, std::string* error) {
    return writeTextAtomic(fs::path(journalDir()) / (cs.id + ".json"), jcsDump(cs.toJson()) + "\n", error);
}

void History::clearJournal(std::string_view csId) {
    std::error_code ec;
    fs::remove(fs::path(journalDir()) / (std::string(csId) + ".json"), ec);
}

std::vector<ChangeSet> History::pendingJournal() const {
    std::vector<ChangeSet> out;
    std::error_code ec;
    fs::path dir = journalDir();
    if (!fs::is_directory(dir, ec)) return out;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir, ec)) if (e.path().extension() == ".json") files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        auto text = readText(f);
        if (!text) continue;
        if (auto j = parseJson(*text)) if (auto cs = ChangeSet::fromJson(*j)) out.push_back(std::move(*cs));
    }
    return out;
}

// ---- idempotency ----

std::optional<Json> History::idempotent(std::string_view key) const {
    auto text = readText(fs::path(historyDir()) / "idempotency.json");
    if (!text) return std::nullopt;
    auto j = parseJson(*text);
    if (!j || !j->is_object()) return std::nullopt;
    auto it = j->find(std::string(key));
    if (it == j->end()) return std::nullopt;
    return *it;
}

bool History::rememberIdempotent(std::string_view key, const Json& envelope, std::string* error) {
    Json all = Json::object();
    if (auto text = readText(fs::path(historyDir()) / "idempotency.json")) if (auto j = parseJson(*text); j && j->is_object()) all = *j;
    all[std::string(key)] = envelope;
    return writeTextAtomic(fs::path(historyDir()) / "idempotency.json", jcsDump(all) + "\n", error);
}

} // namespace akeir
