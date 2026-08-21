// pme/core/Diagnostic.h — 구조화 진단. 설계 문서 §79 (SARIF 2.1.0 + LSP 3.17 + rustc applicability 합성), §13 (오류 envelope = Diagnostic + α).
//
//   Diagnostic {
//     level        note | warning | error
//     ruleId       SCREAMING_SNAKE (rule registry, §29)
//     message.text 사람·agent 용 — "고치는 법"을 말한다
//     logical      object / component / propertyPath          (SARIF logicalLocations)
//     physical     uri / jsonPointer / region                  (SARIF physicalLocation) — text-first 파일을 고칠 위치
//     related[]    부가 위치
//     fixes[]      description / applicability / isPreferred / commands[] / artifactChanges[]
//     fingerprint  실행 간 매칭 / baseline
//     helpUri      game://docs/rules/<RULE_ID>
//   }
//   Applicability: MachineApplicable (자동 적용 OK) | MaybeIncorrect (--fix=maybe) | HasPlaceholders (절대 자동 적용 안 함) | Unspecified
#pragma once

#include "pme/core/Json.h"

#include <optional>
#include <string>
#include <vector>

namespace pme {

enum class Severity { Note, Warning, Error };
enum class Applicability { MachineApplicable, MaybeIncorrect, HasPlaceholders, Unspecified };

const char* severityName(Severity s);
const char* applicabilityName(Applicability a);
std::optional<Severity> severityFromString(std::string_view s);
std::optional<Applicability> applicabilityFromString(std::string_view s);

struct Region {
    int startLine = 0, startColumn = 0, endLine = 0, endColumn = 0; // 1-based; 0 = 미지정
};

struct LogicalLocation {
    std::string object;                      // persistent id (entity_…, prefab_…, asset_…) 또는 빈 문자열
    std::optional<std::string> component;
    std::optional<std::string> propertyPath; // component 내부 JSON Pointer ("/max")
};

struct PhysicalLocation {
    std::string uri;                         // 프로젝트 상대 경로
    std::string jsonPointer;                 // RFC 6901, 문서 내 위치
    std::optional<Region> region;
};

struct Message { std::string text; };

struct RelatedLocation {
    Message message;
    std::optional<PhysicalLocation> physical;
    std::optional<LogicalLocation> logical;
};

struct CommandInvocation {
    std::string op;   // §8 command id (entity.create, component.add, …)
    Json args = Json::object();
};

struct Fix {
    std::string description;
    Applicability applicability = Applicability::Unspecified;
    bool isPreferred = false;
    std::vector<CommandInvocation> commands;   // Command-layer ops (선호)
    std::vector<Json> artifactChanges;         // RFC 6902 ops on the source file (선택)
    std::optional<std::string> cli;            // 같은 fix 의 CLI 한 줄
};

struct Diagnostic {
    Severity level = Severity::Error;
    std::string ruleId;
    Message message;
    std::optional<LogicalLocation> logical;
    std::optional<PhysicalLocation> physical;
    std::vector<RelatedLocation> related;
    std::vector<Fix> fixes;
    std::string fingerprint;                   // 비어 있으면 toJson 시 계산
    std::optional<std::string> helpUri;

    /// ruleId + logical + physical.uri/jsonPointer 에서 결정적 fingerprint 를 만든다 (SARIF §3.27.16).
    std::string computeFingerprint() const;
    Json toJson() const;
    static std::optional<Diagnostic> fromJson(const Json& j);

    static Diagnostic error(std::string ruleId, std::string text);
    static Diagnostic warning(std::string ruleId, std::string text);
    static Diagnostic note(std::string ruleId, std::string text);
    Diagnostic& at(LogicalLocation l) { logical = std::move(l); return *this; }
    Diagnostic& in(PhysicalLocation p) { physical = std::move(p); return *this; }
    Diagnostic& withFix(Fix f) { fixes.push_back(std::move(f)); return *this; }
};

/// 진단 묶음의 요약 ({error, warning, note} 카운트).
struct DiagnosticSummary {
    int error = 0, warning = 0, note = 0;
    Json toJson() const { return Json{{"error", error}, {"warning", warning}, {"note", note}}; }
};
DiagnosticSummary summarize(const std::vector<Diagnostic>& list);

} // namespace pme
