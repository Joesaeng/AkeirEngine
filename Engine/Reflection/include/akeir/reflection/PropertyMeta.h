// akeir/reflection/PropertyMeta.h — 단일 진실 원천 메타데이터 테이블. 설계 문서 §42.2 (PropertyMeta), §43 (한 metadata → serialization/Inspector/schema/CLI/MCP/validation), §43.1 (속성 어휘).
//
//   소비자(Serializer, Inspector, Schema, CLI, MCP, Validation)는 이 테이블만 본다. 테이블을 채우는 front-end(지금은 매크로,
//   나중에 C++26 annotation)는 교체 가능하다. §42.2 "Backend 교체 계획".
//
//   속성 어휘 (§43.1 좌측 열 전부):
//     minimum/maximum (hard → validation ERROR) · warnMin/warnMax (soft → WARNING) · uiMin/uiMax/step (slider)
//     default · description · unit · runtimeOnly · hidden/advanced · enum options · category · ref type filter
//   Visibility mask (§88.8, 유일한 정의):
//     authoring = !RuntimeOnly && !Transient    snapshot = !Transient    save = Save && !Transient
#pragma once

#include "akeir/core/Json.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

namespace akeir {

enum class PropFlags : std::uint16_t {
    None = 0,
    RuntimeOnly = 1 << 0,  // runtime 에서만 의미. authoring 파일에 쓰지 않는다 (§26.1)
    ReadOnly = 1 << 1,     // 외부(CLI/MCP/Inspector)에서 쓰기 금지
    Hidden = 1 << 2,       // Inspector 에 표시 안 함
    Advanced = 1 << 3,     // Inspector 기본 뷰에서 접힘
    Transient = 1 << 4,    // 어디에도 직렬화하지 않음 (snapshot 포함)
    Required = 1 << 5,     // JSON Schema required
    Save = 1 << 6,         // save game 에 포함 (§88.8)
    Ref = 1 << 7,          // persistent id 참조 (reference graph 대상, §19)
};
constexpr PropFlags operator|(PropFlags a, PropFlags b) { return static_cast<PropFlags>(static_cast<std::uint16_t>(a) | static_cast<std::uint16_t>(b)); }
constexpr bool has(PropFlags v, PropFlags f) { return (static_cast<std::uint16_t>(v) & static_cast<std::uint16_t>(f)) != 0; }

/// Phase 1 leaf 타입. 중첩 struct 는 reflect 하지 않는다 (§42.2 "중첩 경로").
enum class PropType { Bool, Int, Float, String, Enum, Vec2, Vec3, Vec4, Quat, Color, Ref };
/// 이름. (doctest 가 unqualified toString() 을 ADL 로 찾아 충돌하므로 toString 이라는 이름을 쓰지 않는다)
const char* propTypeName(PropType t);
inline std::ostream& operator<<(std::ostream& os, PropType t) { return os << propTypeName(t); }
/// JSON Schema "type" (§14): Bool→boolean, Int→integer, Float→number, String/Enum/Ref→string, Vec*/Quat/Color→array
const char* jsonSchemaType(PropType t);
/// 배열 leaf 의 길이 (Vec2=2 … Color=4), 아니면 0
std::size_t arity(PropType t);

/// 직렬화 소비자 (§88.8)
enum class Visibility { Authoring, Snapshot, Save };
bool isVisible(PropFlags flags, Visibility v);

struct PropertyMeta {
    std::string name;                 // JSON key (camelCase)
    std::string description;          // tooltip / schema description / CLI help
    PropType type = PropType::Float;
    Json defaultValue;                // JSON Schema 'default' (JSON 표현: vec 는 배열, enum 은 문자열)
    std::optional<double> minimum, maximum, multipleOf;   // hard clamp → validation ERROR
    std::optional<double> warnMin, warnMax;               // soft range → validation WARNING (Flecs warning_range)
    std::optional<double> uiMin, uiMax, step;             // slider only (Inspector)
    std::string unit;                 // "m", "m/s", "deg", "hp"
    std::string category;             // Inspector group
    std::string refType;              // Ref 일 때: "entity" | "prefab" | "asset" | "asset:texture" …
    std::vector<std::string> enumOptions;
    PropFlags flags = PropFlags::None;

    // 접근자 — component 메모리에서 JSON 표현으로, 또는 그 반대. Reflection builder 가 채운다.
    std::size_t offset = 0;
    std::function<Json(const void* component)> get;
    std::function<bool(void* component, const Json& value)> set;   // 타입 불일치면 false

    /// §14 JSON Schema 2020-12 property 조각 (+ x-* 확장)
    Json toSchema() const;
};

struct ComponentMeta {
    std::string name;                 // PascalCase, C++ 타입 이름과 같다
    std::string description;
    int version = 1;                  // x-component-version (§53)
    std::vector<std::string> requiresComponents; // x-requires (§14, §29 COMPONENT_DEPENDENCY_MISSING). ('requires' 는 C++20 키워드)
    Json lifecycle = Json::object();  // x-lifecycle {init, tick, destroy} (§18)
    std::vector<PropertyMeta> props;
    /// Reflection completeness (ADR-0035): number of data members the aggregate declares (aggregateArity<T>())
    /// and the members deliberately left out with AKEIR_SKIP. Registry::add reports REFLECT_MEMBER_UNLISTED
    /// when props.size() + skipped.size() != memberCount. 0 = unknown (meta built without ComponentBuilder).
    std::size_t memberCount = 0;
    struct SkippedMember { std::string name, reason; };
    std::vector<SkippedMember> skipped;
    std::size_t size = 0, align = 0;
    std::function<void(void*)> construct;   // placement default-construct
    std::function<void(void*)> destroy;
    std::function<void(void* dst, const void* src)> copy;   // copy-assign (std::string 멤버를 가진 component 를 ECS 저장소로 안전하게 옮기기 위해)
    std::function<void(void* dst, void* src)> move;         // move-assign
    bool triviallyCopyable = false;

    const PropertyMeta* find(std::string_view propName) const;
    /// §14 전체 schema 문서
    Json toSchema() const;
    /// §14.1 wire_format: spawnExample + mutationPaths + enumFormats
    Json toWireFormat() const;
    /// default 값으로 채운 JSON (component.add 가 value 로 쓴다, §78.1)
    Json defaultJson(Visibility v = Visibility::Authoring) const;
};

} // namespace akeir
