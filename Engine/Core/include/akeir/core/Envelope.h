// akeir/core/Envelope.h — 규범적 출력 envelope. 설계 문서 §12 (ok/command/result|error/changes/warnings/meta), §13 (error 형태).
//
//   { "ok": true,  "command": "entity.create", "result": {...}, "changes": [...], "warnings": [...], "meta": {...} }
//   { "ok": false, "command": "component.add", "error": { ...§79 Diagnostic + category/retryable/details... }, "warnings": [], "changes": [], "meta": {...} }
//
//   규칙 (§12): ok 가 discriminator. result 는 ok 일 때만, error 는 !ok 일 때만. warnings/changes/meta 는 항상 존재.
//   같은 구조가 CLI `--json` 출력이자 MCP structuredContent 다. schema: game://schema/envelope/1.
#pragma once

#include "akeir/core/Diagnostic.h"
#include "akeir/core/ExitCodes.h"
#include "akeir/core/Json.h"

#include <string>
#include <vector>

namespace akeir {

/// error.category (§13)
enum class ErrorCategory { Usage, Validation, NotFound, Conflict, Precondition, Crash, Timeout, Internal, Cancelled };
const char* errorCategoryName(ErrorCategory c);
int defaultExitCode(ErrorCategory c);

struct CommandError {
    Diagnostic diagnostic;                 // ruleId / level / message / logical / physical / fixes / fingerprint / helpUri
    ErrorCategory category = ErrorCategory::Internal;
    bool retryable = false;
    Json details = Json::object();

    Json toJson() const;
    static CommandError make(ErrorCategory cat, std::string ruleId, std::string text, Json details = Json::object());
};

struct Envelope {
    bool ok = true;
    std::string command;
    Json result = Json::object();          // ok 일 때
    CommandError error;                    // !ok 일 때
    Json changes = Json::array();          // §78 ChangeSet ops
    std::vector<Diagnostic> warnings;
    Json meta = Json::object();            // schemaVersion, engineVersion, tx, dryRun, durationMs, truncated, nextCursor

    static Envelope success(std::string command, Json result = Json::object());
    static Envelope failure(std::string command, CommandError err);

    Envelope& withMeta(const std::string& key, Json value) { meta[key] = std::move(value); return *this; }
    Envelope& withWarning(Diagnostic d) { warnings.push_back(std::move(d)); return *this; }
    Envelope& withChanges(Json ops) { changes = std::move(ops); return *this; }

    /// 최종 JSON. meta 기본값(schemaVersion, engineVersion, dryRun, truncated, nextCursor)을 채운다.
    Json toJson() const;
    /// §13 exit code 표에 따른 프로세스 종료 코드
    int exitCode() const;
};

} // namespace akeir
