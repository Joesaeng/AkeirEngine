// akeir/runtime/Project.cpp — 설계 문서 §5.3, §6, §7, §19, §29, §34
#include "akeir/runtime/Project.h"
#include "akeir/core/Ref.h"
#include "akeir/reflection/Registry.h"
#include "akeir/serialization/Canonical.h"
#include "akeir/serialization/ComponentJson.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>

namespace fs = std::filesystem;

namespace akeir {

namespace {

const char* kWorldSchema = "game://schema/world/1";
const char* kPrefabSchema = "game://schema/prefab/1";
const char* kProjectSchema = "game://schema/project/1";
constexpr int kMaxPrefabDepth = 16;

std::string escapePointerToken(std::string_view s) {
    std::string out;
    for (char c : s) { if (c == '~') out += "~0"; else if (c == '/') out += "~1"; else out += c; }
    return out;
}

bool isInstance(const Json& obj) { return obj.contains("prefab") || obj.contains("base"); }

Diagnostic docError(const std::string& rule, const std::string& text, const std::string& doc, const std::string& pointer, const std::string& object = "") {
    Diagnostic d = Diagnostic::error(rule, text).in(PhysicalLocation{doc, pointer, std::nullopt});
    if (!object.empty()) d.at(LogicalLocation{object, std::nullopt, std::nullopt});
    return d;
}

} // namespace

const std::vector<std::string>& headerKeyOrder() {
    static const std::vector<std::string> k = {"$schema", "schemaVersion", "id", "name", "description", "notes", "tags",
                                               "tickRate", "seed", "defaultWorld", "writable",
                                               "base", "prefab", "parent", "order", "set", "add", "remove", "components", "entities"};
    return k;
}

// ---------------------------------------------------------------- load

void Project::loadDirectory(const std::string& subdir, const std::string& suffix, const std::string& kind, std::vector<Diagnostic>& diags) {
    fs::path dir = fs::path(rootDir_) / subdir;
    if (!fs::exists(dir)) return;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file() && e.path().filename().string().size() > suffix.size() &&
            e.path().filename().string().compare(e.path().filename().string().size() - suffix.size(), suffix.size(), suffix) == 0)
            files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const auto& f : files) {
        std::string rel = (fs::path(subdir) / f.filename()).generic_string();
        std::string err;
        auto j = readJsonFile(f.string(), &err);
        if (!j) { diags.push_back(docError("JSON_PARSE_ERROR", "Cannot parse " + rel + ": " + err, rel, "")); continue; }
        if (!j->is_object()) { diags.push_back(docError("DOCUMENT_NOT_OBJECT", rel + " must be a JSON object.", rel, "")); continue; }
        int sv = j->value("schemaVersion", 1);
        if (sv > kProjectSchemaVersion)
            diags.push_back(docError("SCHEMA_VERSION_NEWER_THAN_ENGINE", rel + " has schemaVersion " + std::to_string(sv) + " but this engine supports " + std::to_string(kProjectSchemaVersion) + ". Refusing best-effort load (§53).", rel, "/schemaVersion"));
        (void)kind;
        docs_[rel] = std::move(*j);
    }
}

std::optional<Project> Project::load(const std::string& rootDir, std::vector<Diagnostic>& diagnostics) {
    fs::path root = fs::absolute(rootDir);
    fs::path pj = root / "project.json";
    std::string err;
    auto j = readJsonFile(pj.string(), &err);
    if (!j) {
        diagnostics.push_back(Diagnostic::error("PROJECT_NOT_FOUND", "No readable project.json in " + root.string() + " (" + err + ").").in(PhysicalLocation{"project.json", "", std::nullopt}));
        return std::nullopt;
    }
    Project p;
    p.rootDir_ = root.generic_string();
    p.projectJson_ = *j;
    int sv = p.projectJson_.value("schemaVersion", 1);
    if (sv > kProjectSchemaVersion)
        diagnostics.push_back(docError("SCHEMA_VERSION_NEWER_THAN_ENGINE", "project.json schemaVersion " + std::to_string(sv) + " is newer than engine " + std::to_string(kProjectSchemaVersion) + ".", "project.json", "/schemaVersion"));
    p.loadDirectory("Worlds", ".world.json", "world", diagnostics);
    p.loadDirectory("Prefabs", ".prefab.json", "prefab", diagnostics);
    p.reindex();
    return p;
}

Project Project::create(const std::string& rootDir, const std::string& name, int tickRate) {
    Project p;
    p.rootDir_ = fs::absolute(rootDir).generic_string();
    p.projectJson_ = Json::object();
    p.projectJson_["$schema"] = kProjectSchema;
    p.projectJson_["schemaVersion"] = kProjectSchemaVersion;
    p.projectJson_["name"] = name;
    p.projectJson_["tickRate"] = tickRate;
    p.projectJson_["seed"] = 0;
    p.projectJson_["writable"] = Json::array({"Worlds/**", "Prefabs/**", "Data/**", "Config/**", "Assets/**", "Tests/**", "Source/**"});
    return p;
}

std::optional<std::string> Project::defaultWorld() const {
    if (projectJson_.contains("defaultWorld") && projectJson_["defaultWorld"].is_string()) return projectJson_["defaultWorld"].get<std::string>();
    auto w = worldPaths();
    if (w.empty()) return std::nullopt;
    const Json& d = docs_.at(w.front());
    return d.value("id", "");
}

// ---------------------------------------------------------------- documents / index

const Json* Project::document(std::string_view path) const {
    auto it = docs_.find(std::string(path));
    return it == docs_.end() ? nullptr : &it->second;
}
Json* Project::documentMut(std::string_view path) {
    auto it = docs_.find(std::string(path));
    return it == docs_.end() ? nullptr : &it->second;
}
void Project::setDocument(const std::string& path, Json doc) { docs_[path] = std::move(doc); reindex(); }
bool Project::removeDocument(std::string_view path) {
    auto it = docs_.find(std::string(path));
    if (it == docs_.end()) return false;
    docs_.erase(it);
    reindex();
    return true;
}

std::vector<std::string> Project::worldPaths() const {
    std::vector<std::string> out;
    for (const auto& [p, d] : docs_) if (p.rfind("Worlds/", 0) == 0) out.push_back(p);
    return out;
}
std::vector<std::string> Project::prefabPaths() const {
    std::vector<std::string> out;
    for (const auto& [p, d] : docs_) if (p.rfind("Prefabs/", 0) == 0) out.push_back(p);
    return out;
}

void Project::reindex() {
    index_.clear();
    duplicates_.clear();
    auto put = [&](const std::string& id, DocLocation loc) {
        auto it = index_.find(id);
        if (it != index_.end()) { duplicates_[id].push_back(it->second.doc + "#" + it->second.pointer); duplicates_[id].push_back(loc.doc + "#" + loc.pointer); return; }
        index_[id] = std::move(loc);
    };
    for (const auto& [path, doc] : docs_) {
        if (!doc.is_object()) continue;
        bool isWorld = path.rfind("Worlds/", 0) == 0;
        if (doc.contains("id") && doc["id"].is_string()) put(doc["id"].get<std::string>(), DocLocation{path, "", isWorld ? "world" : "prefab"});
        if (isWorld && doc.contains("entities") && doc["entities"].is_object())
            for (auto it = doc["entities"].begin(); it != doc["entities"].end(); ++it)
                put(it.key(), DocLocation{path, "/entities/" + escapePointerToken(it.key()), "entity"});
    }
}

std::optional<DocLocation> Project::locate(std::string_view id) const {
    auto it = index_.find(id);
    if (it == index_.end()) return std::nullopt;
    return it->second;
}

std::optional<std::string> Project::entityPath(std::string_view entityId) const {
    auto loc = locate(entityId);
    if (!loc || loc->kind != "entity") return std::nullopt;
    const Json& world = docs_.at(loc->doc);
    const Json& ents = world["entities"];
    std::vector<std::string> parts;
    std::string cur(entityId);
    std::set<std::string> seen;
    while (!cur.empty() && ents.contains(cur) && !seen.count(cur)) {
        seen.insert(cur);
        const Json& e = ents[cur];
        parts.push_back(e.value("name", cur));
        if (e.contains("parent") && e["parent"].is_string()) cur = e["parent"].get<std::string>(); else break;
    }
    std::string out = world.value("name", "");
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) out += "/" + *it;
    return out;
}

std::vector<std::string> Project::resolveSelector(std::string_view selector) const {
    std::vector<std::string> out;
    if (selector.rfind("name:", 0) == 0) {
        std::string_view name = selector.substr(5);
        for (const auto& [id, loc] : index_) {
            const Json& doc = docs_.at(loc.doc);
            const Json* obj = loc.pointer.empty() ? &doc : &doc.at(JsonPointer(loc.pointer));
            if (obj->value("name", "") == name) out.push_back(id);
        }
        return out;
    }
    if (selector.rfind("path:", 0) == 0) {
        std::string_view want = selector.substr(5);
        for (const auto& [id, loc] : index_) {
            if (loc.kind != "entity") continue;
            auto p = entityPath(id);
            if (!p) continue;
            if (*p == want) out.push_back(id);
            else { // world 이름 생략 허용
                auto slash = p->find('/');
                if (slash != std::string::npos && std::string_view(*p).substr(slash + 1) == want) out.push_back(id);
            }
        }
        return out;
    }
    // id 또는 id 의 고유 prefix
    std::string s(selector);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (index_.count(s)) { out.push_back(s); return out; }
    for (const auto& [id, loc] : index_) if (id.rfind(s, 0) == 0 && s.find('_') != std::string::npos && s.size() > s.find('_') + 1) out.push_back(id);
    if (!out.empty()) return out;
    // ▶ v3: 접두어 없는 문자열이 id 와 맞지 않으면 이름으로 본다 ("Goblin_01" == "name:Goblin_01"). 여러 개면 호출자가 AMBIGUOUS_SELECTOR 를 낸다
    if (selector.find(':') == std::string_view::npos) return resolveSelector(std::string("name:") + std::string(selector));
    return out;
}

// ---------------------------------------------------------------- prefab resolve (§34)

bool Project::applyOverrides(Json& components, const Json& container, const std::string& docPath, const std::string& basePointer,
                             std::vector<Diagnostic>* diags, const std::string& objectId) {
    bool ok = true;
    // 순서: remove → add → set (체인 순서 안에서는 한 문서의 세 키를 이 순서로)
    Json root = Json::object();
    root["components"] = components;
    auto fail = [&](const std::string& rule, const std::string& text, const std::string& key) {
        ok = false;
        if (diags) diags->push_back(docError(rule, text, docPath, basePointer + "/" + key, objectId));
    };
    if (container.contains("remove")) {
        if (!container["remove"].is_array()) fail("PREFAB_OVERRIDE_INVALID", "'remove' must be an array of JSON pointers.", "remove");
        else for (const auto& p : container["remove"]) {
            if (!p.is_string()) { fail("PREFAB_OVERRIDE_INVALID", "'remove' entries must be strings.", "remove"); continue; }
            JsonPointer ptr(p.get<std::string>());
            if (!root.contains(ptr)) { fail("PREFAB_OVERRIDE_TARGET_MISSING", "remove target " + p.get<std::string>() + " does not exist in the resolved base.", "remove"); continue; }
            Json patch = Json::array({Json{{"op", "remove"}, {"path", p.get<std::string>()}}});
            root = root.patch(patch);
        }
    }
    if (container.contains("add")) {
        if (!container["add"].is_object()) fail("PREFAB_OVERRIDE_INVALID", "'add' must be an object {pointer: value}.", "add");
        else for (auto it = container["add"].begin(); it != container["add"].end(); ++it) {
            JsonPointer ptr(it.key());
            if (root.contains(ptr)) { fail("PREFAB_OVERRIDE_TARGET_EXISTS", "add target " + it.key() + " already exists; use 'set'.", "add/" + escapePointerToken(it.key())); continue; }
            Json patch = Json::array({Json{{"op", "add"}, {"path", it.key()}, {"value", it.value()}}});
            try { root = root.patch(patch); } catch (const std::exception& e) { fail("PREFAB_OVERRIDE_INVALID", std::string("add failed: ") + e.what(), "add"); }
        }
    }
    if (container.contains("set")) {
        if (!container["set"].is_object()) fail("PREFAB_OVERRIDE_INVALID", "'set' must be an object {pointer: value}.", "set");
        else for (auto it = container["set"].begin(); it != container["set"].end(); ++it) {
            JsonPointer ptr(it.key());
            if (!root.contains(ptr)) {
                Diagnostic d = docError("PREFAB_OVERRIDE_TARGET_MISSING", "set target " + it.key() + " does not exist in the resolved base (use 'add' to introduce it).", docPath, basePointer + "/set/" + escapePointerToken(it.key()), objectId);
                Fix f; f.description = "Move this override from 'set' to 'add'"; f.applicability = Applicability::MaybeIncorrect;
                f.artifactChanges.push_back(Json{{"op", "move"}, {"from", basePointer + "/set/" + escapePointerToken(it.key())}, {"path", basePointer + "/add/" + escapePointerToken(it.key())}});
                d.withFix(f);
                if (diags) diags->push_back(std::move(d));
                ok = false;
                continue;
            }
            root[ptr] = it.value();
        }
    }
    components = root["components"];
    return ok;
}

Json Project::resolvePrefabRec(std::string_view prefabId, std::vector<std::string>& chain, std::vector<Diagnostic>* diags, bool& ok) const {
    std::string id(prefabId);
    auto loc = locate(id);
    if (!loc || loc->kind != "prefab") {
        ok = false;
        if (diags) diags->push_back(Diagnostic::error("PREFAB_NOT_FOUND", "Prefab " + id + " does not exist.").at(LogicalLocation{id, std::nullopt, std::nullopt}));
        return Json::object();
    }
    if (std::find(chain.begin(), chain.end(), id) != chain.end()) {
        ok = false;
        if (diags) diags->push_back(docError("PREFAB_CHAIN_CYCLE", "Prefab inheritance cycle: " + id + " is its own ancestor.", loc->doc, "/base", id));
        return Json::object();
    }
    if (chain.size() >= kMaxPrefabDepth) {
        ok = false;
        if (diags) diags->push_back(docError("PREFAB_CHAIN_TOO_DEEP", "Prefab chain deeper than " + std::to_string(kMaxPrefabDepth) + ".", loc->doc, "/base", id));
        return Json::object();
    }
    chain.push_back(id);
    const Json& doc = docs_.at(loc->doc);
    Json components;
    if (doc.contains("base") && doc["base"].is_string()) {
        components = resolvePrefabRec(doc["base"].get<std::string>(), chain, diags, ok);
        if (!applyOverrides(components, doc, loc->doc, "", diags, id)) ok = false;
    } else {
        components = doc.value("components", Json::object());
    }
    chain.pop_back();
    return components;
}

std::optional<Json> Project::resolvePrefab(std::string_view prefabId, std::vector<Diagnostic>* diagnostics) const {
    std::vector<std::string> chain;
    bool ok = true;
    Json c = resolvePrefabRec(prefabId, chain, diagnostics, ok);
    if (!ok) return std::nullopt;
    return c;
}

std::optional<Json> Project::resolveEntityComponents(std::string_view entityId, std::vector<Diagnostic>* diagnostics) const {
    auto loc = locate(entityId);
    if (!loc || loc->kind != "entity") {
        if (diagnostics) diagnostics->push_back(Diagnostic::error("ENTITY_NOT_FOUND", "Entity " + std::string(entityId) + " does not exist.").at(LogicalLocation{std::string(entityId), std::nullopt, std::nullopt}));
        return std::nullopt;
    }
    const Json& e = docs_.at(loc->doc).at(JsonPointer(loc->pointer));
    if (e.contains("prefab") && e["prefab"].is_string()) {
        auto base = resolvePrefab(e["prefab"].get<std::string>(), diagnostics);
        if (!base) return std::nullopt;
        Json components = *base;
        if (!applyOverrides(components, e, loc->doc, loc->pointer, diagnostics, std::string(entityId))) return std::nullopt;
        return components;
    }
    return e.value("components", Json::object());
}

// ---------------------------------------------------------------- canonical (§5.3)

Json Project::canonicalizeDocument(const Json& doc) {
    if (!doc.is_object()) return doc;
    std::function<Json(const Json&)> canonObj;
    auto canonComponents = [&](const Json& comps) {
        Json out = Json::object();
        std::vector<std::string> names;
        for (auto it = comps.begin(); it != comps.end(); ++it) names.push_back(it.key());
        std::sort(names.begin(), names.end());                      // component A→Z
        for (const auto& n : names) {
            const Json& c = comps[n];
            const ComponentMeta* meta = Registry::global().find(n);
            if (!meta || !c.is_object()) { out[n] = canonicalizeFloats(c); continue; }
            Json cc = Json::object();
            for (const auto& p : meta->props) if (c.contains(p.name)) cc[p.name] = canonicalizeFloats(c[p.name]);   // 선언 순서
            for (auto it = c.begin(); it != c.end(); ++it) if (!cc.contains(it.key())) cc[it.key()] = canonicalizeFloats(it.value()); // 미지 키는 뒤에 (validate 가 잡는다)
            out[n] = cc;
        }
        return out;
    };
    canonObj = [&](const Json& obj) -> Json {
        Json out = Json::object();
        // 1) 헤더 키 고정 순서
        for (const auto& k : headerKeyOrder()) {
            if (!obj.contains(k)) continue;
            const Json& v = obj[k];
            if (k == "components" && v.is_object()) out[k] = canonComponents(v);
            else if (k == "entities" && v.is_object()) {
                std::vector<std::string> ids;
                for (auto it = v.begin(); it != v.end(); ++it) ids.push_back(it.key());
                std::sort(ids.begin(), ids.end());                  // entity id 순
                Json ents = Json::object();
                for (const auto& id : ids) ents[id] = canonObj(v[id]);
                out[k] = ents;
            } else if ((k == "set" || k == "add") && v.is_object()) {
                std::vector<std::string> ptrs;
                for (auto it = v.begin(); it != v.end(); ++it) ptrs.push_back(it.key());
                std::sort(ptrs.begin(), ptrs.end());
                Json o = Json::object();
                for (const auto& p : ptrs) o[p] = canonicalizeFloats(v[p]);
                out[k] = o;
            } else if (k == "remove" && v.is_array()) {
                std::vector<std::string> ptrs;
                for (const auto& e : v) if (e.is_string()) ptrs.push_back(e.get<std::string>());
                std::sort(ptrs.begin(), ptrs.end());
                out[k] = ptrs;
            } else out[k] = canonicalizeFloats(v);
        }
        // 2) 나머지 키는 알파벳순
        std::vector<std::string> rest;
        for (auto it = obj.begin(); it != obj.end(); ++it) if (!out.contains(it.key())) rest.push_back(it.key());
        std::sort(rest.begin(), rest.end());
        for (const auto& k : rest) out[k] = canonicalizeFloats(obj[k]);
        return out;
    };
    return canonObj(doc);
}

bool Project::saveDocument(std::string_view path, std::string* error) const {
    const Json* d = document(path);
    if (!d) { if (error) *error = "no such document"; return false; }
    return writeCanonicalFile((fs::path(rootDir_) / std::string(path)).string(), canonicalizeDocument(*d), error);
}

std::vector<std::string> Project::saveAll() const {
    std::vector<std::string> failed;
    std::string err;
    if (!writeCanonicalFile((fs::path(rootDir_) / "project.json").string(), canonicalizeDocument(projectJson_), &err)) failed.push_back("project.json: " + err);
    for (const auto& [p, d] : docs_) if (!saveDocument(p, &err)) failed.push_back(p + ": " + err);
    return failed;
}

// ---------------------------------------------------------------- validate (§29)

std::vector<Diagnostic> Project::validate() const {
    std::vector<Diagnostic> out;
    // 중복 id
    for (const auto& [id, where] : duplicates_) {
        Diagnostic d = Diagnostic::error("DUPLICATE_PERSISTENT_ID", "Persistent id " + id + " appears in more than one place: " + Json(where).dump() + ". Run `akeir id fix --keep <path>` (§7.3).")
                           .at(LogicalLocation{id, std::nullopt, std::nullopt});
        out.push_back(std::move(d));
    }
    for (const auto& [path, doc] : docs_) {
        if (!doc.is_object()) continue;
        bool isWorld = path.rfind("Worlds/", 0) == 0;
        const char* expectSchema = isWorld ? kWorldSchema : kPrefabSchema;
        if (doc.value("$schema", "") != expectSchema)
            out.push_back(docError("DOCUMENT_SCHEMA_MISMATCH", path + " $schema should be " + expectSchema + ".", path, "/$schema")
                              .withFix(Fix{"Set $schema", Applicability::MachineApplicable, true, {}, {Json{{"op", "add"}, {"path", "/$schema"}, {"value", expectSchema}}}, std::nullopt}));
        // id 형식
        std::string id = doc.value("id", "");
        std::string idErr = Id::validate(id);
        if (!idErr.empty()) out.push_back(docError("ID_FORMAT_INVALID", path + " id '" + id + "': " + idErr + ".", path, "/id", id));
        else {
            std::string wantPrefix = isWorld ? "world" : "prefab";
            if (Id::parse(id)->prefix() != wantPrefix) out.push_back(docError("ID_PREFIX_MISMATCH", path + " id should have prefix '" + wantPrefix + "_'.", path, "/id", id));
        }

        auto validateComponents = [&](const Json& comps, const std::string& pointer, const std::string& objectId) {
            if (!comps.is_object()) { out.push_back(docError("COMPONENTS_NOT_OBJECT", "'components' must be an object keyed by component name.", path, pointer, objectId)); return; }
            for (auto it = comps.begin(); it != comps.end(); ++it) {
                const ComponentMeta* meta = Registry::global().find(it.key());
                if (!meta) {
                    Json known = Json::array();
                    for (const auto* m : Registry::global().all()) known.push_back(m->name);
                    out.push_back(docError("COMPONENT_UNKNOWN", "Unknown component '" + it.key() + "'. Known: " + known.dump() + ".", path, pointer + "/" + escapePointerToken(it.key()), objectId)
                                      .at(LogicalLocation{objectId, it.key(), std::nullopt}));
                    continue;
                }
                auto ds = validateComponentJson(*meta, it.value(), PhysicalLocation{path, pointer + "/" + escapePointerToken(it.key()), std::nullopt}, Visibility::Authoring, objectId);
                out.insert(out.end(), ds.begin(), ds.end());
                // requires (§29 COMPONENT_DEPENDENCY_MISSING)
                for (const auto& req : meta->requiresComponents) {
                    if (comps.contains(req)) continue;
                    // fix 는 전이적 의존성까지 닫는다 (Movement 가 RigidBody2D 를 요구하면 둘 다) — 의존 대상을 먼저 add 하는 순서
                    std::vector<std::string> chain;
                    std::function<void(const std::string&)> collect = [&](const std::string& name) {
                        if (comps.contains(name) || std::find(chain.begin(), chain.end(), name) != chain.end()) return;
                        if (const ComponentMeta* rm = Registry::global().find(name))
                            for (const auto& r2 : rm->requiresComponents) collect(r2);
                        chain.push_back(name);
                    };
                    collect(req);
                    Fix f; f.description = "Add " + req + " component"; f.applicability = Applicability::MachineApplicable; f.isPreferred = true;
                    if (chain.size() > 1) { f.description += " (and its own requirements:"; for (std::size_t k = 0; k + 1 < chain.size(); ++k) f.description += " " + chain[k]; f.description += ")"; }
                    std::string cli;
                    for (const auto& name : chain) {
                        f.commands.push_back(CommandInvocation{"component.add", Json{{"entity", objectId}, {"component", name}}});
                        cli += (cli.empty() ? "" : " && ") + std::string("akeir component add ") + objectId + " " + name + " --json";
                    }
                    f.cli = cli;
                    out.push_back(docError("COMPONENT_DEPENDENCY_MISSING", it.key() + " requires " + req + ".", path, pointer, objectId)
                                      .at(LogicalLocation{objectId, it.key(), std::nullopt}).withFix(f));
                }
                // Collider2D 만 있고 RigidBody2D 가 없으면 physics body 가 만들어지지 않는다 (벽을 뚫고 지나간다) — 경고 + fix
                if (it.key() == "Collider2D" && !comps.contains("RigidBody2D")) {
                    Fix f; f.description = "Add a static RigidBody2D so the collider participates in physics"; f.applicability = Applicability::MachineApplicable; f.isPreferred = true;
                    f.commands.push_back(CommandInvocation{"component.add", Json{{"entity", objectId}, {"component", "RigidBody2D"}, {"value", Json{{"type", "static"}}}}});
                    f.cli = "akeir component add " + objectId + " RigidBody2D --value \"{\\\"type\\\":\\\"static\\\"}\" --json";
                    Diagnostic d = Diagnostic::warning("COLLIDER_WITHOUT_BODY", "Collider2D without RigidBody2D creates no physics body; nothing collides with it. Add RigidBody2D (type static for walls).")
                                       .in(PhysicalLocation{path, pointer + "/" + escapePointerToken(it.key()), std::nullopt}).at(LogicalLocation{objectId, it.key(), std::nullopt}).withFix(f);
                    out.push_back(std::move(d));
                }
                // Ref 속성의 dangling 검사 (§19)
                for (const auto& p : meta->props) {
                    if (p.type != PropType::Ref || !it.value().contains(p.name) || !it.value()[p.name].is_string()) continue;
                    std::string v = it.value()[p.name].get<std::string>();
                    if (v.empty()) continue;
                    Ref r{v};
                    std::string target(r.idPart());
                    if (!Id::validate(target).empty()) continue; // 형식 오류는 component 검증이 잡는다
                    std::string prefix(Id::parse(target)->prefix());
                    if (prefix == "asset") continue;             // asset sidecar 는 이후 Phase
                    if (!locate(target))
                        out.push_back(docError("REF_DANGLING", "Property '" + p.name + "' references " + target + " which does not exist.", path, pointer + "/" + escapePointerToken(it.key()) + "/" + p.name, objectId)
                                          .at(LogicalLocation{objectId, it.key(), "/" + p.name}));
                }
            }
        };

        if (isWorld) {
            if (!doc.contains("entities") || !doc["entities"].is_object()) { out.push_back(docError("WORLD_ENTITIES_MISSING", path + " has no 'entities' object.", path, "", id)); continue; }
            const Json& ents = doc["entities"];
            for (auto it = ents.begin(); it != ents.end(); ++it) {
                const std::string& eid = it.key();
                const std::string ptr = "/entities/" + escapePointerToken(eid);
                const Json& e = it.value();
                std::string eErr = Id::validate(eid);
                if (!eErr.empty()) { out.push_back(docError("ID_FORMAT_INVALID", "Entity key '" + eid + "': " + eErr + ".", path, ptr, eid)); continue; }
                if (Id::parse(eid)->prefix() != "entity") out.push_back(docError("ID_PREFIX_MISMATCH", "Entity key '" + eid + "' should have prefix 'entity_'.", path, ptr, eid));
                if (!e.is_object()) { out.push_back(docError("ENTITY_NOT_OBJECT", "Entity must be an object.", path, ptr, eid)); continue; }
                // parent / hierarchy cycle
                if (e.contains("parent") && !e["parent"].is_null()) {
                    if (!e["parent"].is_string() || !ents.contains(e["parent"].get<std::string>()))
                        out.push_back(docError("PARENT_NOT_FOUND", "Entity parent " + e["parent"].dump() + " is not in this world.", path, ptr + "/parent", eid));
                    else {
                        std::set<std::string> seen{eid};
                        std::string cur = e["parent"].get<std::string>();
                        while (ents.contains(cur)) {
                            if (seen.count(cur)) { out.push_back(docError("HIERARCHY_CYCLE", "Entity " + eid + " is its own ancestor via parent links.", path, ptr + "/parent", eid)); break; }
                            seen.insert(cur);
                            const Json& pe = ents[cur];
                            if (!pe.contains("parent") || !pe["parent"].is_string()) break;
                            cur = pe["parent"].get<std::string>();
                        }
                    }
                }
                if (e.contains("prefab")) {
                    std::vector<Diagnostic> ds;
                    auto comps = resolveEntityComponents(eid, &ds);
                    out.insert(out.end(), ds.begin(), ds.end());
                    if (comps) validateComponents(*comps, ptr + "/components", eid); // 인스턴스는 resolve 결과를 검증 (물리 위치는 근사)
                } else if (e.contains("components")) {
                    validateComponents(e["components"], ptr + "/components", eid);
                } else {
                    out.push_back(Diagnostic::note("ENTITY_EMPTY", "Entity " + eid + " has neither 'components' nor 'prefab'.").in(PhysicalLocation{path, ptr, std::nullopt}).at(LogicalLocation{eid, std::nullopt, std::nullopt}));
                }
            }
        } else {
            if (doc.contains("base")) {
                std::vector<Diagnostic> ds;
                auto comps = resolvePrefab(id, &ds);
                out.insert(out.end(), ds.begin(), ds.end());
                if (comps) validateComponents(*comps, "/components", id);
            } else if (doc.contains("components")) {
                validateComponents(doc["components"], "/components", id);
            } else {
                out.push_back(docError("PREFAB_COMPONENTS_MISSING", path + " has neither 'components' nor 'base'.", path, "", id));
            }
        }
    }
    // 파일이 canonical 인지 (§5.3 JSON_NOT_CANONICAL)
    for (const auto& [path, doc] : docs_) {
        fs::path f = fs::path(rootDir_) / path;
        std::ifstream in(f, std::ios::binary);
        if (!in) continue;
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (!isCanonicalText(text, canonicalizeDocument(doc)))
            out.push_back(Diagnostic::warning("JSON_NOT_CANONICAL", path + " is not in canonical form. Run `akeir fmt`.").in(PhysicalLocation{path, "", std::nullopt})
                              .withFix(Fix{"Rewrite file in canonical form", Applicability::MachineApplicable, true, {CommandInvocation{"project.fmt", Json{{"path", path}}}}, {}, std::string("akeir fmt " + path)}));
    }
    return out;
}

} // namespace akeir

// ---------------------------------------------------------------- reference graph (§19)

namespace akeir {

Json Project::Reference::toJson() const {
    Json j = Json{{"from", from}, {"kind", kind}, {"doc", doc}, {"pointer", pointer}};
    if (!detail.empty()) j["detail"] = detail;
    return j;
}

namespace {

std::vector<std::string> splitPointerParts(const std::string& ptr) {
    std::vector<std::string> parts;
    std::string cur;
    for (std::size_t i = 1; i <= ptr.size(); ++i) {
        if (i == ptr.size() || ptr[i] == '/') { parts.push_back(cur); cur.clear(); }
        else if (ptr[i] == '~' && i + 1 < ptr.size() && (ptr[i + 1] == '0' || ptr[i + 1] == '1')) { cur += ptr[i + 1] == '0' ? '~' : '/'; ++i; }
        else cur += ptr[i];
    }
    if (ptr.empty()) parts.clear();
    return parts;
}

/// 문서 하나에서 나가는 모든 참조 (from, kind, pointer, detail, target)
struct Edge { Project::Reference ref; std::string target; };

void collectRefsInComponents(const Json& comps, const std::string& owner, const std::string& doc, const std::string& basePointer, std::vector<Edge>& out) {
    if (!comps.is_object()) return;
    for (const auto& [cname, cval] : comps.items()) {
        const ComponentMeta* meta = Registry::global().find(cname);
        if (!meta || !cval.is_object()) continue;
        for (const auto& p : meta->props) {
            if (p.type != PropType::Ref || !cval.contains(p.name) || !cval[p.name].is_string()) continue;
            std::string v = cval[p.name].get<std::string>();
            if (v.empty()) continue;
            Ref r{v};
            out.push_back({{owner, "property", doc, basePointer + "/" + escapePointerToken(cname) + "/" + p.name, cname + "." + p.name}, std::string(r.idPart())});
        }
    }
}

void collectRefsInOverrides(const Json& container, const std::string& owner, const std::string& doc, const std::string& basePointer, std::vector<Edge>& out) {
    for (const char* key : {"set", "add"}) {
        if (!container.contains(key) || !container[key].is_object()) continue;
        for (const auto& [ptr, val] : container[key].items()) {
            // "/components/<C>/<prop>" 형태의 set 이나 "/components/<C>" 형태의 add 안에서 Ref 속성 찾기
            if (val.is_string()) {
                auto parts = splitPointerParts(ptr);
                if (parts.size() == 3 && parts[0] == "components") {
                    const ComponentMeta* meta = Registry::global().find(parts[1]);
                    const PropertyMeta* pm = meta ? meta->find(parts[2]) : nullptr;
                    if (pm && pm->type == PropType::Ref && !val.get<std::string>().empty())
                        out.push_back({{owner, "override", doc, basePointer + "/" + key + "/" + escapePointerToken(ptr), ptr}, std::string(Ref{val.get<std::string>()}.idPart())});
                }
            } else if (val.is_object()) {
                auto parts = splitPointerParts(ptr);
                if (parts.size() == 2 && parts[0] == "components") {
                    Json wrapped = Json::object();
                    wrapped[parts[1]] = val;
                    std::vector<Edge> inner;
                    collectRefsInComponents(wrapped, owner, doc, basePointer + "/" + key + "/" + escapePointerToken(ptr), inner);
                    for (auto& e : inner) { e.ref.kind = "override"; e.ref.detail = ptr + " " + e.ref.detail; out.push_back(e); }
                }
            }
        }
    }
}

std::vector<Edge> allEdges(const Project& prj) {
    std::vector<Edge> out;
    if (auto dw = prj.defaultWorld()) out.push_back({{"project", "defaultWorld", "project.json", "/defaultWorld", ""}, *dw});
    for (const auto& [path, doc] : prj.documents()) {
        if (!doc.is_object()) continue;
        std::string docId = doc.value("id", "");
        if (doc.contains("entities") && doc["entities"].is_object()) {
            for (const auto& [eid, e] : doc["entities"].items()) {
                if (!e.is_object()) continue;
                std::string base = "/entities/" + escapePointerToken(eid);
                if (e.contains("parent") && e["parent"].is_string()) out.push_back({{eid, "parent", path, base + "/parent", ""}, e["parent"].get<std::string>()});
                if (e.contains("prefab") && e["prefab"].is_string()) out.push_back({{eid, "prefab", path, base + "/prefab", ""}, e["prefab"].get<std::string>()});
                if (e.contains("components")) collectRefsInComponents(e["components"], eid, path, base + "/components", out);
                collectRefsInOverrides(e, eid, path, base, out);
            }
        } else {
            if (doc.contains("base") && doc["base"].is_string()) out.push_back({{docId, "base", path, "/base", ""}, doc["base"].get<std::string>()});
            if (doc.contains("components")) collectRefsInComponents(doc["components"], docId, path, "/components", out);
            collectRefsInOverrides(doc, docId, path, "", out);
        }
    }
    std::sort(out.begin(), out.end(), [](const Edge& a, const Edge& b) { return std::tie(a.ref.doc, a.ref.pointer) < std::tie(b.ref.doc, b.ref.pointer); });
    return out;
}

} // namespace

std::vector<Project::Reference> Project::referencesTo(std::string_view id) const {
    std::vector<Reference> out;
    for (const auto& e : allEdges(*this)) if (e.target == id) out.push_back(e.ref);
    return out;
}

std::vector<Project::Reference> Project::referencesFrom(std::string_view id) const {
    std::vector<Reference> out;
    for (const auto& e : allEdges(*this)) if (e.ref.from == id) { Reference r = e.ref; r.detail = (r.detail.empty() ? "" : r.detail + " -> ") + e.target; out.push_back(r); }
    return out;
}

} // namespace akeir
