// pme/reflection/Registry.h — runtime component registry + 문자열 주소 지정 get/set. 설계 문서 §42.2, §8 (SetProperty), §11 (CLI 경로), §14.1 (mutation path).
//
//   Registry::global().find("Health") → ComponentMeta*
//   getByPointer(meta, component, "/max")         → Json
//   setByPointer(meta, component, "/position/0", 5.0) → bool
//   경로는 component 내부 RFC 6901 JSON Pointer. leaf 배열은 index 로만 접근 (/position/0).
#pragma once

#include "pme/reflection/PropertyMeta.h"

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <typeindex>
#include <vector>

namespace pme {

class Registry {
public:
    static Registry& global();

    /// 같은 이름이 이미 있으면 덮어쓰지 않고 false (테스트에서 중복 등록 감지).
    bool add(ComponentMeta meta, std::type_index type);
    const ComponentMeta* find(std::string_view name) const;
    const ComponentMeta* find(std::type_index type) const;
    template <class T> const ComponentMeta* find() const { return find(std::type_index(typeid(T))); }
    /// 이름순 정렬된 목록 (결정적 순서 — schema 출력, capabilities, 해시에 쓴다)
    std::vector<const ComponentMeta*> all() const;
    std::size_t size() const { return byName_.size(); }
    void clear(); // 테스트 전용

private:
    std::map<std::string, std::unique_ptr<ComponentMeta>, std::less<>> byName_;
    std::map<std::type_index, ComponentMeta*> byType_;
};

/// component 내부 경로로 읽기. 경로가 잘못되면 nullopt.
std::optional<Json> getByPointer(const ComponentMeta& meta, const void* component, std::string_view pointer);
/// component 내부 경로로 쓰기. 성공 시 true. ReadOnly 속성은 allowReadOnly=false 면 거부.
bool setByPointer(const ComponentMeta& meta, void* component, std::string_view pointer, const Json& value, bool allowReadOnly = false);
/// "/max" → ("max", nullopt) / "/position/0" → ("position", 0). 형식 오류면 nullopt.
std::optional<std::pair<std::string, std::optional<std::size_t>>> splitPropertyPointer(std::string_view pointer);

/// 모든 등록 component 의 schema 를 하나의 객체로 (§14 `akeir schema --all`)
Json allSchemas();

} // namespace pme
