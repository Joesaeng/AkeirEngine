// akeir/reflection/PropertyMeta.cpp — schema / wire_format / default 생성. 설계 문서 §14, §14.1, §42.2, §43.1, §88.8
#include "akeir/reflection/PropertyMeta.h"

namespace akeir {

const char* propTypeName(PropType t) {
    switch (t) {
    case PropType::Bool: return "bool"; case PropType::Int: return "int"; case PropType::Float: return "float";
    case PropType::String: return "string"; case PropType::Enum: return "enum"; case PropType::Vec2: return "vec2";
    case PropType::Vec3: return "vec3"; case PropType::Vec4: return "vec4"; case PropType::Quat: return "quat";
    case PropType::Color: return "color"; case PropType::Ref: return "ref";
    }
    return "?";
}

const char* jsonSchemaType(PropType t) {
    switch (t) {
    case PropType::Bool: return "boolean";
    case PropType::Int: return "integer";
    case PropType::Float: return "number";
    case PropType::String: case PropType::Enum: case PropType::Ref: return "string";
    default: return "array";
    }
}

std::size_t arity(PropType t) {
    switch (t) {
    case PropType::Vec2: return 2; case PropType::Vec3: return 3;
    case PropType::Vec4: case PropType::Quat: case PropType::Color: return 4;
    default: return 0;
    }
}

bool isVisible(PropFlags f, Visibility v) {
    // §88.8 유일한 정의
    if (has(f, PropFlags::Transient)) return false;
    switch (v) {
    case Visibility::Authoring: return !has(f, PropFlags::RuntimeOnly);
    case Visibility::Snapshot: return true;
    case Visibility::Save: return has(f, PropFlags::Save);
    }
    return false;
}

Json PropertyMeta::toSchema() const {
    Json s = Json::object();
    s["type"] = jsonSchemaType(type);
    if (std::size_t n = arity(type)) {
        s["items"] = Json{{"type", "number"}};
        s["minItems"] = n;
        s["maxItems"] = n;
    }
    if (type == PropType::Enum) s["enum"] = enumOptions;
    if (type == PropType::Ref) s["pattern"] = "^$|^[a-z][a-z_]*_[0-7][0-9a-hjkmnp-tv-z]{25}(#.+)?$";
    if (!defaultValue.is_null()) s["default"] = defaultValue;
    if (minimum) s["minimum"] = *minimum;
    if (maximum) s["maximum"] = *maximum;
    if (multipleOf) s["multipleOf"] = *multipleOf;
    if (!description.empty()) s["description"] = description;
    if (has(flags, PropFlags::ReadOnly) || has(flags, PropFlags::RuntimeOnly)) s["readOnly"] = true;
    // x-* 확장 (§14)
    s["x-cpp"] = propTypeName(type);
    if (warnMin || warnMax) { Json w = Json::object(); if (warnMin) w["min"] = *warnMin; if (warnMax) w["max"] = *warnMax; s["x-warn"] = w; }
    if (uiMin || uiMax || step) {
        Json ui = Json::object();
        ui["widget"] = (type == PropType::Enum) ? "combo" : (type == PropType::Bool) ? "checkbox" : (type == PropType::Ref) ? "picker" : (type == PropType::Color) ? "color" : (uiMin && uiMax) ? "slider" : "field";
        if (uiMin) ui["uiMin"] = *uiMin; if (uiMax) ui["uiMax"] = *uiMax; if (step) ui["step"] = *step;
        s["x-ui"] = ui;
    }
    if (!unit.empty()) s["x-unit"] = unit;
    if (!category.empty()) s["x-category"] = category;
    if (has(flags, PropFlags::RuntimeOnly)) s["x-runtimeOnly"] = true;
    if (has(flags, PropFlags::Hidden)) s["x-hidden"] = true;
    if (has(flags, PropFlags::Advanced)) s["x-advanced"] = true;
    if (has(flags, PropFlags::Transient)) s["x-transient"] = true;
    if (has(flags, PropFlags::Save)) s["x-save"] = true;
    if (has(flags, PropFlags::Ref)) s["x-ref"] = refType.empty() ? "any" : refType;
    return s;
}

const PropertyMeta* ComponentMeta::find(std::string_view propName) const {
    for (const auto& p : props) if (p.name == propName) return &p;
    return nullptr;
}

Json ComponentMeta::toSchema() const {
    Json s = Json::object();
    s["$schema"] = "https://json-schema.org/draft/2020-12/schema";
    s["$id"] = "game://schema/component/" + name + "/" + std::to_string(version);
    s["title"] = name;
    if (!description.empty()) s["description"] = description;
    s["type"] = "object";
    s["x-component-version"] = version;
    if (!requiresComponents.empty()) s["x-requires"] = requiresComponents;
    if (!lifecycle.empty()) s["x-lifecycle"] = lifecycle;
    Json props_ = Json::object();
    Json required = Json::array();
    for (const auto& p : props) {
        props_[p.name] = p.toSchema();
        if (has(p.flags, PropFlags::Required)) required.push_back(p.name);
    }
    s["properties"] = props_;
    if (!required.empty()) s["required"] = required;
    s["additionalProperties"] = false;
    return s;
}

Json ComponentMeta::toWireFormat() const {
    // §14.1: type schema 만으로는 부족 — 실제 보낼 JSON 예시와 유효한 mutation path 목록
    Json w = Json::object();
    w["type"] = name;
    w["spawnExample"] = defaultJson(Visibility::Authoring);
    Json paths = Json::array();
    Json enums = Json::object();
    for (const auto& p : props) {
        if (has(p.flags, PropFlags::Transient)) continue;
        paths.push_back("/" + p.name);
        for (std::size_t i = 0, n = arity(p.type); i < n; ++i) paths.push_back("/" + p.name + "/" + std::to_string(i));
        if (p.type == PropType::Enum) enums["/" + p.name] = p.enumOptions;
    }
    w["mutationPaths"] = paths;
    if (!enums.empty()) w["enumFormats"] = enums;
    Json ro = Json::array();
    for (const auto& p : props) if (has(p.flags, PropFlags::ReadOnly) || has(p.flags, PropFlags::RuntimeOnly)) ro.push_back("/" + p.name);
    if (!ro.empty()) w["readOnlyPaths"] = ro;
    return w;
}

Json ComponentMeta::defaultJson(Visibility v) const {
    Json j = Json::object();
    for (const auto& p : props) if (isVisible(p.flags, v)) j[p.name] = p.defaultValue;
    return j;
}

} // namespace akeir
