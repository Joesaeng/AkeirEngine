// Tools/CLI/Mcp.cpp — `game mcp`: MCP 서버 (stdio, newline-delimited JSON-RPC). 설계 문서 §46 (MCP Adapter), §46.2 (Command API ↔ MCP 매핑), §47 (tool 15개, 2층 구조), §15 (capabilities pass-through).
//
//   ▶ v3 (ADR-0030): 공식 SDK sidecar(§46.1) 대신 C++ 네이티브 — 같은 프로세스의 ServeHost(단일 writer) 위에 MCP 메서드만 올린다. 외부 런타임(Node/Python) 불필요.
//   지원 메서드:
//     server/discover            (2026-07-28 stateless 핸드셰이크) → {supportedVersions, capabilities:{tools:{}}, instructions}
//     initialize                 (2025-xx 클라이언트 호환) → {protocolVersion, capabilities:{tools:{}}, serverInfo, instructions}
//     notifications/initialized  (무시), ping → {}
//     tools/list                 → capabilities.tools[] 중 enabled 만 (name, title, description, inputSchema, outputSchema, annotations)
//     tools/call                 → {content:[{type:"text", text: envelope JSON}], structuredContent: envelope, isError: !ok}   (§46.2: 도메인 오류도 같은 envelope + isError)
//   tool → 실행 매핑 (§46.2 표): query/inspect/explain/validate/run/run_status/test/capture/project_info/schema_describe/capabilities/history/tx 는 CLI argv 로,
//                               apply 는 bus.apply 로 (changes[].op = busCommands[].id).
//   로그는 stderr(JSONL) 로만 — stdout 은 MCP 채널 (§28: MCP Logging 은 쓰지 않는다).
#include "Serve.h"
#include "pme/core/ExitCodes.h"
#include "pme/core/Log.h"
#include "pme/serialization/Canonical.h"

#include <iostream>
#include <sstream>

namespace pme::cli {

namespace {

const char* kSupportedVersions[] = {"2026-07-28", "2025-11-25", "2025-06-18", "2025-03-26", "2024-11-05", nullptr};

Json rpcErr(const Json& id, int code, const std::string& msg) { return Json{{"jsonrpc", "2.0"}, {"id", id}, {"error", Json{{"code", code}, {"message", msg}}}}; }
Json rpcOk(const Json& id, Json result) { return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}}; }

std::string instructionsText() {
    return "MoltEngine: AI-native game framework. Authoring data is JSON under the project (worlds, prefabs); every change goes through the Command API and returns a ChangeSet. "
           "Start with `capabilities` (tools, busCommands = ops allowed in apply.changes[], error codes) and `project_info`. "
           "Use `query`/`inspect`/`explain` to read, `apply` to change (atomic batch; '$name' references earlier results; dryRun to preview), `validate` (fix:true to auto-fix) before `run`/`test`. "
           "`history` undoes. `tx` groups several apply calls into one undo entry. Errors come back as the same envelope with isError=true and error.ruleId.";
}

Json toolsList() {
    Json caps = capabilitiesJson();
    Json out = Json::array();
    for (const auto& t : caps["tools"]) {
        if (!t.value("enabled", false)) continue;
        Json tool = Json{{"name", t["name"]}, {"title", t["title"]}, {"description", t["description"]}, {"inputSchema", t["inputSchema"]}, {"outputSchema", t["outputSchema"]}, {"annotations", t["annotations"]}};
        out.push_back(tool);
    }
    return out;
}

std::string str(const Json& a, const char* k, const std::string& def = "") { return a.contains(k) && a[k].is_string() ? a[k].get<std::string>() : def; }
bool flag(const Json& a, const char* k) { return a.contains(k) && a[k].is_boolean() && a[k].get<bool>(); }
std::string num(const Json& a, const char* k) { return a.contains(k) && a[k].is_number() ? std::to_string(a[k].get<long long>()) : ""; }
std::string joinList(const Json& v) { std::string s; if (v.is_array()) for (const auto& x : v) if (x.is_string()) s += (s.empty() ? "" : ",") + x.get<std::string>(); else if (v.is_string()) s = v.get<std::string>(); return s; }

/// tool 이름 + arguments → envelope JSON
Json callTool(ServeHost& host, const std::string& name, const Json& a) {
    std::vector<std::string> argv;
    auto opt = [&](const char* flagName, const std::string& v) { if (!v.empty()) { argv.push_back(std::string("--") + flagName); argv.push_back(v); } };
    auto boolFlag = [&](const char* flagName, const char* key) { if (flag(a, key)) argv.push_back(std::string("--") + flagName); };

    if (name == "capabilities") { argv = {"capabilities"}; }
    else if (name == "project_info") { argv = {"project", "info"}; }
    else if (name == "schema_describe") { argv = {"schema"}; if (flag(a, "all")) argv.push_back("--all"); else if (!str(a, "component").empty()) { argv.push_back("component"); argv.push_back(str(a, "component")); } else argv.push_back("--all"); }
    else if (name == "query") { argv = {"query"}; opt("with", joinList(a.value("with", Json()))); opt("without", joinList(a.value("without", Json()))); opt("ticks", num(a, "ticks")); opt("limit", num(a, "limit")); opt("world", str(a, "world")); boolFlag("components", "components"); }
    else if (name == "inspect") { argv = {"dump", str(a, "entity", "world")}; if (str(a, "entity").empty()) argv.push_back("--all"); opt("ticks", num(a, "ticks")); opt("world", str(a, "world")); }
    else if (name == "explain") { argv = {"explain", str(a, "selector")}; }
    else if (name == "refs") { argv = {"refs", str(a, "selector", str(a, "id"))}; }
    else if (name == "validate") { argv = {"validate"}; boolFlag("fix", "fix"); boolFlag("dry-run", "dryRun"); }
    else if (name == "run") { argv = {"run", "--headless"}; opt("ticks", num(a, "ticks")); opt("seed", num(a, "seed")); opt("world", str(a, "world")); opt("hash-every", num(a, "hashEvery")); opt("snapshot-out", str(a, "snapshotOut")); }
    else if (name == "run_status") { argv = {"run", "status"}; if (!str(a, "run").empty()) argv.push_back(str(a, "run")); }
    else if (name == "test") { argv = {"test"}; if (!str(a, "filter").empty()) argv.push_back(str(a, "filter")); opt("junit", str(a, "junit")); opt("results-dir", str(a, "resultsDir")); boolFlag("update-golden", "updateGolden"); }
    else if (name == "capture") { argv = {"capture"}; opt("ticks", num(a, "ticks")); opt("width", num(a, "width")); opt("height", num(a, "height")); opt("out", str(a, "out")); opt("compare", str(a, "compare")); }
    else if (name == "history") {
        std::string action = str(a, "action", "list");
        if (action == "undo") { argv = {"undo"}; if (!num(a, "steps").empty()) argv.push_back(num(a, "steps")); opt("actor", str(a, "actor")); }
        else if (action == "redo") { argv = {"redo"}; if (!num(a, "steps").empty()) argv.push_back(num(a, "steps")); }
        else { argv = {"history"}; opt("limit", num(a, "limit")); }
    }
    else if (name == "tx") {
        std::string action = str(a, "action", "list");
        if (action == "begin") { argv = {"tx", "begin"}; opt("ttl", num(a, "ttl")); }
        else if (action == "commit") { argv = {"tx", "commit", str(a, "tx")}; }
        else if (action == "rollback") { argv = {"tx", "rollback", str(a, "tx")}; }
        else argv = {"tx", "list"};
    }
    else if (name == "apply") {
        Json extra = Json::object();
        if (!str(a, "tx").empty()) extra["tx"] = str(a, "tx");
        if (!str(a, "actor").empty()) extra["actor"] = str(a, "actor");
        return host.callBus("apply", a, extra);
    }
    else return Envelope::failure(name, CommandError::make(ErrorCategory::Usage, "UNKNOWN_TOOL", "No such tool '" + name + "'. See tools/list.")).toJson();

    Json extra = Json::object();
    if (!str(a, "actor").empty()) extra["actor"] = str(a, "actor");
    if (!str(a, "tx").empty()) extra["tx"] = str(a, "tx");
    argv.push_back("--json");
    return host.callArgv(argv, extra);
}

} // namespace

int runMcp(Context& ctx) {
    ServeHost host;
    Envelope fail;
    if (!host.init(ctx.projectDir, ctx.args.getOr("actor", "mcp"), fail)) {
        // stdout 은 MCP 채널이므로 오류는 stderr 로만
        std::fputs(fail.toJson().dump().c_str(), stderr); std::fputc('\n', stderr);
        return fail.exitCode();
    }
    PME_LOG(Info, "mcp", "ready", "game mcp ready (stdio)", Json{{"projectDir", ctx.projectDir}});
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        auto reqOpt = parseJson(line);
        if (!reqOpt || !reqOpt->is_object()) { std::fputs(rpcErr(Json(), -32700, "Parse error").dump().c_str(), stdout); std::fputc('\n', stdout); std::fflush(stdout); continue; }
        const Json& req = *reqOpt;
        const std::string method = req.value("method", "");
        const bool isNotification = !req.contains("id");
        Json id = req.value("id", Json());
        Json params = req.value("params", Json::object());
        Json resp;
        if (method == "server/discover") {
            Json versions = Json::array(); for (const char** v = kSupportedVersions; *v; ++v) versions.push_back(*v);
            resp = rpcOk(id, Json{{"supportedVersions", versions}, {"capabilities", Json{{"tools", Json{{"listChanged", false}}}}}, {"instructions", instructionsText()}});
        } else if (method == "initialize") {
            std::string requested = params.value("protocolVersion", "2025-06-18");
            bool known = false; for (const char** v = kSupportedVersions; *v; ++v) if (requested == *v) known = true;
            resp = rpcOk(id, Json{{"protocolVersion", known ? requested : "2025-06-18"}, {"capabilities", Json{{"tools", Json{{"listChanged", false}}}}},
                                  {"serverInfo", Json{{"name", "moltengine"}, {"title", "MoltEngine"}, {"version", PME_VERSION_STRING}}}, {"instructions", instructionsText()}});
        } else if (method == "notifications/initialized" || method.rfind("notifications/", 0) == 0) {
            continue;   // 알림에는 응답하지 않는다
        } else if (method == "ping") {
            resp = rpcOk(id, Json::object());
        } else if (method == "tools/list") {
            resp = rpcOk(id, Json{{"tools", toolsList()}});
        } else if (method == "tools/call") {
            std::string name = params.value("name", "");
            Json args = params.value("arguments", Json::object());
            if (!args.is_object()) args = Json::object();
            Json env = callTool(host, name, args);
            bool ok = env.value("ok", false);
            resp = rpcOk(id, Json{{"content", Json::array({Json{{"type", "text"}, {"text", env.dump()}}})}, {"structuredContent", env}, {"isError", !ok}});
        } else if (method == "resources/list" || method == "prompts/list") {
            resp = rpcOk(id, Json{{method == "resources/list" ? "resources" : "prompts", Json::array()}});
        } else {
            if (isNotification) continue;
            resp = rpcErr(id, -32601, "Method not found: " + method);
        }
        if (isNotification) continue;
        std::fputs(resp.dump().c_str(), stdout); std::fputc('\n', stdout); std::fflush(stdout);
        if (host.stopRequested()) break;
    }
    PME_LOG(Info, "mcp", "stopped", "game mcp stopped", Json{{"requests", host.requests()}});
    return kExitOk;
}

void registerMcpCommands(std::vector<CommandSpec>& t) {
    t.push_back({"mcp", {"mcp"}, "Meta", "MCP server over stdio (§46)",
                 "Speaks MCP (newline-delimited JSON-RPC on stdin/stdout): server/discover, initialize, tools/list, tools/call. Tools = capabilities.tools[] (enabled). Single writer in-process (same as `game serve`). Register in an MCP client as: command=game, args=[\"mcp\",\"--project\",\"<dir>\"].",
                 "game mcp [--project DIR] [--actor A]", false, false, false, [](Context&) { return Envelope::failure("mcp", CommandError::make(ErrorCategory::Usage, "USAGE_ERROR", "mcp must be the top-level command.")); }});
}

} // namespace pme::cli
