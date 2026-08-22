// BuiltinCommands.cpp — 내장 Mutation command. 설계 문서 §8 (Command 목록), §8.1 (id = <noun>.<verb>, BRP alias), §34 (prefab override: set/add/remove),
// §78.1 (Command → ops 매핑: 인스턴스 entity 는 override 맵만 건드린다), §7 (새 id 는 UUIDv7), §6 (order key).
//
//   command          args                                                         result
//   entity.create    {world?, name, parent?, order?, tags?, components?, prefab?, set?}   {id, path, doc}
//   entity.delete    {entity, recursive?=true}                                    {id, deleted:[ids]}
//   entity.rename    {entity, name}                                               {id, name}
//   entity.reparent  {entity, parent|null, order?}                                {id, parent, order}
//   component.add    {entity|prefab selector, component, value?}                  {id, component, value}   ('entity' 는 prefab selector 도 받는다)
//   component.remove {entity, component}                                          {id, component}
//   property.set     {entity, component, path, value}                             {id, component, path, value, override?}
//   tag.add / tag.remove {entity, tag}                                            {id, tags}
//   prefab.create    {name, components?, base?, tags?}                            {id, doc}
//   prefab.instantiate {prefab, world?, name?, parent?, position?, set?, tags?}   {id, path, doc}
//   world.create     {name}                                                       {id, doc}
//   asset.import     {source, grid?, names?, pixelsPerUnit?, filter?, pivot?}      {id, doc, subAssets}   ← Assets/<png>.meta.json sidecar (§37, ADR-0037)
//   document.patch   {doc, ops:[RFC 6902]}                                        {doc, applied}   ← raw 편집 (validate --fix 의 artifactChanges 용)
//
//   인스턴스 entity(prefab 참조)의 component/property 변경은 문서의 set/add/remove 맵에 기록된다 (§78.1). 그래야 prefab 을 고치면 인스턴스가 따라간다.
//   값 검증은 reflection(validateComponentJson)으로 즉시 하고, 문서 수준(REF_DANGLING, DEPENDENCY 등)은 CommandBus::validateFork 가 commit 전에 한다.
#include "akeir/commands/CommandBus.h"

#include "akeir/core/Id.h"
#include "akeir/reflection/Registry.h"
#include "akeir/runtime/Assets.h"
#include "akeir/serialization/Canonical.h"
#include "akeir/serialization/ComponentJson.h"

#include <algorithm>
#include <cctype>

namespace akeir {

namespace {

using EntityRef = CommandContext::EntityRef;

Json schema(std::initializer_list<std::pair<const char*, Json>> props, std::initializer_list<const char*> required = {}) {
    Json s = Json::object();
    s["type"] = "object";
    Json p = Json::object();
    for (const auto& [k, v] : props) p[k] = v;
    s["properties"] = std::move(p);
    if (required.size()) { Json r = Json::array(); for (auto* k : required) r.push_back(k); s["required"] = std::move(r); }
    s["additionalProperties"] = false;
    return s;
}
Json str(const char* desc) { return Json{{"type", "string"}, {"description", desc}}; }
Json obj(const char* desc) { return Json{{"type", "object"}, {"description", desc}}; }
Json strArr(const char* desc) { return Json{{"type", "array"}, {"items", Json{{"type", "string"}}}, {"description", desc}}; }

const Json& entityJson(CommandContext& ctx, const EntityRef& r) {
    return ctx.project.document(r.doc)->at(Json::json_pointer(r.pointer));
}
bool isInstance(const Json& e) { return e.contains("prefab") && e["prefab"].is_string(); }

std::string normalizePointer(std::string p) {
    if (p.empty()) return p;
    if (p[0] != '/') p = "/" + p;
    while (p.size() > 1 && p.back() == '/') p.pop_back();
    return p;
}

std::string sanitizeFileName(std::string name) {
    std::string out;
    for (char c : name) out += (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-') ? c : '_';
    if (out.empty()) out = "Unnamed";
    return out;
}

const ComponentMeta* requireMeta(CommandContext& ctx, const std::string& component) {
    if (const ComponentMeta* m = Registry::global().find(component)) return m;
    Json cands = Json::array();
    for (const auto* m : Registry::global().all()) cands.push_back(m->name);
    ctx.fail(ErrorCategory::NotFound, "COMPONENT_UNKNOWN", "Component '" + component + "' is not registered.", Json{{"component", component}, {"registered", cands}});
    return nullptr;
}

/// 값 검증 (error 만 실패). warning 은 ctx.warnings 로.
bool validateValue(CommandContext& ctx, const ComponentMeta& meta, const Json& value, const std::string& doc, const std::string& pointer, const std::string& entityId) {
    auto diags = validateComponentJson(meta, value, PhysicalLocation{doc, pointer, std::nullopt}, Visibility::Authoring, entityId);
    std::vector<Diagnostic> errors;
    for (auto& d : diags) (d.level == Severity::Error ? errors : ctx.warnings).push_back(d);
    if (errors.empty()) return true;
    Json all = Json::array();
    for (const auto& d : errors) all.push_back(d.toJson());
    ctx.failDiagnostic(ErrorCategory::Validation, errors.front(), Json{{"diagnostics", all}});
    return false;
}

/// 마지막 형제 order key
std::string lastSiblingOrder(const Json& worldDoc, const Json& parent) {
    std::string last;
    if (!worldDoc.contains("entities")) return last;
    for (const auto& [id, e] : worldDoc["entities"].items()) {
        if (!e.is_object()) continue;
        Json p = e.value("parent", Json());
        if (p != parent) continue;
        std::string o = e.value("order", "");
        if (o > last) last = o;
    }
    return last;
}

std::vector<std::string> descendants(const Json& worldDoc, const std::string& rootId) {
    std::vector<std::string> out{rootId};
    if (!worldDoc.contains("entities")) return out;
    for (std::size_t i = 0; i < out.size(); ++i) {
        for (const auto& [id, e] : worldDoc["entities"].items()) {
            if (!e.is_object()) continue;
            const Json& p = e.contains("parent") ? e["parent"] : Json();
            if (p.is_string() && p.get<std::string>() == out[i] && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
        }
    }
    return out;
}

bool setBuilderError(CommandContext& ctx) {
    const auto& errs = ctx.changes.errors();
    if (!errs.empty()) return ctx.failDiagnostic(ErrorCategory::Conflict, errs.back());
    return ctx.fail(ErrorCategory::Internal, "CHANGE_FAILED", "ChangeBuilder rejected the op.");
}

// ---------------------------------------------------------------- entity.*

bool entityCreate(CommandContext& ctx) {
    const Json& a = ctx.args;
    if (!a.contains("name") || !a["name"].is_string() || a["name"].get<std::string>().empty())
        return ctx.fail(ErrorCategory::Usage, "ARG_REQUIRED", "entity.create needs 'name'.");
    auto worldDoc = ctx.resolveWorldDoc(a.value("world", ""));
    if (!worldDoc) return false;
    Json parent;   // null = root
    if (a.contains("parent") && a["parent"].is_string() && !a["parent"].get<std::string>().empty()) {
        auto p = ctx.resolveEntity(a["parent"].get<std::string>());
        if (!p) return false;
        if (p->doc != *worldDoc) return ctx.fail(ErrorCategory::Usage, "PARENT_IN_OTHER_WORLD", "Parent entity lives in " + p->doc + ", not in " + *worldDoc + ".");
        parent = p->id;
    }
    const Json& wd = *ctx.project.document(*worldDoc);
    std::string id = Id::generate("entity").str();
    Json e = Json::object();
    e["name"] = a["name"];
    e["parent"] = parent;
    e["order"] = a.contains("order") && a["order"].is_string() ? a["order"].get<std::string>() : nextOrderKey(lastSiblingOrder(wd, parent));
    if (a.contains("tags")) { if (!a["tags"].is_array()) return ctx.fail(ErrorCategory::Usage, "ARG_TYPE", "'tags' must be an array of strings."); e["tags"] = a["tags"]; }

    if (a.contains("prefab") && a["prefab"].is_string()) {
        auto p = ctx.resolvePrefab(a["prefab"].get<std::string>());
        if (!p) return false;
        e["prefab"] = p->id;
        if (a.contains("set")) { if (!a["set"].is_object()) return ctx.fail(ErrorCategory::Usage, "ARG_TYPE", "'set' must be an object {pointer: value}."); e["set"] = a["set"]; }
    } else {
        Json comps = a.value("components", Json::object());
        if (!comps.is_object()) return ctx.fail(ErrorCategory::Usage, "ARG_TYPE", "'components' must be an object {Name: {...}}.");
        if (!comps.contains("Transform")) {
            Json full = Json::object();
            full["Transform"] = Registry::global().find("Transform")->defaultJson(Visibility::Authoring);
            for (auto& [k, v] : comps.items()) full[k] = v;
            comps = std::move(full);
        }
        for (auto& [name, value] : comps.items()) {
            const ComponentMeta* m = requireMeta(ctx, name);
            if (!m) return false;
            if (!value.is_object()) return ctx.fail(ErrorCategory::Usage, "ARG_TYPE", "components." + name + " must be an object.");
            Json merged = m->defaultJson(Visibility::Authoring);
            for (auto& [k, v] : value.items()) merged[k] = v;
            merged = canonicalizeFloats(merged);
            if (!validateValue(ctx, *m, merged, *worldDoc, "/entities/" + id + "/components/" + name, id)) return false;
            value = std::move(merged);
        }
        e["components"] = std::move(comps);
    }
    if (!ctx.changes.add(*worldDoc, "/entities/" + id, e)) return setBuilderError(ctx);
    ctx.result = Json{{"id", id}, {"path", ctx.project.entityPath(id).value_or("")}, {"doc", *worldDoc}};
    return true;
}

bool entityDelete(CommandContext& ctx) {
    if (!ctx.args.contains("entity")) return ctx.fail(ErrorCategory::Usage, "ARG_REQUIRED", "entity.delete needs 'entity'.");
    auto r = ctx.resolveEntity(ctx.args["entity"].get<std::string>());
    if (!r) return false;
    const Json& wd = *ctx.project.document(r->doc);
    auto ids = descendants(wd, r->id);
    if (ids.size() > 1 && !ctx.args.value("recursive", true))
        return ctx.fail(ErrorCategory::Precondition, "ENTITY_HAS_CHILDREN", "Entity has " + std::to_string(ids.size() - 1) + " descendant(s); pass recursive:true.", Json{{"descendants", ids}});
    // 자식부터 지운다 (inverse 가 부모부터 되살리도록)
    for (auto it = ids.rbegin(); it != ids.rend(); ++it)
        if (!ctx.changes.remove(r->doc, "/entities/" + *it)) return setBuilderError(ctx);
    ctx.result = Json{{"id", r->id}, {"deleted", ids}};
    return true;
}

bool entityRename(CommandContext& ctx) {
    if (!ctx.args.contains("entity") || !ctx.args.contains("name") || !ctx.args["name"].is_string())
        return ctx.fail(ErrorCategory::Usage, "ARG_REQUIRED", "entity.rename needs 'entity' and 'name'.");
    auto r = ctx.resolveEntity(ctx.args["entity"].get<std::string>());
    if (!r) return false;
    if (!ctx.changes.set(r->doc, r->pointer + "/name", ctx.args["name"])) return setBuilderError(ctx);
    ctx.result = Json{{"id", r->id}, {"name", ctx.args["name"]}, {"path", ctx.project.entityPath(r->id).value_or("")}};
    return true;
}

bool entityReparent(CommandContext& ctx) {
    if (!ctx.args.contains("entity") || !ctx.args.contains("parent"))
        return ctx.fail(ErrorCategory::Usage, "ARG_REQUIRED", "entity.reparent needs 'entity' and 'parent' (id/selector or null).");
    auto r = ctx.resolveEntity(ctx.args["entity"].get<std::string>());
    if (!r) return false;
    Json parent;
    if (ctx.args["parent"].is_string() && !ctx.args["parent"].get<std::string>().empty()) {
        auto p = ctx.resolveEntity(ctx.args["parent"].get<std::string>());
        if (!p) return false;
        if (p->doc != r->doc) return ctx.fail(ErrorCategory::Usage, "PARENT_IN_OTHER_WORLD", "Cannot reparent across worlds.");
        auto sub = descendants(*ctx.project.document(r->doc), r->id);
        if (std::find(sub.begin(), sub.end(), p->id) != sub.end())
            return ctx.fail(ErrorCategory::Precondition, "HIERARCHY_CYCLE", "New parent is the entity itself or one of its descendants.");
        parent = p->id;
    }
    const Json& wd = *ctx.project.document(r->doc);
    std::string order = ctx.args.contains("order") && ctx.args["order"].is_string() ? ctx.args["order"].get<std::string>() : nextOrderKey(lastSiblingOrder(wd, parent));
    if (!ctx.changes.set(r->doc, r->pointer + "/parent", parent)) return setBuilderError(ctx);
    if (!ctx.changes.set(r->doc, r->pointer + "/order", order)) return setBuilderError(ctx);
    ctx.result = Json{{"id", r->id}, {"parent", parent}, {"order", order}, {"path", ctx.project.entityPath(r->id).value_or("")}};
    return true;
}

// ---------------------------------------------------------------- component.* / property.set

struct Target {
    EntityRef ref;        // entity: pointer "/entities/<id>"; prefab: pointer "" (문서 루트)
    bool instance = false; // entity 가 prefab 을 참조하거나 prefab 이 base 를 가진다 → override 맵으로 편집
    bool isPrefab = false;
    Json resolved;        // 최종 components
};

/// 'entity' 인자는 entity 또는 prefab selector 를 받는다 (prefab 편집 = "모든 고블린을 빠르게").
std::optional<Target> loadTarget(CommandContext& ctx) {
    if (!ctx.args.contains("entity") || !ctx.args["entity"].is_string()) { ctx.fail(ErrorCategory::Usage, "ARG_REQUIRED", "'entity' is required (entity or prefab selector)."); return std::nullopt; }
    const std::string selector = ctx.args["entity"].get<std::string>();
    Target t;
    auto ids = ctx.project.resolveSelector(selector);
    std::vector<std::string> ents, prefabs;
    for (const auto& id : ids) if (auto loc = ctx.project.locate(id)) { if (loc->kind == "entity") ents.push_back(id); else if (loc->kind == "prefab") prefabs.push_back(id); }
    if (ents.size() + prefabs.size() > 1) {
        Json cands = Json::array(); for (auto& i : ents) cands.push_back(i); for (auto& i : prefabs) cands.push_back(i);
        ctx.fail(ErrorCategory::Usage, "AMBIGUOUS_SELECTOR", "'" + selector + "' matches several entities/prefabs; use an id or path: selector.", Json{{"selector", selector}, {"candidates", cands}});
        return std::nullopt;
    }
    if (ents.empty() && prefabs.empty()) { ctx.fail(ErrorCategory::NotFound, "ENTITY_NOT_FOUND", "entity or prefab '" + selector + "' not found.", Json{{"selector", selector}}); return std::nullopt; }
    std::vector<Diagnostic> diags;
    std::optional<Json> res;
    if (!ents.empty()) {
        auto loc = ctx.project.locate(ents[0]);
        t.ref = EntityRef{loc->doc, loc->pointer, ents[0]};
        t.instance = isInstance(entityJson(ctx, t.ref));
        res = ctx.project.resolveEntityComponents(ents[0], &diags);
    } else {
        auto loc = ctx.project.locate(prefabs[0]);
        t.ref = EntityRef{loc->doc, "", prefabs[0]};
        t.isPrefab = true;
        const Json& pd = *ctx.project.document(loc->doc);
        t.instance = pd.contains("base") && pd["base"].is_string();
        res = ctx.project.resolvePrefab(prefabs[0], &diags);
    }
    if (!res) {
        ctx.failDiagnostic(ErrorCategory::Precondition, diags.empty() ? Diagnostic::error("PREFAB_RESOLVE_FAILED", "Cannot resolve components.") : diags.front());
        return std::nullopt;
    }
    t.resolved = std::move(*res);
    return t;
}

/// override 체인의 base components (entity → prefab 전체 / derived prefab → base prefab 전체)
std::optional<Json> baseComponents(CommandContext& ctx, const Target& t) {
    const Json& e = entityJson(ctx, t.ref);
    const char* key = t.isPrefab ? "base" : "prefab";
    if (!e.contains(key) || !e[key].is_string()) return std::nullopt;
    return ctx.project.resolvePrefab(e[key].get<std::string>());
}

std::string requireComponentArg(CommandContext& ctx) {
    if (!ctx.args.contains("component") || !ctx.args["component"].is_string()) { ctx.fail(ErrorCategory::Usage, "ARG_REQUIRED", "'component' is required."); return ""; }
    return ctx.args["component"].get<std::string>();
}

bool componentAdd(CommandContext& ctx) {
    auto t = loadTarget(ctx);
    if (!t) return false;
    std::string comp = requireComponentArg(ctx);
    if (comp.empty()) return false;
    const ComponentMeta* m = requireMeta(ctx, comp);
    if (!m) return false;
    if (t->resolved.contains(comp))
        return ctx.fail(ErrorCategory::Conflict, "COMPONENT_EXISTS", "Entity already has " + comp + " (use property.set).", Json{{"entity", t->ref.id}, {"component", comp}});
    Json value = m->defaultJson(Visibility::Authoring);
    if (ctx.args.contains("value")) {
        if (!ctx.args["value"].is_object()) return ctx.fail(ErrorCategory::Usage, "ARG_TYPE", "'value' must be an object.");
        for (auto& [k, v] : ctx.args["value"].items()) value[k] = v;
    }
    value = canonicalizeFloats(value);
    const std::string compPointer = "/components/" + comp;
    if (!validateValue(ctx, *m, value, t->ref.doc, t->ref.pointer + compPointer, t->ref.id)) return false;

    if (!t->instance) {
        if (!ctx.changes.exists(t->ref.doc, t->ref.pointer + "/components"))
            if (!ctx.changes.add(t->ref.doc, t->ref.pointer + "/components", Json::object())) return setBuilderError(ctx);
        if (!ctx.changes.add(t->ref.doc, t->ref.pointer + compPointer, value)) return setBuilderError(ctx);
    } else {
        const Json& e = entityJson(ctx, t->ref);
        // prefab 이 가진 component 를 remove 했다가 되살리는 경우: remove 목록에서 빼고 값은 set 으로
        int removeIdx = -1;
        if (e.contains("remove") && e["remove"].is_array())
            for (std::size_t i = 0; i < e["remove"].size(); ++i) if (e["remove"][i] == compPointer) removeIdx = static_cast<int>(i);
        if (removeIdx >= 0) {
            if (!ctx.changes.remove(t->ref.doc, t->ref.pointer + "/remove/" + std::to_string(removeIdx))) return setBuilderError(ctx);
            if (ctx.changes.get(t->ref.doc, t->ref.pointer + "/remove")->empty())
                if (!ctx.changes.remove(t->ref.doc, t->ref.pointer + "/remove")) return setBuilderError(ctx);
            if (ctx.args.contains("value"))
                for (auto& [k, v] : ctx.args["value"].items()) {
                    if (!ctx.changes.exists(t->ref.doc, t->ref.pointer + "/set")) if (!ctx.changes.add(t->ref.doc, t->ref.pointer + "/set", Json::object())) return setBuilderError(ctx);
                    if (!ctx.changes.set(t->ref.doc, t->ref.pointer + "/set/" + escapeToken(compPointer + "/" + k), canonicalizeFloats(v))) return setBuilderError(ctx);
                }
        } else {
            if (!ctx.changes.exists(t->ref.doc, t->ref.pointer + "/add"))
                if (!ctx.changes.add(t->ref.doc, t->ref.pointer + "/add", Json::object())) return setBuilderError(ctx);
            if (!ctx.changes.add(t->ref.doc, t->ref.pointer + "/add/" + escapeToken(compPointer), value)) return setBuilderError(ctx);
        }
    }
    ctx.result = Json{{"id", t->ref.id}, {"component", comp}, {"value", value}, {"override", t->instance}, {"prefab", t->isPrefab}};
    return true;
}

bool componentRemove(CommandContext& ctx) {
    auto t = loadTarget(ctx);
    if (!t) return false;
    std::string comp = requireComponentArg(ctx);
    if (comp.empty()) return false;
    if (!t->resolved.contains(comp))
        return ctx.fail(ErrorCategory::NotFound, "COMPONENT_NOT_ON_ENTITY", "Entity has no " + comp + ".", Json{{"entity", t->ref.id}, {"component", comp}});
    if (comp == "Transform") return ctx.fail(ErrorCategory::Precondition, "COMPONENT_REQUIRED", "Transform cannot be removed.");
    // 의존하는 component 가 남으면 거부 (COMPONENT_DEPENDENCY_MISSING 을 미리 막는다)
    for (const auto& [other, _] : t->resolved.items()) {
        if (other == comp) continue;
        if (const ComponentMeta* om = Registry::global().find(other))
            for (const auto& req : om->requiresComponents) if (req == comp)
                return ctx.fail(ErrorCategory::Precondition, "COMPONENT_DEPENDENCY", other + " requires " + comp + "; remove " + other + " first.", Json{{"dependent", other}});
    }
    const std::string compPointer = "/components/" + comp;
    if (!t->instance) {
        if (!ctx.changes.remove(t->ref.doc, t->ref.pointer + compPointer)) return setBuilderError(ctx);
    } else {
        const Json& e = entityJson(ctx, t->ref);
        if (e.contains("add") && e["add"].is_object() && e["add"].contains(compPointer)) {
            if (!ctx.changes.remove(t->ref.doc, t->ref.pointer + "/add/" + escapeToken(compPointer))) return setBuilderError(ctx);
            if (ctx.changes.get(t->ref.doc, t->ref.pointer + "/add")->empty())
                if (!ctx.changes.remove(t->ref.doc, t->ref.pointer + "/add")) return setBuilderError(ctx);
        } else {
            if (!ctx.changes.exists(t->ref.doc, t->ref.pointer + "/remove"))
                if (!ctx.changes.add(t->ref.doc, t->ref.pointer + "/remove", Json::array())) return setBuilderError(ctx);
            if (!ctx.changes.add(t->ref.doc, t->ref.pointer + "/remove/-", compPointer)) return setBuilderError(ctx);
        }
        // 이 component 를 가리키던 set 항목 정리
        const Json* setMap = ctx.changes.get(t->ref.doc, t->ref.pointer + "/set");
        if (setMap && setMap->is_object()) {
            std::vector<std::string> keys;
            for (const auto& [k, _] : setMap->items()) if (k.rfind(compPointer + "/", 0) == 0) keys.push_back(k);
            for (const auto& k : keys) if (!ctx.changes.remove(t->ref.doc, t->ref.pointer + "/set/" + escapeToken(k))) return setBuilderError(ctx);
            if (!keys.empty() && ctx.changes.get(t->ref.doc, t->ref.pointer + "/set")->empty())
                if (!ctx.changes.remove(t->ref.doc, t->ref.pointer + "/set")) return setBuilderError(ctx);
        }
    }
    ctx.result = Json{{"id", t->ref.id}, {"component", comp}, {"override", t->instance}, {"prefab", t->isPrefab}};
    return true;
}

bool propertySet(CommandContext& ctx) {
    auto t = loadTarget(ctx);
    if (!t) return false;
    std::string comp = requireComponentArg(ctx);
    if (comp.empty()) return false;
    if (!ctx.args.contains("path") || !ctx.args["path"].is_string() || !ctx.args.contains("value"))
        return ctx.fail(ErrorCategory::Usage, "ARG_REQUIRED", "property.set needs 'path' (e.g. \"max\" or \"/position/0\") and 'value'.");
    const ComponentMeta* m = requireMeta(ctx, comp);
    if (!m) return false;
    if (!t->resolved.contains(comp))
        return ctx.fail(ErrorCategory::NotFound, "COMPONENT_NOT_ON_ENTITY", "Entity has no " + comp + " (use component.add).", Json{{"entity", t->ref.id}, {"component", comp}});
    std::string path = normalizePointer(ctx.args["path"].get<std::string>());
    if (path.empty()) return ctx.fail(ErrorCategory::Usage, "ARG_TYPE", "'path' must point at a property, not the whole component.");
    // property 존재 / flag 검사
    std::string propName = path.substr(1, path.find('/', 1) == std::string::npos ? std::string::npos : path.find('/', 1) - 1);
    const PropertyMeta* pm = m->find(propName);
    if (!pm) {
        Json cands = Json::array();
        for (const auto& p : m->props) cands.push_back(p.name);
        return ctx.fail(ErrorCategory::NotFound, "PROPERTY_UNKNOWN", comp + " has no property '" + propName + "'.", Json{{"component", comp}, {"property", propName}, {"properties", cands}});
    }
    if (has(pm->flags, PropFlags::RuntimeOnly)) return ctx.fail(ErrorCategory::Precondition, "PROPERTY_RUNTIME_ONLY", comp + "." + propName + " is runtimeOnly; it is not authored (set it in play mode instead).");
    if (has(pm->flags, PropFlags::ReadOnly)) return ctx.fail(ErrorCategory::Precondition, "PROPERTY_READ_ONLY", comp + "." + propName + " is readOnly.");

    Json value = canonicalizeFloats(ctx.args["value"]);
    // 결과 component 를 만들어 검증
    Json after = t->resolved[comp];
    try {
        Json patch = Json::array({Json{{"op", after.contains(Json::json_pointer(path)) ? "replace" : "add"}, {"path", path}, {"value", value}}});
        after = after.patch(patch);
    } catch (const std::exception& e) {
        return ctx.fail(ErrorCategory::Usage, "PROPERTY_PATH_INVALID", std::string("Cannot apply path ") + path + ": " + e.what());
    }
    const std::string compPointer = "/components/" + comp;
    if (!validateValue(ctx, *m, after, t->ref.doc, t->ref.pointer + compPointer, t->ref.id)) return false;

    if (!t->instance) {
        // 문서에 property 가 생략(=default)되어 있을 수 있다 → set 이 add/replace 를 고른다
        const std::string full = t->ref.pointer + compPointer + path;
        // 중간 경로(예 /position) 가 없으면 property 전체를 쓴다
        std::string propPointer = t->ref.pointer + compPointer + "/" + propName;
        if (!ctx.changes.exists(t->ref.doc, propPointer)) {
            if (!ctx.changes.add(t->ref.doc, propPointer, after[propName])) return setBuilderError(ctx);
        } else if (!ctx.changes.set(t->ref.doc, full, value)) return setBuilderError(ctx);
        ctx.result = Json{{"id", t->ref.id}, {"component", comp}, {"path", path}, {"value", value}, {"override", false}, {"prefab", t->isPrefab}};
        return true;
    }
    // 인스턴스: set 맵. prefab 값과 같아지면 override 를 지운다 (깨끗한 문서)
    const std::string key = compPointer + path;
    const std::string setKeyPointer = t->ref.pointer + "/set/" + escapeToken(key);
    Json baseValue;
    bool haveBase = false;
    if (auto base = baseComponents(ctx, *t)) {
        Json root = Json::object(); root["components"] = *base;
        Json::json_pointer kp(key);
        if (root.contains(kp)) { baseValue = root.at(kp); haveBase = true; }
    }
    // add 맵에 이 component 가 있으면 (인스턴스가 추가한 component) add 값 안에서 고친다
    const std::string addKeyPointer = t->ref.pointer + "/add/" + escapeToken(compPointer);
    if (ctx.changes.exists(t->ref.doc, addKeyPointer)) {
        if (!ctx.changes.set(t->ref.doc, addKeyPointer + path, value)) return setBuilderError(ctx);
        ctx.result = Json{{"id", t->ref.id}, {"component", comp}, {"path", path}, {"value", value}, {"override", "add"}, {"prefab", t->isPrefab}};
        return true;
    }
    if (haveBase && baseValue == value) {
        if (ctx.changes.exists(t->ref.doc, setKeyPointer)) {
            if (!ctx.changes.remove(t->ref.doc, setKeyPointer)) return setBuilderError(ctx);
            if (ctx.changes.get(t->ref.doc, t->ref.pointer + "/set")->empty())
                if (!ctx.changes.remove(t->ref.doc, t->ref.pointer + "/set")) return setBuilderError(ctx);
        }
        ctx.result = Json{{"id", t->ref.id}, {"component", comp}, {"path", path}, {"value", value}, {"override", "inherited"}, {"prefab", t->isPrefab}};
        return true;
    }
    if (!ctx.changes.exists(t->ref.doc, t->ref.pointer + "/set"))
        if (!ctx.changes.add(t->ref.doc, t->ref.pointer + "/set", Json::object())) return setBuilderError(ctx);
    if (!ctx.changes.set(t->ref.doc, setKeyPointer, value)) return setBuilderError(ctx);
    ctx.result = Json{{"id", t->ref.id}, {"component", comp}, {"path", path}, {"value", value}, {"override", "set"}, {"prefab", t->isPrefab}};
    return true;
}

// ---------------------------------------------------------------- tag.*

bool tagAddRemove(CommandContext& ctx, bool add) {
    if (!ctx.args.contains("entity") || !ctx.args.contains("tag") || !ctx.args["tag"].is_string())
        return ctx.fail(ErrorCategory::Usage, "ARG_REQUIRED", "needs 'entity' and 'tag'.");
    auto r = ctx.resolveEntity(ctx.args["entity"].get<std::string>());
    if (!r) return false;
    std::string tag = ctx.args["tag"].get<std::string>();
    const Json& e = entityJson(ctx, *r);
    Json tags = e.value("tags", Json::array());
    if (!tags.is_array()) tags = Json::array();
    auto it = std::find(tags.begin(), tags.end(), Json(tag));
    if (add) {
        if (it == tags.end()) {
            if (!e.contains("tags")) { if (!ctx.changes.add(r->doc, r->pointer + "/tags", Json::array({tag}))) return setBuilderError(ctx); }
            else if (!ctx.changes.add(r->doc, r->pointer + "/tags/-", tag)) return setBuilderError(ctx);
            tags.push_back(tag);
        }
    } else {
        if (it == tags.end()) return ctx.fail(ErrorCategory::NotFound, "TAG_NOT_ON_ENTITY", "Entity has no tag '" + tag + "'.");
        auto idx = static_cast<std::size_t>(it - tags.begin());
        if (!ctx.changes.remove(r->doc, r->pointer + "/tags/" + std::to_string(idx))) return setBuilderError(ctx);
        tags.erase(it);
    }
    ctx.result = Json{{"id", r->id}, {"tags", tags}};
    return true;
}

// ---------------------------------------------------------------- prefab.* / world.create

bool prefabCreate(CommandContext& ctx) {
    const Json& a = ctx.args;
    if (!a.contains("name") || !a["name"].is_string() || a["name"].get<std::string>().empty())
        return ctx.fail(ErrorCategory::Usage, "ARG_REQUIRED", "prefab.create needs 'name'.");
    std::string name = a["name"].get<std::string>();
    std::string doc = "Prefabs/" + sanitizeFileName(name) + ".prefab.json";
    if (ctx.project.document(doc)) return ctx.fail(ErrorCategory::Conflict, "DOCUMENT_EXISTS", doc + " already exists.", Json{{"doc", doc}});
    std::string id = Id::generate("prefab").str();
    Json p = Json::object();
    p["$schema"] = "game://schema/prefab/1";
    p["schemaVersion"] = 1;
    p["id"] = id;
    p["name"] = name;
    if (a.contains("tags")) p["tags"] = a["tags"];
    if (a.contains("base") && a["base"].is_string()) {
        auto b = ctx.resolvePrefab(a["base"].get<std::string>());
        if (!b) return false;
        p["base"] = b->id;
        if (a.contains("set")) p["set"] = a["set"];
        if (a.contains("add")) p["add"] = a["add"];
        if (a.contains("remove")) p["remove"] = a["remove"];
    } else {
        Json comps = a.value("components", Json::object());
        if (!comps.is_object()) return ctx.fail(ErrorCategory::Usage, "ARG_TYPE", "'components' must be an object.");
        if (!comps.contains("Transform")) {
            Json full = Json::object();
            full["Transform"] = Registry::global().find("Transform")->defaultJson(Visibility::Authoring);
            for (auto& [k, v] : comps.items()) full[k] = v;
            comps = std::move(full);
        }
        for (auto& [cname, value] : comps.items()) {
            const ComponentMeta* m = requireMeta(ctx, cname);
            if (!m) return false;
            Json merged = m->defaultJson(Visibility::Authoring);
            if (value.is_object()) for (auto& [k, v] : value.items()) merged[k] = v;
            merged = canonicalizeFloats(merged);
            if (!validateValue(ctx, *m, merged, doc, "/components/" + cname, id)) return false;
            value = std::move(merged);
        }
        p["components"] = std::move(comps);
    }
    if (!ctx.changes.addDocument(doc, Project::canonicalizeDocument(p))) return setBuilderError(ctx);
    ctx.result = Json{{"id", id}, {"doc", doc}, {"name", name}};
    return true;
}

bool prefabInstantiate(CommandContext& ctx) {
    const Json& a = ctx.args;
    if (!a.contains("prefab") || !a["prefab"].is_string()) return ctx.fail(ErrorCategory::Usage, "ARG_REQUIRED", "prefab.instantiate needs 'prefab'.");
    auto p = ctx.resolvePrefab(a["prefab"].get<std::string>());
    if (!p) return false;
    Json createArgs = Json::object();
    if (a.contains("name") && a["name"].is_string()) createArgs["name"] = a["name"];
    else createArgs["name"] = ctx.project.document(p->doc)->value("name", std::string("Instance"));
    createArgs["prefab"] = p->id;
    if (a.contains("world")) createArgs["world"] = a["world"];
    if (a.contains("parent")) createArgs["parent"] = a["parent"];
    if (a.contains("order")) createArgs["order"] = a["order"];
    if (a.contains("tags")) createArgs["tags"] = a["tags"];
    Json set = a.value("set", Json::object());
    if (a.contains("position")) set["/components/Transform/position"] = a["position"];
    if (!set.empty()) createArgs["set"] = canonicalizeFloats(set);
    CommandContext sub{ctx.project, createArgs, ctx.changes, ctx.actor};
    if (!entityCreate(sub)) { ctx.error = sub.error; return false; }
    ctx.warnings.insert(ctx.warnings.end(), sub.warnings.begin(), sub.warnings.end());
    ctx.result = sub.result;
    ctx.result["prefab"] = p->id;
    return true;
}

// ---------------------------------------------------------------- asset.import (ADR-0037)
//   The PNG already lives under Assets/ (the user drops it there); this writes the sidecar next to it with a fresh
//   asset_ id and the sprite sub-assets: a grid (cellWidth × cellHeight, row-major, one name per cell) or, without a
//   grid, one sprite covering the whole image named after the file stem.
bool assetImport(CommandContext& ctx) {
    const Json& a = ctx.args;
    if (!a.contains("source") || !a["source"].is_string() || a["source"].get<std::string>().empty())
        return ctx.fail(ErrorCategory::Usage, "ARG_REQUIRED", "asset.import needs 'source' (project-relative PNG path under Assets/, e.g. Assets/Textures/arena.png).");
    std::string source = a["source"].get<std::string>();
    std::replace(source.begin(), source.end(), '\\', '/');
    if (source.rfind("Assets/", 0) != 0) return ctx.fail(ErrorCategory::Usage, "ARG_TYPE", "'source' must be inside Assets/ (got '" + source + "').", Json{{"source", source}});
    const std::string doc = source + ".meta.json";
    if (ctx.project.document(doc)) return ctx.fail(ErrorCategory::Conflict, "DOCUMENT_EXISTS", doc + " already exists (edit it with document.patch).", Json{{"doc", doc}});
    const std::string abs = ctx.project.rootDir() + "/" + source;
    Json m = Json::object();
    m["$schema"] = "game://schema/asset-meta/1";
    m["schemaVersion"] = 1;
    m["id"] = (a.contains("id") && a["id"].is_string() && !a["id"].get<std::string>().empty()) ? a["id"].get<std::string>() : Id::generate("asset").str();
    if (!Id::validate(m["id"].get<std::string>()).empty() || Id::parse(m["id"].get<std::string>())->prefix() != "asset") return ctx.fail(ErrorCategory::Usage, "ARG_TYPE", "'id' must be a valid asset_ id.");
    m["source"] = source;
    // ADR-0046: .ttf/.otf → Font sidecar (no sub-assets; TextRenderer.font references the asset whole, size per component)
    std::string ext = source.substr(source.find_last_of('.') == std::string::npos ? source.size() : source.find_last_of('.'));
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (ext == ".ttf" || ext == ".otf" || ext == ".ttc") {
        if (!fontFileSignature(abs)) return ctx.fail(ErrorCategory::NotFound, "ASSET_SOURCE_MISSING", source + " is not a readable TrueType/OpenType font in this project.", Json{{"source", source}});
        m["importer"] = "Font";
        m["importerVersion"] = 1;
        m["settings"] = Json::object();
        if (!ctx.changes.addDocument(doc, m)) return setBuilderError(ctx);
        ctx.result = Json{{"id", m["id"]}, {"doc", doc}, {"source", source}, {"importer", "Font"}, {"hint", "TextRenderer.font = \"" + m["id"].get<std::string>() + "\", TextRenderer.size = pixel height"}};
        return true;
    }
    int imgW = 0, imgH = 0;
    if (!pngDimensions(abs, imgW, imgH)) return ctx.fail(ErrorCategory::NotFound, "ASSET_SOURCE_MISSING", source + " is not a readable PNG in this project.", Json{{"source", source}});
    m["importer"] = "Texture2D";
    m["importerVersion"] = 1;
    Json settings = Json::object();
    std::string filter = a.value("filter", "nearest");
    if (filter != "nearest" && filter != "linear") return ctx.fail(ErrorCategory::Usage, "ARG_TYPE", "'filter' must be nearest or linear.");
    settings["filter"] = filter;
    double ppu = a.contains("pixelsPerUnit") && a["pixelsPerUnit"].is_number() ? a["pixelsPerUnit"].get<double>() : 16.0;
    if (ppu <= 0) return ctx.fail(ErrorCategory::Usage, "ARG_TYPE", "'pixelsPerUnit' must be positive.");
    settings["pixelsPerUnit"] = ppu;
    m["settings"] = settings;
    Json pivot = a.contains("pivot") && a["pivot"].is_array() && a["pivot"].size() == 2 ? a["pivot"] : Json::array({0.5, 0.5});
    Json subs = Json::array();
    if (a.contains("grid")) {
        const Json& g = a["grid"];
        if (!g.is_object() || !g.contains("cellWidth") || !g.contains("cellHeight") || !g["cellWidth"].is_number_integer() || !g["cellHeight"].is_number_integer())
            return ctx.fail(ErrorCategory::Usage, "ARG_TYPE", "'grid' must be {cellWidth, cellHeight} in pixels.");
        const int cw = g["cellWidth"].get<int>(), ch = g["cellHeight"].get<int>();
        if (cw <= 0 || ch <= 0 || cw > imgW || ch > imgH) return ctx.fail(ErrorCategory::Usage, "ARG_TYPE", "grid cell " + std::to_string(cw) + "x" + std::to_string(ch) + " does not fit the image " + std::to_string(imgW) + "x" + std::to_string(imgH) + ".");
        const int cols = imgW / cw, rows = imgH / ch;
        if (!a.contains("names") || !a["names"].is_array() || a["names"].empty())
            return ctx.fail(ErrorCategory::Usage, "ARG_REQUIRED", "a grid needs 'names' (one per cell, row-major; sub-assets are addressed by name, §88.7). The image has " + std::to_string(cols) + "x" + std::to_string(rows) + " cells.");
        const Json& names = a["names"];
        if (static_cast<int>(names.size()) > cols * rows) return ctx.fail(ErrorCategory::Usage, "ARG_TYPE", std::to_string(names.size()) + " names but the grid has only " + std::to_string(cols * rows) + " cells.");
        int i = 0;
        for (const auto& n : names) {
            if (!n.is_string() || n.get<std::string>().empty()) return ctx.fail(ErrorCategory::Usage, "ARG_TYPE", "'names' must be non-empty strings.");
            for (const auto& prev : subs) if (prev["name"] == n) return ctx.fail(ErrorCategory::Usage, "ARG_TYPE", "duplicate sprite name '" + n.get<std::string>() + "'.");
            const int col = i % cols, row = i / cols;
            subs.push_back(Json{{"name", n}, {"kind", "sprite"}, {"rect", Json::array({col * cw, row * ch, cw, ch})}, {"pivot", pivot}});
            ++i;
        }
    } else {
        std::string stem = source.substr(source.find_last_of('/') + 1);
        if (auto dot = stem.find('.'); dot != std::string::npos) stem = stem.substr(0, dot);
        std::string name = a.contains("names") && a["names"].is_array() && !a["names"].empty() && a["names"][0].is_string() ? a["names"][0].get<std::string>() : stem;
        subs.push_back(Json{{"name", name}, {"kind", "sprite"}, {"rect", Json::array({0, 0, imgW, imgH})}, {"pivot", pivot}});
    }
    m["subAssets"] = subs;
    if (!ctx.changes.addDocument(doc, m)) return setBuilderError(ctx);
    Json names = Json::array();
    for (const auto& s : subs) names.push_back(s["name"]);
    ctx.result = Json{{"id", m["id"]}, {"doc", doc}, {"source", source}, {"width", imgW}, {"height", imgH}, {"subAssets", names}};
    return true;
}

bool worldCreate(CommandContext& ctx) {
    const Json& a = ctx.args;
    if (!a.contains("name") || !a["name"].is_string() || a["name"].get<std::string>().empty())
        return ctx.fail(ErrorCategory::Usage, "ARG_REQUIRED", "world.create needs 'name'.");
    std::string name = a["name"].get<std::string>();
    std::string doc = "Worlds/" + sanitizeFileName(name) + ".world.json";
    if (ctx.project.document(doc)) return ctx.fail(ErrorCategory::Conflict, "DOCUMENT_EXISTS", doc + " already exists.", Json{{"doc", doc}});
    std::string id = Id::generate("world").str();
    Json w = Json::object();
    w["$schema"] = "game://schema/world/1";
    w["schemaVersion"] = 1;
    w["id"] = id;
    w["name"] = name;
    w["entities"] = Json::object();
    if (!ctx.changes.addDocument(doc, w)) return setBuilderError(ctx);
    ctx.result = Json{{"id", id}, {"doc", doc}, {"name", name}};
    return true;
}

bool documentPatch(CommandContext& ctx) {
    const Json& a = ctx.args;
    if (!a.contains("doc") || !a["doc"].is_string() || !a.contains("ops") || !a["ops"].is_array())
        return ctx.fail(ErrorCategory::Usage, "ARG_REQUIRED", "document.patch needs 'doc' and 'ops' (RFC 6902 array).");
    std::string doc = a["doc"].get<std::string>();
    if (!ctx.project.document(doc)) return ctx.fail(ErrorCategory::NotFound, "DOCUMENT_NOT_FOUND", doc + " is not a loaded document.", Json{{"doc", doc}});
    int n = 0;
    for (const auto& o : a["ops"]) {
        if (!o.is_object() || !o.contains("op") || !o.contains("path")) return ctx.fail(ErrorCategory::Usage, "ARG_TYPE", "each op needs 'op' and 'path'.");
        std::string op = o["op"].get<std::string>(), path = o["path"].get<std::string>();
        bool ok = false;
        if (op == "add") ok = ctx.changes.add(doc, path, o.value("value", Json()));
        else if (op == "replace") ok = ctx.changes.replace(doc, path, o.value("value", Json()));
        else if (op == "remove") ok = ctx.changes.remove(doc, path);
        else if (op == "move") ok = ctx.changes.move(doc, o.value("from", ""), path);
        else if (op == "test") ok = ctx.changes.test(doc, path, o.value("value", Json()));
        else return ctx.fail(ErrorCategory::Usage, "CHANGESET_OP_UNKNOWN", "op '" + op + "' is not allowed (copy is not invertible).");
        if (!ok) return setBuilderError(ctx);
        ++n;
    }
    ctx.result = Json{{"doc", doc}, {"applied", n}};
    return true;
}

} // namespace

void registerBuiltinCommands(CommandBus& bus) {
    using K = CommandKind;
    bus.registerCommand({"entity.create", K::Mutation, "Create an entity in a world (plain with components, or a prefab instance).",
                         schema({{"world", str("world id/selector; default = project.defaultWorld")}, {"name", str("entity name")}, {"parent", str("parent entity selector (omit = root)")},
                                 {"order", str("sibling order key (default: after last sibling)")}, {"tags", strArr("tags")}, {"components", obj("{Name: {prop: value}} — missing props take defaults; Transform is added if absent")},
                                 {"prefab", str("prefab selector → instance")}, {"set", obj("instance overrides {\"/components/X/prop\": value}")}}, {"name"}),
                         {"spawn_entity"}, entityCreate});
    bus.registerCommand({"entity.delete", K::Mutation, "Delete an entity (and its descendants unless recursive:false).",
                         schema({{"entity", str("entity selector")}, {"recursive", Json{{"type", "boolean"}, {"default", true}}}}, {"entity"}), {"despawn"}, entityDelete});
    bus.registerCommand({"entity.rename", K::Mutation, "Rename an entity.", schema({{"entity", str("entity selector")}, {"name", str("new name")}}, {"entity", "name"}), {}, entityRename});
    bus.registerCommand({"entity.reparent", K::Mutation, "Move an entity under another parent (null = root).",
                         schema({{"entity", str("entity selector")}, {"parent", Json{{"type", Json::array({"string", "null"})}}}, {"order", str("order key")}}, {"entity", "parent"}), {"reparent"}, entityReparent});
    bus.registerCommand({"component.add", K::Mutation, "Add a component (defaults merged with value). On prefab instances this becomes an 'add' override.",
                         schema({{"entity", str("entity selector")}, {"component", str("component name")}, {"value", obj("initial property values")}}, {"entity", "component"}), {"insert"}, componentAdd});
    bus.registerCommand({"component.remove", K::Mutation, "Remove a component. On prefab instances this becomes a 'remove' override.",
                         schema({{"entity", str("entity selector")}, {"component", str("component name")}}, {"entity", "component"}), {"remove"}, componentRemove});
    bus.registerCommand({"property.set", K::Mutation, "Set one property (JSON pointer inside the component). On prefab instances this becomes a 'set' override; setting the prefab value clears it.",
                         schema({{"entity", str("entity selector")}, {"component", str("component name")}, {"path", str("property pointer: \"max\", \"/position/0\"")}, {"value", Json{{"description", "new value (any JSON)"}}}}, {"entity", "component", "path", "value"}),
                         {"mutate_component"}, propertySet});
    bus.registerCommand({"tag.add", K::Mutation, "Add a tag to an entity.", schema({{"entity", str("entity selector")}, {"tag", str("tag")}}, {"entity", "tag"}), {}, [](CommandContext& c) { return tagAddRemove(c, true); }});
    bus.registerCommand({"tag.remove", K::Mutation, "Remove a tag from an entity.", schema({{"entity", str("entity selector")}, {"tag", str("tag")}}, {"entity", "tag"}), {}, [](CommandContext& c) { return tagAddRemove(c, false); }});
    bus.registerCommand({"prefab.create", K::Mutation, "Create Prefabs/<Name>.prefab.json (plain components or derived via base + set/add/remove). Transform is added if absent; missing props take defaults.",
                         schema({{"name", str("prefab name (also file name)")}, {"components", obj("{Name: {...}}")}, {"base", str("base prefab selector")}, {"set", obj("")}, {"add", obj("")}, {"remove", strArr("")}, {"tags", strArr("")}}, {"name"}), {}, prefabCreate});
    bus.registerCommand({"prefab.instantiate", K::Mutation, "Create a prefab instance entity in a world.",
                         schema({{"prefab", str("prefab selector")}, {"world", str("world selector; default = defaultWorld")}, {"name", str("instance name; default = prefab name")}, {"parent", str("parent entity selector")},
                                 {"position", Json{{"type", "array"}, {"description", "[x, y, z] shortcut for set /components/Transform/position"}}}, {"set", obj("overrides")}, {"tags", strArr("")}}, {"prefab"}), {}, prefabInstantiate});
    bus.registerCommand({"document.patch", K::Mutation, "Apply raw RFC 6902 ops to one document (escape hatch; used by validate --fix for artifactChanges). 'before' is recorded automatically.",
                         schema({{"doc", str("project-relative document path")}, {"ops", Json{{"type", "array"}, {"items", Json{{"type", "object"}}}}}}, {"doc", "ops"}), {}, documentPatch});
    bus.registerCommand({"world.create", K::Mutation, "Create Worlds/<Name>.world.json with no entities.", schema({{"name", str("world name (also file name)")}}, {"name"}), {}, worldCreate});
    bus.registerCommand({"asset.import", K::Mutation, "Create the Assets/<file>.meta.json sidecar for a PNG or a TTF/OTF font already under Assets/ (§37, ADR-0046). PNG: a fresh asset_ id plus sprite sub-assets — a grid with one name per cell (row-major) or one whole-image sprite; entities reference sprites as \"<id>#sprites/<name>\" in SpriteRenderer.sprite. Font: importer Font, referenced whole by TextRenderer.font (size per component; any Unicode the font covers, e.g. Korean).",
                         schema({{"source", str("project-relative path under Assets/: a PNG (Assets/Textures/arena.png) or a .ttf/.otf font (Assets/Fonts/NotoSansKR.ttf)")},
                                 {"grid", Json{{"type", "object"}, {"description", "{cellWidth, cellHeight} in pixels — slices the sheet row-major; requires names"}}},
                                 {"names", Json{{"type", "array"}, {"items", str("sprite name")}, {"description", "one per grid cell (row-major); without a grid, the single sprite's name (default: file stem)"}}},
                                 {"pixelsPerUnit", Json{{"type", "number"}, {"description", "world units = pixels / pixelsPerUnit (default 16)"}}},
                                 {"filter", Json{{"enum", Json::array({"nearest", "linear"})}, {"description", "texture sampling (default nearest — pixel art)"}}},
                                 {"pivot", Json{{"type", "array"}, {"description", "[x, y] in 0..1 inside each sprite (default [0.5, 0.5])"}}},
                                 {"id", str("optional explicit asset_ id (default: generated UUIDv7)")}}, {"source"}), {}, assetImport});
}

} // namespace akeir
