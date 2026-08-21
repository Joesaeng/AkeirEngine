// pme/serialization/ComponentJson.cpp — 설계 문서 §14, §26.1, §29, §79, §88.8
#include "pme/serialization/ComponentJson.h"
#include "pme/core/Ref.h"
#include "pme/serialization/Canonical.h"

namespace pme {

Json componentToJson(const ComponentMeta& meta, const void* component, Visibility v) {
    Json j = Json::object();
    for (const auto& p : meta.props) {
        if (!isVisible(p.flags, v)) continue;
        j[p.name] = canonicalizeFloats(p.get(component));
    }
    return j;
}

namespace {

bool typeMatches(const PropertyMeta& p, const Json& v) {
    switch (p.type) {
    case PropType::Bool: return v.is_boolean();
    case PropType::Int: return v.is_number_integer();
    case PropType::Float: return v.is_number();
    case PropType::String: case PropType::Enum: case PropType::Ref: return v.is_string();
    default: {
        std::size_t n = arity(p.type);
        if (!v.is_array() || v.size() != n) return false;
        for (const auto& e : v) if (!e.is_number()) return false;
        return true;
    }
    }
}

std::string expectedShape(const PropertyMeta& p) {
    switch (p.type) {
    case PropType::Bool: return "boolean";
    case PropType::Int: return "integer";
    case PropType::Float: return "number";
    case PropType::String: return "string";
    case PropType::Enum: return "string (one of the enum values)";
    case PropType::Ref: return "string id (e.g. entity_01j5xq8z3mf0n9k2c7p4rtvw6y or asset_…#kind/name)";
    default: return "array of " + std::to_string(arity(p.type)) + " numbers";
    }
}

PhysicalLocation at(const PhysicalLocation& where, const std::string& key) {
    PhysicalLocation pl = where;
    pl.jsonPointer = where.jsonPointer + "/" + key;
    return pl;
}

LogicalLocation logicalFor(const std::string& entityId, const ComponentMeta& meta, const std::string& prop) {
    LogicalLocation l;
    l.object = entityId;
    l.component = meta.name;
    l.propertyPath = "/" + prop;
    return l;
}

Fix setFix(const std::string& entityId, const ComponentMeta& meta, const std::string& prop, Json value, Applicability app, const std::string& desc) {
    Fix f;
    f.description = desc;
    f.applicability = app;
    f.isPreferred = true;
    f.commands.push_back(CommandInvocation{"property.set", Json{{"entity", entityId}, {"component", meta.name}, {"path", "/" + prop}, {"value", value}}});
    return f;
}

} // namespace

std::vector<Diagnostic> validateComponentJson(const ComponentMeta& meta, const Json& json, const PhysicalLocation& where, Visibility v, const std::string& entityId) {
    std::vector<Diagnostic> out;
    if (!json.is_object()) {
        out.push_back(Diagnostic::error("COMPONENT_NOT_OBJECT", "Component '" + meta.name + "' must be a JSON object.").in(where)
                          .at(LogicalLocation{entityId, meta.name, std::nullopt}));
        return out;
    }
    // 알 수 없는 키 / runtimeOnly 가 authoring 에 있음
    for (auto it = json.begin(); it != json.end(); ++it) {
        const PropertyMeta* p = meta.find(it.key());
        if (!p) {
            Diagnostic d = Diagnostic::error("PROPERTY_UNKNOWN", "Component '" + meta.name + "' has no property '" + it.key() + "'. See `game schema component " + meta.name + "`.")
                               .in(at(where, it.key())).at(LogicalLocation{entityId, meta.name, "/" + it.key()});
            Fix f; f.description = "Remove unknown property"; f.applicability = Applicability::MaybeIncorrect;
            f.artifactChanges.push_back(Json{{"op", "remove"}, {"path", where.jsonPointer + "/" + it.key()}});
            d.withFix(f);
            out.push_back(std::move(d));
            continue;
        }
        if (v == Visibility::Authoring && has(p->flags, PropFlags::RuntimeOnly)) {
            Diagnostic d = Diagnostic::warning("RUNTIME_ONLY_IN_AUTHORING", "Property '" + p->name + "' is runtimeOnly and is ignored in authoring files.")
                               .in(at(where, it.key())).at(logicalFor(entityId, meta, p->name));
            Fix f; f.description = "Remove runtimeOnly property from authoring file"; f.applicability = Applicability::MachineApplicable; f.isPreferred = true;
            f.artifactChanges.push_back(Json{{"op", "remove"}, {"path", where.jsonPointer + "/" + it.key()}});
            d.withFix(f);
            out.push_back(std::move(d));
            continue;
        }
        const Json& val = it.value();
        if (!typeMatches(*p, val)) {
            out.push_back(Diagnostic::error("PROPERTY_TYPE_MISMATCH", "Property '" + p->name + "' expects " + expectedShape(*p) + ".")
                              .in(at(where, it.key())).at(logicalFor(entityId, meta, p->name))
                              .withFix(setFix(entityId, meta, p->name, p->defaultValue, Applicability::MaybeIncorrect, "Reset to default value")));
            continue;
        }
        if (p->type == PropType::Enum) {
            const std::string s = val.get<std::string>();
            bool found = false;
            for (const auto& o : p->enumOptions) if (o == s) { found = true; break; }
            if (!found) {
                Diagnostic d = Diagnostic::error("ENUM_VALUE_INVALID", "Property '" + p->name + "' must be one of: " + Json(p->enumOptions).dump() + ".")
                                   .in(at(where, it.key())).at(logicalFor(entityId, meta, p->name));
                for (const auto& o : p->enumOptions) d.withFix(setFix(entityId, meta, p->name, o, Applicability::MaybeIncorrect, "Set to \"" + o + "\""));
                if (!d.fixes.empty()) { d.fixes[0].isPreferred = true; for (std::size_t k = 1; k < d.fixes.size(); ++k) d.fixes[k].isPreferred = false; }
                out.push_back(std::move(d));
            }
            continue;
        }
        if (p->type == PropType::Ref) {
            std::string err = Ref::validate(val.get<std::string>());
            if (!err.empty())
                out.push_back(Diagnostic::error("REF_FORMAT_INVALID", "Property '" + p->name + "': " + err + ".")
                                  .in(at(where, it.key())).at(logicalFor(entityId, meta, p->name)));
            continue;
        }
        if (p->type == PropType::Int || p->type == PropType::Float) {
            double d = val.get<double>();
            if ((p->minimum && d < *p->minimum) || (p->maximum && d > *p->maximum)) {
                double clamped = d;
                if (p->minimum && d < *p->minimum) clamped = *p->minimum;
                if (p->maximum && d > *p->maximum) clamped = *p->maximum;
                Json cv = (p->type == PropType::Int) ? Json(static_cast<std::int64_t>(clamped)) : Json(clamped);
                out.push_back(Diagnostic::error("PROPERTY_OUT_OF_RANGE", "Property '" + p->name + "' = " + val.dump() + " is outside [" +
                                                    (p->minimum ? Json(*p->minimum).dump() : "-inf") + ", " + (p->maximum ? Json(*p->maximum).dump() : "inf") + "].")
                                  .in(at(where, it.key())).at(logicalFor(entityId, meta, p->name))
                                  .withFix(setFix(entityId, meta, p->name, cv, Applicability::MachineApplicable, "Clamp to range")));
            } else if ((p->warnMin && d < *p->warnMin) || (p->warnMax && d > *p->warnMax)) {
                out.push_back(Diagnostic::warning("PROPERTY_OUT_OF_WARN_RANGE", "Property '" + p->name + "' = " + val.dump() + " is outside the recommended range [" +
                                                      (p->warnMin ? Json(*p->warnMin).dump() : "-inf") + ", " + (p->warnMax ? Json(*p->warnMax).dump() : "inf") + "].")
                                  .in(at(where, it.key())).at(logicalFor(entityId, meta, p->name)));
            }
            if (p->multipleOf && *p->multipleOf > 0) {
                double q = d / *p->multipleOf;
                if (std::fabs(q - std::round(q)) > 1e-9)
                    out.push_back(Diagnostic::error("PROPERTY_NOT_MULTIPLE", "Property '" + p->name + "' must be a multiple of " + Json(*p->multipleOf).dump() + ".")
                                      .in(at(where, it.key())).at(logicalFor(entityId, meta, p->name)));
            }
        }
    }
    // required 누락 — authoring 문서(전체)에만 적용. snapshot/save 는 runtime 상태 부분 적용이므로 검사하지 않는다
    for (const auto& p : meta.props) {
        if (v == Visibility::Authoring && has(p.flags, PropFlags::Required) && isVisible(p.flags, v) && !json.contains(p.name)) {
            out.push_back(Diagnostic::error("PROPERTY_REQUIRED_MISSING", "Component '" + meta.name + "' requires property '" + p.name + "'.")
                              .in(where).at(logicalFor(entityId, meta, p.name))
                              .withFix(setFix(entityId, meta, p.name, p.defaultValue, Applicability::MachineApplicable, "Add with default value")));
        }
    }
    return out;
}

ApplyResult componentFromJson(const ComponentMeta& meta, void* component, const Json& json, Visibility v, const PhysicalLocation& where, const std::string& entityId) {
    ApplyResult r;
    r.diagnostics = validateComponentJson(meta, json, where, v, entityId);
    for (const auto& d : r.diagnostics) if (d.level == Severity::Error) r.ok = false;
    if (!r.ok || !json.is_object()) return r;
    for (auto it = json.begin(); it != json.end(); ++it) {
        const PropertyMeta* p = meta.find(it.key());
        if (!p || !isVisible(p->flags, v)) continue;
        p->set(component, it.value());
    }
    return r;
}

} // namespace pme
