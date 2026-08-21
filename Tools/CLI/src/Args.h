// Tools/CLI/Args.h — 간단한 인자 파서. 설계 문서 §11, §12.
//
//   akeir <noun> [<verb>] [positional…] [--flag] [--key value] [--key=value] [--json|--output json|text|ndjson]
//   전역 플래그: --output, --json(= --output json), --project <dir>, --timeout <dur>, --yes, --dry-run, --tx <id>, --fields a,b, --limit N, --cursor C
#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace akeir::cli {

enum class OutputMode { Auto, Json, Text, NdJson };

struct Args {
    std::vector<std::string> positionals;          // noun, verb, … (플래그 제외)
    std::map<std::string, std::string> options;    // --key value / --key=value
    std::vector<std::string> flags;                // --flag (값 없음)

    bool has(const std::string& key) const;        // option 또는 flag
    std::optional<std::string> get(const std::string& key) const;
    std::string getOr(const std::string& key, const std::string& def) const { return get(key).value_or(def); }
    std::optional<long long> getInt(const std::string& key) const;
    std::string positional(std::size_t i, const std::string& def = "") const { return i < positionals.size() ? positionals[i] : def; }

    OutputMode outputMode() const;                 // --json / --output; Auto = TTY 감지 (§12 규칙 3)
};

Args parseArgs(int argc, char** argv);
Args parseArgs(const std::vector<std::string>& argv);   // argv[0] 없이 (RPC params.argv)
bool stdoutIsTty();
/// "30s", "2m", "1500ms", "90" (초) → ms
std::optional<long long> parseDurationMs(const std::string& text);

} // namespace akeir::cli
