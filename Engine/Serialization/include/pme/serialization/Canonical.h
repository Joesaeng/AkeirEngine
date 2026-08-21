// pme/serialization/Canonical.h — Canonical Project JSON 직렬화. 설계 문서 §5.3.
//
//   Command layer 가 쓰는 모든 프로젝트 파일은 이 writer 하나로만 출력한다. 규약:
//     - UTF-8, BOM 없음, LF, 파일 끝 개행, 2-space indent
//     - 객체는 여러 줄, 스칼라만 담긴 배열은 한 줄 ([0, 0, 0]) — vec/quat/color 가 읽히게
//     - 키 순서 = Json(ordered_json) 에 들어 있는 순서. 정렬은 호출자(문서 모델)가 §5.3 규칙대로 미리 한다
//     - float32 값은 shortest-round-trip ("0.3", "0.30000001192092896" 아님) — 호출자는 canonicalizeFloat() 로 넣는다
//     - NaN/Inf 금지 → 예외 대신 false 반환
//     - 해시는 RFC 8785 JCS (jcsDump) 로, pretty 형식을 해시하지 않는다
#pragma once

#include "pme/core/Json.h"

#include <optional>
#include <string>
#include <string_view>

namespace pme {

/// pretty canonical 텍스트. NaN/Inf 가 있으면 nullopt.
std::optional<std::string> canonicalDump(const Json& doc);

/// RFC 8785 JSON Canonicalization Scheme 에 따른 바이트 (해시용). 키는 UTF-16 code unit 순 정렬, 공백 없음.
std::string jcsDump(const Json& doc);

/// 파일을 canonical 텍스트로 쓴다 (temp + rename, §9.2). 성공 시 true.
bool writeCanonicalFile(const std::string& path, const Json& doc, std::string* error = nullptr);

/// 파일 → Json. 주석/trailing comma 는 허용하지 않는다 (§5.3). 실패 시 nullopt + error (byte 위치 포함).
std::optional<Json> readJsonFile(const std::string& path, std::string* error = nullptr);
std::optional<Json> parseJson(std::string_view text, std::string* error = nullptr);

/// float32 를 "가장 짧은 round-trip 십진수" 의 double 로 (§5.3 숫자 규칙). 0.3f → 0.3 (double), 직렬화하면 "0.3".
double canonicalizeFloat(float v);
/// Json 트리 안의 모든 number_float 를 float32 canonical 로 정규화한 복사본 (component JSON 에 적용)
Json canonicalizeFloats(const Json& j);

/// 문서가 canonical 텍스트와 byte-identical 인지 (JSON_NOT_CANONICAL 검사, §29)
bool isCanonicalText(std::string_view fileText, const Json& parsed);

} // namespace pme
