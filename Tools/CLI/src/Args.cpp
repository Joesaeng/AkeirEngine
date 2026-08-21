// Tools/CLI/Args.cpp — 설계 문서 §11, §12
#include "Args.h"

#include <cctype>
#include <cstdlib>

#ifdef _WIN32
#  include <io.h>
#  define PME_ISATTY _isatty
#  define PME_FILENO _fileno
#else
#  include <unistd.h>
#  define PME_ISATTY isatty
#  define PME_FILENO fileno
#endif
#include <cstdio>

namespace pme::cli {

namespace {
// 값이 따라오는 옵션 목록 — 이 목록에 없는 "--x" 는 flag 로 본다.
const char* kValueOptions[] = {"output", "project", "timeout", "tx", "fields", "limit", "cursor", "world", "seed",
                               "ticks", "frames", "tick-rate", "threads", "hash-every", "hash-out", "replay-out",
                               "name", "importer", "filter", "expr", "jq", "if-match", "format", "run", "with", "without", "snapshot-out", "replay",
                               "args", "actor", "idempotency-key", "junit", "results-dir", "width", "height", "out", "compare", "diff", "per-pixel", "max-mismatch", "record", "video-driver", "ttl", "port", "idle-timeout", "dir", "parent", "prefab", "components", "set", "value", "position", "tags", "base", "order", nullptr};
bool takesValue(const std::string& key) {
    for (const char** p = kValueOptions; *p; ++p) if (key == *p) return true;
    return false;
}
} // namespace

bool Args::has(const std::string& key) const {
    if (options.count(key)) return true;
    for (const auto& f : flags) if (f == key) return true;
    return false;
}

std::optional<std::string> Args::get(const std::string& key) const {
    auto it = options.find(key);
    if (it == options.end()) return std::nullopt;
    return it->second;
}

std::optional<long long> Args::getInt(const std::string& key) const {
    auto v = get(key);
    if (!v) return std::nullopt;
    char* end = nullptr;
    long long n = std::strtoll(v->c_str(), &end, 10);
    if (!end || *end != '\0') return std::nullopt;
    return n;
}

OutputMode Args::outputMode() const {
    if (has("json")) return OutputMode::Json;
    auto o = get("output");
    if (!o) return OutputMode::Auto;
    if (*o == "json") return OutputMode::Json;
    if (*o == "text") return OutputMode::Text;
    if (*o == "ndjson") return OutputMode::NdJson;
    return OutputMode::Auto;
}

Args parseArgs(const std::vector<std::string>& argv) {
    Args a;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        const std::string& s = argv[i];
        if (s.rfind("--", 0) == 0 && s.size() > 2) {
            std::string key = s.substr(2);
            auto eq = key.find('=');
            if (eq != std::string::npos) { a.options[key.substr(0, eq)] = key.substr(eq + 1); continue; }
            if (takesValue(key) && i + 1 < argv.size()) { a.options[key] = argv[++i]; continue; }
            a.flags.push_back(key);
        } else {
            a.positionals.push_back(s);
        }
    }
    return a;
}

Args parseArgs(int argc, char** argv) {
    std::vector<std::string> v;
    for (int i = 1; i < argc; ++i) v.emplace_back(argv[i]);
    return parseArgs(v);
}

bool stdoutIsTty() { return PME_ISATTY(PME_FILENO(stdout)) != 0; }

std::optional<long long> parseDurationMs(const std::string& text) {
    if (text.empty()) return std::nullopt;
    std::size_t i = 0;
    while (i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) || text[i] == '.')) ++i;
    if (i == 0) return std::nullopt;
    double value = std::atof(text.substr(0, i).c_str());
    std::string unit = text.substr(i);
    if (unit.empty() || unit == "s") return static_cast<long long>(value * 1000.0);
    if (unit == "ms") return static_cast<long long>(value);
    if (unit == "m") return static_cast<long long>(value * 60000.0);
    if (unit == "h") return static_cast<long long>(value * 3600000.0);
    return std::nullopt;
}

} // namespace pme::cli
