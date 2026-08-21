// akeir/core/Json.h — 프로젝트 전체의 JSON 문서 타입.
// 설계 문서 §5.3: 출력 키 순서를 우리가 통제해야 하므로 nlohmann::ordered_json 을 문서 타입으로 쓴다
// (기본 nlohmann::json 은 std::map 으로 키를 알파벳 정렬한다).
// RFC 6901 JSON Pointer / RFC 6902 patch·diff 는 ordered_json 에서도 동작한다 (§78 ChangeSet 구현 의존성).
#pragma once

#include <nlohmann/json.hpp>

namespace akeir {

using Json = nlohmann::ordered_json;
using JsonPointer = Json::json_pointer;

} // namespace akeir
