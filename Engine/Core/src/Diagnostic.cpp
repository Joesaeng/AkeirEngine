// akeir/core/Diagnostic.cpp — 설계 문서 §79
#include "akeir/core/Diagnostic.h"
#include "akeir/core/Hash.h"

namespace akeir {

const char* severityName(Severity s) {
    switch (s) { case Severity::Note: return "note"; case Severity::Warning: return "warning"; default: return "error"; }
}
const char* applicabilityName(Applicability a) {
    switch (a) {
    case Applicability::MachineApplicable: return "MachineApplicable";
    case Applicability::MaybeIncorrect: return "MaybeIncorrect";
    case Applicability::HasPlaceholders: return "HasPlaceholders";
    default: return "Unspecified";
    }
}
std::optional<Severity> severityFromString(std::string_view s) {
    if (s == "note") return Severity::Note;
    if (s == "warning") return Severity::Warning;
    if (s == "error") return Severity::Error;
    return std::nullopt;
}
std::optional<Applicability> applicabilityFromString(std::string_view s) {
    if (s == "MachineApplicable") return Applicability::MachineApplicable;
    if (s == "MaybeIncorrect") return Applicability::MaybeIncorrect;
    if (s == "HasPlaceholders") return Applicability::HasPlaceholders;
    if (s == "Unspecified") return Applicability::Unspecified;
    return std::nullopt;
}

namespace {
Json locToJson(const LogicalLocation& l) {
    Json j = Json::object();
    j["object"] = l.object;
    if (l.component) j["component"] = *l.component;
    if (l.propertyPath) j["propertyPath"] = *l.propertyPath;
    return j;
}
Json physToJson(const PhysicalLocation& p) {
    Json j = Json::object();
    j["uri"] = p.uri;
    j["jsonPointer"] = p.jsonPointer;
    if (p.region) j["region"] = Json{{"startLine", p.region->startLine}, {"startColumn", p.region->startColumn},
                                     {"endLine", p.region->endLine}, {"endColumn", p.region->endColumn}};
    return j;
}
std::optional<LogicalLocation> locFromJson(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    LogicalLocation l;
    l.object = j.value("object", "");
    if (j.contains("component") && j["component"].is_string()) l.component = j["component"].get<std::string>();
    if (j.contains("propertyPath") && j["propertyPath"].is_string()) l.propertyPath = j["propertyPath"].get<std::string>();
    return l;
}
std::optional<PhysicalLocation> physFromJson(const Json& j) {
    if (!j.is_object()) return std::nullopt;
    PhysicalLocation p;
    p.uri = j.value("uri", "");
    p.jsonPointer = j.value("jsonPointer", "");
    if (j.contains("region") && j["region"].is_object()) {
        const auto& r = j["region"];
        p.region = Region{r.value("startLine", 0), r.value("startColumn", 0), r.value("endLine", 0), r.value("endColumn", 0)};
    }
    return p;
}
} // namespace

std::string Diagnostic::computeFingerprint() const {
    Hasher h;
    h.str(ruleId);
    if (logical) { h.str(logical->object); h.str(logical->component.value_or("")); h.str(logical->propertyPath.value_or("")); }
    if (physical) { h.str(physical->uri); h.str(physical->jsonPointer); }
    return toHex64(h.value());
}

Json Diagnostic::toJson() const {
    Json j = Json::object();
    j["ruleId"] = ruleId;
    j["level"] = severityName(level);
    j["message"] = Json{{"text", message.text}};
    if (logical) j["logical"] = locToJson(*logical);
    if (physical) j["physical"] = physToJson(*physical);
    if (!related.empty()) {
        Json arr = Json::array();
        for (const auto& r : related) {
            Json rj = Json::object();
            rj["message"] = Json{{"text", r.message.text}};
            if (r.physical) rj["physical"] = physToJson(*r.physical);
            if (r.logical) rj["logical"] = locToJson(*r.logical);
            arr.push_back(rj);
        }
        j["related"] = arr;
    }
    Json fixArr = Json::array();
    for (const auto& f : fixes) {
        Json fj = Json::object();
        fj["description"] = f.description;
        fj["applicability"] = applicabilityName(f.applicability);
        fj["isPreferred"] = f.isPreferred;
        Json cmds = Json::array();
        for (const auto& c : f.commands) cmds.push_back(Json{{"op", c.op}, {"args", c.args}});
        fj["commands"] = cmds;
        if (!f.artifactChanges.empty()) fj["artifactChanges"] = f.artifactChanges;
        if (f.cli) fj["cli"] = *f.cli;
        fixArr.push_back(fj);
    }
    j["fixes"] = fixArr;
    j["fingerprint"] = fingerprint.empty() ? computeFingerprint() : fingerprint;
    j["helpUri"] = helpUri.value_or("game://docs/rules/" + ruleId);
    return j;
}

std::optional<Diagnostic> Diagnostic::fromJson(const Json& j) {
    if (!j.is_object() || !j.contains("ruleId")) return std::nullopt;
    Diagnostic d;
    d.ruleId = j.value("ruleId", "");
    d.level = severityFromString(j.value("level", "error")).value_or(Severity::Error);
    if (j.contains("message")) {
        if (j["message"].is_object()) d.message.text = j["message"].value("text", "");
        else if (j["message"].is_string()) d.message.text = j["message"].get<std::string>();
    }
    if (j.contains("logical")) d.logical = locFromJson(j["logical"]);
    if (j.contains("physical")) d.physical = physFromJson(j["physical"]);
    if (j.contains("fixes") && j["fixes"].is_array()) {
        for (const auto& fj : j["fixes"]) {
            Fix f;
            f.description = fj.value("description", "");
            f.applicability = applicabilityFromString(fj.value("applicability", "Unspecified")).value_or(Applicability::Unspecified);
            f.isPreferred = fj.value("isPreferred", false);
            if (fj.contains("commands") && fj["commands"].is_array())
                for (const auto& c : fj["commands"]) f.commands.push_back({c.value("op", ""), c.value("args", Json::object())});
            if (fj.contains("artifactChanges") && fj["artifactChanges"].is_array())
                for (const auto& a : fj["artifactChanges"]) f.artifactChanges.push_back(a);
            if (fj.contains("cli") && fj["cli"].is_string()) f.cli = fj["cli"].get<std::string>();
            d.fixes.push_back(std::move(f));
        }
    }
    d.fingerprint = j.value("fingerprint", "");
    if (j.contains("helpUri") && j["helpUri"].is_string()) d.helpUri = j["helpUri"].get<std::string>();
    return d;
}

Diagnostic Diagnostic::error(std::string ruleId, std::string text) { Diagnostic d; d.level = Severity::Error; d.ruleId = std::move(ruleId); d.message.text = std::move(text); return d; }
Diagnostic Diagnostic::warning(std::string ruleId, std::string text) { Diagnostic d; d.level = Severity::Warning; d.ruleId = std::move(ruleId); d.message.text = std::move(text); return d; }
Diagnostic Diagnostic::note(std::string ruleId, std::string text) { Diagnostic d; d.level = Severity::Note; d.ruleId = std::move(ruleId); d.message.text = std::move(text); return d; }

DiagnosticSummary summarize(const std::vector<Diagnostic>& list) {
    DiagnosticSummary s;
    for (const auto& d : list) {
        switch (d.level) { case Severity::Error: ++s.error; break; case Severity::Warning: ++s.warning; break; default: ++s.note; }
    }
    return s;
}

} // namespace akeir
