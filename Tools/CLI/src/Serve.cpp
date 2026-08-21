// Tools/CLI/Serve.cpp — `game serve` 데몬(단일 writer) + 얇은 RPC 클라이언트 + ServeHost(디스패치; `game mcp` 와 공유). 설계 문서 §88.1, §9.1, §46.2, §13. 프로토콜은 Serve.h 머리말.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#endif

#include "Serve.h"
#include "GameSystems.h"
#include "pme/core/ExitCodes.h"
#include "pme/core/Id.h"
#include "pme/core/Log.h"
#include "pme/core/Time.h"
#include "pme/runtime/Components.h"
#include "pme/serialization/Canonical.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace pme::cli {

// ---------------------------------------------------------------- ServeInfo

Json ServeInfo::toJson() const {
    return Json{{"pid", pid}, {"port", port}, {"token", token}, {"startedAt", startedAt}, {"projectDir", projectDir}, {"transport", "tcp-ndjson-jsonrpc"}};
}

std::string ServeInfo::path(const std::string& projectDir) { return (fs::path(projectDir) / "Cache" / "serve.json").string(); }

std::optional<ServeInfo> ServeInfo::load(const std::string& projectDir) {
    if (projectDir.empty()) return std::nullopt;
    auto j = readJsonFile(path(projectDir));
    if (!j || !j->is_object()) return std::nullopt;
    ServeInfo s;
    s.pid = j->value("pid", 0);
    s.port = j->value("port", 0);
    s.token = j->value("token", "");
    s.startedAt = j->value("startedAt", "");
    s.projectDir = j->value("projectDir", "");
    if (s.port <= 0 || s.token.empty()) return std::nullopt;
    return s;
}

namespace {

#ifdef _WIN32
struct WsaGuard {
    bool ok = false;
    WsaGuard() { WSADATA d; ok = WSAStartup(MAKEWORD(2, 2), &d) == 0; }
    ~WsaGuard() { if (ok) WSACleanup(); }
};

bool sendAll(SOCKET s, const std::string& data) {
    std::size_t off = 0;
    while (off < data.size()) {
        int n = send(s, data.data() + off, static_cast<int>(std::min<std::size_t>(data.size() - off, 1 << 16)), 0);
        if (n <= 0) return false;
        off += static_cast<std::size_t>(n);
    }
    return true;
}

/// '\n' 까지 한 줄. 연결이 닫히면 false.
bool recvLine(SOCKET s, std::string& buffer, std::string& line) {
    for (;;) {
        auto nl = buffer.find('\n');
        if (nl != std::string::npos) { line = buffer.substr(0, nl); buffer.erase(0, nl + 1); return true; }
        char tmp[8192];
        int n = recv(s, tmp, sizeof(tmp), 0);
        if (n <= 0) return false;
        buffer.append(tmp, static_cast<std::size_t>(n));
        if (buffer.size() > (64u << 20)) return false;   // 64 MB 상한
    }
}
#endif

std::string randomToken() {
    // UUIDv7 두 개를 이어 붙인다 (예측 불가능성은 §88.6 수준이면 충분: 로컬 사용자 외에는 Cache/serve.json 을 읽을 수 없다)
    std::string a = Id::generate("tok").str(), b = Id::generate("tok").str();
    return a.substr(4) + b.substr(4);
}

Json rpcError(const Json& id, int code, const std::string& message, Json data = Json()) {
    Json e = Json{{"code", code}, {"message", message}};
    if (!data.is_null()) e["data"] = data;
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"error", e}};
}

Json rpcResult(const Json& id, const Envelope& env) {
    return Json{{"jsonrpc", "2.0"}, {"id", id}, {"result", Json{{"envelope", env.toJson()}, {"exitCode", env.exitCode()}}}};
}

ExecOptions execOptionsFrom(const Json& params) {
    ExecOptions o;
    o.tx = params.value("tx", "");
    o.dryRun = params.value("dryRun", false);
    o.validateAfter = !params.value("noValidate", false);
    o.idempotencyKey = params.value("idempotencyKey", "");
    return o;
}

} // namespace

// ---------------------------------------------------------------- ServeHost

bool ServeHost::init(const std::string& projectDir, const std::string& actor, Envelope& fail) {
    registerBuiltinComponents();
    game::registerGameComponents();
    projectDir_ = projectDir;
    if (projectDir.empty()) { fail = Envelope::failure("serve", CommandError::make(ErrorCategory::NotFound, "PROJECT_NOT_FOUND", "No project.json found. Use --project <dir>.")); return false; }
    std::vector<Diagnostic> diags;
    project_ = Project::load(projectDir, diags);
    if (!project_) { fail = Envelope::failure("serve", CommandError::make(ErrorCategory::NotFound, "PROJECT_NOT_FOUND", "Cannot load project from " + projectDir)); for (auto& d : diags) fail.withWarning(d); return false; }
    BusOptions bo; bo.actor = actor;
    bus_ = std::make_unique<CommandBus>(*project_, bo);
    recovered_ = bus_->recoverJournal().size();
    info_.pid = static_cast<int>(GetCurrentProcessId());
    info_.token = randomToken();
    info_.startedAt = WallTime::now().iso8601();
    info_.projectDir = projectDir;
    for (auto& d : diags) fail.withWarning(d);   // 호출자가 hello envelope 에 복사할 수 있게
    return true;
}

Json ServeHost::dispatch(const Json& req, bool checkToken) {
    Json id = req.is_object() ? req.value("id", Json()) : Json();
    if (!req.is_object() || req.value("jsonrpc", "") != "2.0" || !req.contains("method") || !req["method"].is_string())
        return rpcError(id, -32600, "Invalid Request (need jsonrpc:\"2.0\", method, token)");
    if (checkToken && req.value("token", "") != info_.token) return rpcError(id, -32001, "Unauthorized: token does not match Cache/serve.json");
    const std::string method = req["method"].get<std::string>();
    Json params = req.value("params", Json::object());
    ++requests_;
    bus_->expireTransactions();

    // ---- 서버 전용
    if (method == "serve.status") {
        Envelope env = Envelope::success("serve.status", Json{{"pid", info_.pid}, {"port", info_.port}, {"startedAt", info_.startedAt}, {"projectDir", projectDir_}, {"requests", requests_},
                                                            {"transactions", bus_->txList()}, {"history", bus_->historyJson(5)}, {"runs", runRegistry_.size()}, {"documents", project_->documents().size()}});
        return rpcResult(id, env);
    }
    if (method == "serve.stop") { stop_ = true; return rpcResult(id, Envelope::success("serve.stop", Json{{"stopping", true}})); }
    if (method == "project.reload") {
        std::vector<Diagnostic> diags;
        auto again = Project::load(projectDir_, diags);
        if (!again) return rpcResult(id, Envelope::failure("project.reload", CommandError::make(ErrorCategory::NotFound, "PROJECT_NOT_FOUND", "Reload failed.")));
        project_ = std::move(again);
        BusOptions bo; bo.actor = "serve";
        bus_ = std::make_unique<CommandBus>(*project_, bo);   // 열린 tx 는 모두 버려진다 (base 무효화, §9.2)
        Envelope env = Envelope::success("project.reload", Json{{"documents", project_->documents().size()}});
        for (auto& d : diags) env.withWarning(d);
        return rpcResult(id, env);
    }

    // ---- CLI command (params.argv) 또는 bus command (params.args)
    Args args;
    if (params.contains("argv") && params["argv"].is_array()) {
        std::vector<std::string> argv;
        for (const auto& a : params["argv"]) if (a.is_string()) argv.push_back(a.get<std::string>());
        args = parseArgs(argv);
    }
    if (params.contains("tx") && params["tx"].is_string()) args.options["tx"] = params["tx"].get<std::string>();
    if (params.contains("actor") && params["actor"].is_string()) args.options["actor"] = params["actor"].get<std::string>();
    if (params.contains("dryRun") && params["dryRun"].is_boolean() && params["dryRun"].get<bool>()) args.flags.push_back("dry-run");
    bus_->setActor(args.options.count("actor") ? args.options["actor"] : std::string("cli"));   // history 에는 호출자가 남는다, 데몬이 아니라

    if (params.contains("args") && params["args"].is_object()) {
        // 구조화 호출 (MCP 경로): bus command 직접
        if (method == "apply") return rpcResult(id, bus_->apply(params["args"], execOptionsFrom(params)));
        if (bus_->find(method)) return rpcResult(id, bus_->execute(method, params["args"], execOptionsFrom(params)));
        return rpcError(id, -32601, "Unknown bus command '" + method + "' (see capabilities.busCommands)");
    }
    // CLI 철자 경로: method 가 command id 면 그 spec, 아니면 argv 에서 찾는다
    const CommandSpec* spec = nullptr;
    for (const auto& s : commandTable()) if (s.id == method) { spec = &s; break; }
    if (!spec) { std::size_t consumed = 0; spec = findCommand(args.positionals, consumed); }
    if (!spec) return rpcError(id, -32601, "Unknown method '" + method + "'");
    if (spec->id == "serve" || spec->id == "mcp") return rpcError(id, -32602, spec->id + " cannot be nested");
    if (args.positionals.empty()) args.positionals = spec->cli;   // method 만 주고 argv 를 생략한 경우

    Context ctx{args, projectDir_};
    ctx.resident = &*project_;
    ctx.residentBus = bus_.get();
    ctx.runRegistry = &runRegistry_;
    Envelope env;
    try { env = spec->run(ctx); }
    catch (const std::exception& e) { env = Envelope::failure(spec->id, CommandError::make(ErrorCategory::Internal, "INTERNAL_ERROR", std::string("Unhandled C++ exception: ") + e.what())); }
    env.withMeta("serve", Json{{"pid", info_.pid}, {"request", requests_}});
    return rpcResult(id, env);
}

Json ServeHost::callArgv(const std::vector<std::string>& argv, const Json& extraParams) {
    Json params = extraParams.is_object() ? extraParams : Json::object();
    params["argv"] = argv;
    Json resp = dispatch(Json{{"jsonrpc", "2.0"}, {"id", 0}, {"method", argv.empty() ? "" : argv[0]}, {"params", params}}, false);
    if (resp.contains("result")) return resp["result"]["envelope"];
    return Envelope::failure(argv.empty() ? "?" : argv[0], CommandError::make(ErrorCategory::Internal, "RPC_ERROR", resp.value("error", Json::object()).value("message", "error"))).toJson();
}

Json ServeHost::callBus(const std::string& commandId, const Json& args, const Json& extraParams) {
    Json params = extraParams.is_object() ? extraParams : Json::object();
    params["args"] = args;
    Json resp = dispatch(Json{{"jsonrpc", "2.0"}, {"id", 0}, {"method", commandId}, {"params", params}}, false);
    if (resp.contains("result")) return resp["result"]["envelope"];
    return Envelope::failure(commandId, CommandError::make(ErrorCategory::Internal, "RPC_ERROR", resp.value("error", Json::object()).value("message", "error"))).toJson();
}

// ---------------------------------------------------------------- runServe

int runServe(Context& ctx) {
    auto printFail = [&](const Envelope& env) { std::fputs(env.toJson().dump().c_str(), stdout); std::fputc('\n', stdout); std::fflush(stdout); return env.exitCode(); };
    if (!ctx.projectDir.empty()) {
        if (auto existing = ServeInfo::load(ctx.projectDir)) {
            Json probe; int code = 0;
            Args a; a.positionals = {"serve", "status"};
            if (tryRemote(a, ctx.projectDir, "serve.status", probe, code))
                return printFail(Envelope::failure("serve", CommandError::make(ErrorCategory::Conflict, "SERVE_ALREADY_RUNNING", "Another `game serve` holds this project (pid " + std::to_string(existing->pid) + ", port " + std::to_string(existing->port) + ").", existing->toJson())));
            std::error_code ec; fs::remove(ServeInfo::path(ctx.projectDir), ec);   // stale
        }
    }
    ServeHost host;
    Envelope hello;
    if (!host.init(ctx.projectDir, ctx.args.getOr("actor", "serve"), hello)) return printFail(hello);
    const bool stdio = ctx.args.has("stdio");

#ifdef _WIN32
    WsaGuard wsa;
    SOCKET listener = INVALID_SOCKET;
    if (!stdio) {
        if (!wsa.ok) return printFail(Envelope::failure("serve", CommandError::make(ErrorCategory::Internal, "SOCKET_ERROR", "WSAStartup failed")));
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   // loopback 만 (§46.2). --bind 0.0.0.0 은 제공하지 않는다
        addr.sin_port = htons(static_cast<u_short>(ctx.args.getInt("port").value_or(0)));   // 0 = OS 가 고른다
        if (bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 || listen(listener, 8) != 0)
            return printFail(Envelope::failure("serve", CommandError::make(ErrorCategory::Internal, "SOCKET_ERROR", "bind/listen failed: " + std::to_string(WSAGetLastError()))));
        int len = sizeof(addr);
        getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &len);
        host.info().port = ntohs(addr.sin_port);
        std::string err;
        fs::create_directories(fs::path(ctx.projectDir) / "Cache");
        if (!writeCanonicalFile(ServeInfo::path(ctx.projectDir), host.info().toJson(), &err))
            return printFail(Envelope::failure("serve", CommandError::make(ErrorCategory::Internal, "SAVE_FAILED", "Cannot write Cache/serve.json: " + err)));
    }
#endif

    // 시작 envelope (stdout, 한 줄) — 클라이언트/사람이 port/token 을 여기서 읽을 수 있다. stdio 모드에서는 이 줄이 첫 NDJSON 응답 이전에 나간다.
    Json started = Json{{"pid", host.info().pid}, {"port", host.info().port}, {"token", host.info().token}, {"transport", stdio ? "stdio-ndjson-jsonrpc" : "tcp-ndjson-jsonrpc"},
                        {"projectDir", ctx.projectDir}, {"documents", host.documents()}, {"journalRecovered", host.journalRecovered()}, {"serveFile", ServeInfo::path(ctx.projectDir)}};
    Envelope helloOut = Envelope::success("serve", started);
    for (auto& d : hello.warnings) helloOut.withWarning(d);
    std::fputs(helloOut.toJson().dump().c_str(), stdout); std::fputc('\n', stdout); std::fflush(stdout);
    PME_LOG(Info, "serve", "listening", "game serve ready", Json{{"port", host.info().port}, {"pid", host.info().pid}});

    const long long idleMs = ctx.args.getInt("idle-timeout").value_or(0);
    auto lastActivity = std::chrono::steady_clock::now();

    if (stdio) {
        std::string line;
        while (!host.stopRequested() && std::getline(std::cin, line)) {
            if (line.empty()) continue;
            Json req = parseJson(line).value_or(Json());
            Json resp = host.dispatch(req, false);   // stdio 는 부모 프로세스가 소유하므로 token 생략
            std::fputs(resp.dump().c_str(), stdout); std::fputc('\n', stdout); std::fflush(stdout);
        }
    } else {
#ifdef _WIN32
        while (!host.stopRequested()) {
            if (idleMs > 0) {
                fd_set set; FD_ZERO(&set); FD_SET(listener, &set);
                timeval tv{1, 0};
                int r = select(0, &set, nullptr, nullptr, &tv);
                if (r == 0) {
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - lastActivity).count() > idleMs) { PME_LOG(Info, "serve", "idle_exit", "idle timeout"); break; }
                    continue;
                }
            }
            SOCKET c = accept(listener, nullptr, nullptr);
            if (c == INVALID_SOCKET) break;
            lastActivity = std::chrono::steady_clock::now();
            std::string buffer, line;
            while (!host.stopRequested() && recvLine(c, buffer, line)) {
                if (line.empty()) continue;
                auto req = parseJson(line);
                Json resp = req ? host.dispatch(*req, true) : rpcError(Json(), -32700, "Parse error");
                if (!sendAll(c, resp.dump() + "\n")) break;
                lastActivity = std::chrono::steady_clock::now();
            }
            closesocket(c);
        }
        closesocket(listener);
        std::error_code ec; fs::remove(ServeInfo::path(ctx.projectDir), ec);
#endif
    }
    PME_LOG(Info, "serve", "stopped", "game serve stopped", Json{{"requests", host.requests()}});
    return kExitOk;
}

// ---------------------------------------------------------------- client

bool tryRemote(const Args& args, const std::string& projectDir, const std::string& commandId, Json& envelopeOut, int& exitCodeOut) {
#ifndef _WIN32
    return false;
#else
    auto info = ServeInfo::load(projectDir);
    if (!info) return false;
    WsaGuard wsa;
    if (!wsa.ok) return false;
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<u_short>(info->port));
    if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        std::error_code ec; fs::remove(ServeInfo::path(projectDir), ec);   // 데몬이 죽었다 → stale 파일 제거, one-shot 으로
        return false;
    }
    Json argv = Json::array();
    for (const auto& p : args.positionals) argv.push_back(p);
    for (const auto& [k, v] : args.options) { argv.push_back("--" + k); argv.push_back(v); }
    for (const auto& f : args.flags) if (f != "local") argv.push_back("--" + f);
    Json req = Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", commandId}, {"params", Json{{"argv", argv}}}, {"token", info->token}};
    bool ok = false;
    if (sendAll(s, req.dump() + "\n")) {
        std::string buffer, line;
        if (recvLine(s, buffer, line)) {
            auto resp = parseJson(line);
            if (resp && resp->is_object()) {
                if (resp->contains("result") && (*resp)["result"].is_object()) {
                    envelopeOut = (*resp)["result"].value("envelope", Json::object());
                    exitCodeOut = (*resp)["result"].value("exitCode", 1);
                    ok = true;
                } else if (resp->contains("error")) {
                    Envelope env = Envelope::failure(commandId, CommandError::make(ErrorCategory::Internal, "RPC_ERROR", (*resp)["error"].value("message", "rpc error"), (*resp)["error"]));
                    envelopeOut = env.toJson();
                    exitCodeOut = env.exitCode();
                    ok = true;
                }
            }
        }
    }
    closesocket(s);
    return ok;
#endif
}

// ---------------------------------------------------------------- commands

namespace {

Envelope cmdServe(Context& ctx) {
    // main() 이 runServe 를 직접 부른다 (stdout 을 RPC 채널로 쓰기 때문). 여기 오면 중첩 호출이다.
    return Envelope::failure("serve", CommandError::make(ErrorCategory::Usage, "USAGE_ERROR", "serve must be the top-level command."));
}

Envelope cmdServeStatus(Context& ctx) {
    auto info = ServeInfo::load(ctx.projectDir);
    if (!info) return Envelope::failure("serve.status", CommandError::make(ErrorCategory::NotFound, "SERVE_NOT_RUNNING", "No `game serve` for this project (no Cache/serve.json).", Json{{"hint", "game serve"}}));
    Json env; int code = 0;
    if (!tryRemote(ctx.args, ctx.projectDir, "serve.status", env, code))
        return Envelope::failure("serve.status", CommandError::make(ErrorCategory::NotFound, "SERVE_NOT_RUNNING", "Cache/serve.json existed but the process did not answer; removed the stale file.", info->toJson()));
    return Envelope::success("serve.status", env.value("result", Json::object()));
}

Envelope cmdServeStop(Context& ctx) {
    Json env; int code = 0;
    if (!tryRemote(ctx.args, ctx.projectDir, "serve.stop", env, code))
        return Envelope::failure("serve.stop", CommandError::make(ErrorCategory::NotFound, "SERVE_NOT_RUNNING", "No running `game serve` for this project."));
    return Envelope::success("serve.stop", env.value("result", Json::object()));
}

} // namespace

void registerServeCommands(std::vector<CommandSpec>& t) {
    t.push_back({"serve", {"serve"}, "Meta", "Run the resident Command host (§88.1)",
                 "Loads the project once, holds the single CommandBus, listens on 127.0.0.1:<port> (NDJSON JSON-RPC, per-session token in Cache/serve.json). While it runs, every `game <cmd>` in this project is forwarded to it, so `tx begin/commit` and `run status` work across calls. --stdio uses stdin/stdout instead (Editor embedding).",
                 "game serve [--port P] [--stdio] [--idle-timeout ms] [--actor A]", false, false, false, cmdServe});
    t.push_back({"serve.status", {"serve", "status"}, "Query", "Is a daemon running?", "", "game serve status [--json]", true, false, true, cmdServeStatus});
    t.push_back({"serve.stop", {"serve", "stop"}, "RuntimeControl", "Stop the daemon", "", "game serve stop", false, false, true, cmdServeStop});
}

} // namespace pme::cli
