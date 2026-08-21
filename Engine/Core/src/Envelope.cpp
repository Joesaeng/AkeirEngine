// pme/core/Envelope.cpp — 설계 문서 §12, §13
#include "pme/core/Envelope.h"

namespace pme {

const char* errorCategoryName(ErrorCategory c) {
    switch (c) {
    case ErrorCategory::Usage: return "usage";
    case ErrorCategory::Validation: return "validation";
    case ErrorCategory::NotFound: return "not_found";
    case ErrorCategory::Conflict: return "conflict";
    case ErrorCategory::Precondition: return "precondition";
    case ErrorCategory::Crash: return "crash";
    case ErrorCategory::Timeout: return "timeout";
    case ErrorCategory::Cancelled: return "cancelled";
    default: return "internal";
    }
}

int defaultExitCode(ErrorCategory c) {
    switch (c) {
    case ErrorCategory::Usage: return kExitUsage;
    case ErrorCategory::Validation: return kExitFindings;
    case ErrorCategory::NotFound: return kExitNotFound;
    case ErrorCategory::Crash: return kExitCrash;
    case ErrorCategory::Timeout: return kExitTimeout;
    case ErrorCategory::Cancelled: return kExitInterrupted;
    default: return kExitCommandFailed;
    }
}

CommandError CommandError::make(ErrorCategory cat, std::string ruleId, std::string text, Json details) {
    CommandError e;
    e.diagnostic = Diagnostic::error(std::move(ruleId), std::move(text));
    e.category = cat;
    e.details = std::move(details);
    return e;
}

Json CommandError::toJson() const {
    // §13: error = §79 Diagnostic + category / retryable / details. 필드 순서: ruleId, level, category, message, …
    Json d = diagnostic.toJson();
    Json j = Json::object();
    j["ruleId"] = d["ruleId"];
    j["level"] = d["level"];
    j["category"] = errorCategoryName(category);
    j["message"] = d["message"];
    if (d.contains("logical")) j["logical"] = d["logical"];
    if (d.contains("physical")) j["physical"] = d["physical"];
    if (d.contains("related")) j["related"] = d["related"];
    j["details"] = details.is_null() ? Json::object() : details;
    j["retryable"] = retryable;
    j["fixes"] = d["fixes"];
    j["fingerprint"] = d["fingerprint"];
    j["helpUri"] = d["helpUri"];
    return j;
}

Envelope Envelope::success(std::string command, Json result) {
    Envelope e;
    e.ok = true;
    e.command = std::move(command);
    e.result = std::move(result);
    return e;
}

Envelope Envelope::failure(std::string command, CommandError err) {
    Envelope e;
    e.ok = false;
    e.command = std::move(command);
    e.error = std::move(err);
    return e;
}

Json Envelope::toJson() const {
    Json j = Json::object();
    j["ok"] = ok;
    j["command"] = command;
    if (ok) j["result"] = result;
    else j["error"] = error.toJson();
    j["changes"] = changes.is_array() ? changes : Json::array();
    Json w = Json::array();
    for (const auto& d : warnings) w.push_back(d.toJson());
    j["warnings"] = w;
    Json m = meta;
    if (!m.contains("schemaVersion")) m["schemaVersion"] = 1;
    if (!m.contains("engine")) m["engine"] = "AKEIR";
    if (!m.contains("engineVersion")) m["engineVersion"] = PME_VERSION_STRING;
    if (!m.contains("dryRun")) m["dryRun"] = false;
    if (!m.contains("truncated")) m["truncated"] = false;
    if (!m.contains("nextCursor")) m["nextCursor"] = nullptr;
    j["meta"] = m;
    return j;
}

int Envelope::exitCode() const {
    if (ok) return kExitOk;
    if (error.diagnostic.ruleId == "CONFIRMATION_REQUIRED") return kExitConfirmationRequired;
    return defaultExitCode(error.category);
}

} // namespace pme
