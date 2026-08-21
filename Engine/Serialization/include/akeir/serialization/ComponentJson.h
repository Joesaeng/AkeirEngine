// akeir/serialization/ComponentJson.h — reflection 기반 component ↔ JSON + schema 수준 검증. 설계 문서 §14 (schema), §26.1/§88.8 (visibility), §29 (검증 코드), §42.2, §79 (Diagnostic).
//
//   componentToJson(meta, comp, Visibility)   → canonical float 로 정규화된 JSON (§5.3)
//   validateComponentJson(meta, json, where)  → Diagnostic[] (PROPERTY_UNKNOWN / TYPE_MISMATCH / OUT_OF_RANGE / ENUM_VALUE_INVALID / REF_FORMAT_INVALID / RUNTIME_ONLY_IN_AUTHORING / REQUIRED_MISSING)
//   componentFromJson(meta, comp, json, Visibility) → 검증 후 적용. error 가 있으면 적용하지 않는다(all-or-nothing)
#pragma once

#include "akeir/core/Diagnostic.h"
#include "akeir/reflection/PropertyMeta.h"

#include <vector>

namespace akeir {

Json componentToJson(const ComponentMeta& meta, const void* component, Visibility v);

/// where.uri / where.jsonPointer 는 component 객체의 위치 (예 "/entities/entity_…/components/Health"). 속성별 진단은 그 아래 경로를 가리킨다.
std::vector<Diagnostic> validateComponentJson(const ComponentMeta& meta, const Json& json, const PhysicalLocation& where,
                                              Visibility v = Visibility::Authoring, const std::string& entityId = "");

struct ApplyResult {
    bool ok = true;                        // error 없음 (warning 은 ok)
    std::vector<Diagnostic> diagnostics;
};
ApplyResult componentFromJson(const ComponentMeta& meta, void* component, const Json& json, Visibility v = Visibility::Authoring,
                              const PhysicalLocation& where = {}, const std::string& entityId = "");

} // namespace akeir
