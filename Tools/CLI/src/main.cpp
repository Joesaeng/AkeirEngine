// Tools/CLI/main.cpp — `akeir` 진입점. 설계 문서 §11 (CLI 1급), §12 (envelope, TTY 감지), §13 (exit code), §88.4 (crash/watchdog).
//
// 흐름: parseArgs → installCrashHandler → (--timeout) startWatchdog → findCommand → run → envelope 출력 → exit code
// stdout 에는 envelope 만 나간다. 로그는 stderr (JSONL, §28).
#include "Args.h"
#include "Commands.h"
#include "Serve.h"
#include "akeir/core/Crash.h"
#include "akeir/core/Envelope.h"
#include "akeir/core/ExitCodes.h"
#include "akeir/core/Log.h"
#include "akeir/core/Time.h"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

using namespace akeir;
using namespace akeir::cli;

namespace {

void printEnvelope(const Envelope& env, OutputMode mode) {
    Json j = env.toJson();
    bool json = (mode == OutputMode::Json || mode == OutputMode::NdJson) || (mode == OutputMode::Auto && !stdoutIsTty());
    if (json) {
        std::fputs(j.dump().c_str(), stdout);
        std::fputc('\n', stdout);
    } else {
        // 사람용: 당분간 pretty JSON. (Phase 4 에서 명령별 텍스트 포맷을 붙인다.)
        std::fputs(j.dump(2).c_str(), stdout);
        std::fputc('\n', stdout);
    }
    std::fflush(stdout);
}

std::string findProjectDir(const Args& args) {
    if (auto p = args.get("project")) return std::filesystem::absolute(*p).string();
    // cwd 에서 위로 올라가며 project.json 을 찾는다
    auto dir = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        if (std::filesystem::exists(dir / "project.json")) return dir.string();
        if (!dir.has_parent_path() || dir.parent_path() == dir) break;
        dir = dir.parent_path();
    }
    return "";
}

} // namespace

int main(int argc, char** argv) {
    Args args = parseArgs(argc, argv);
    OutputMode mode = args.outputMode();

    // 로그: stderr JSONL + 마지막 64개 ring (크래시 envelope 에 실린다)
    auto ring = std::make_shared<RingSink>(64);
    Logger::global().addSink(ring);
    if (args.has("verbose")) Logger::global().setMinLevel(LogLevel::Debug);
    if (args.has("quiet")) Logger::global().setMinLevel(LogLevel::Error);

    std::size_t consumed = 0;
    const CommandSpec* spec = findCommand(args.positionals, consumed);
    std::string commandId = spec ? spec->id : "unknown";

    // --help / -h / help: 창을 열거나 프로젝트를 건드리기 전에 usage 만 출력 (exit 0)
    const bool wantsHelp = args.has("help") || args.has("h") || (!args.positionals.empty() && args.positionals[0] == "help");
    if (wantsHelp || args.positionals.empty()) {
        Json r = Json::object();
        if (spec && wantsHelp && !(args.positionals.size() == 1 && args.positionals[0] == "help")) {
            r["command"] = spec->id; r["title"] = spec->title; r["description"] = spec->description; r["usage"] = spec->usage; r["kind"] = spec->kind;
        } else {
            Json cmds = Json::array();
            for (const auto& s : commandTable()) { std::string line; for (const auto& p : s.cli) line += (line.empty() ? "" : " ") + p; cmds.push_back(Json{{"cli", line}, {"id", s.id}, {"title", s.title}, {"usage", s.usage}}); }
            r["commands"] = cmds;
            r["hint"] = "akeir <command> --help | akeir capabilities --json (full schemas) | Docs/00-START-HERE.md";
        }
        Envelope env = Envelope::success("help", r);
        printEnvelope(env, mode);
        return kExitOk;
    }

    CrashConfig cc;
    cc.command = commandId;
    cc.lastLogs = ring;
    std::string projectDir = findProjectDir(args);
    cc.dumpDir = projectDir.empty() ? "Cache/crash" : (std::filesystem::path(projectDir) / "Cache" / "crash").string();
    installCrashHandler(cc);

    if (!spec) {
        Json details = Json::object();
        details["positionals"] = args.positionals;
        Json known = Json::array();
        for (const auto& s : commandTable()) { std::string line; for (const auto& p : s.cli) line += (line.empty() ? "" : " ") + p; known.push_back(line); }
        details["knownCommands"] = known;
        Envelope env = Envelope::failure(commandId, CommandError::make(ErrorCategory::Usage, "UNKNOWN_COMMAND",
            "Unknown command. Run `akeir capabilities --json` to list commands.", details));
        printEnvelope(env, mode);
        return env.exitCode();
    }

    if (auto t = args.get("timeout")) {
        auto ms = parseDurationMs(*t);
        if (!ms) {
            Envelope env = Envelope::failure(commandId, CommandError::make(ErrorCategory::Usage, "USAGE_ERROR",
                "Invalid --timeout. Use e.g. 30s, 2m, 1500ms.", Json{{"value", *t}}));
            printEnvelope(env, mode);
            return env.exitCode();
        }
        startWatchdog(std::chrono::milliseconds(*ms), commandId);
    }

    Stopwatch sw;
    Context ctx{args, projectDir};
    if (spec->id == "serve") return runServe(ctx);   // 데몬: stdout 이 RPC 채널 (§88.1)
    if (spec->id == "mcp") return runMcp(ctx);       // MCP 서버: stdout 이 MCP 채널 (§46)

    // 데몬이 떠 있으면 얇은 클라이언트가 된다 (§88.1). --local 이면 무시. serve status/stop 은 스스로 연결한다.
    if (!args.has("local") && spec->id != "serve.status" && spec->id != "serve.stop" && spec->id != "capabilities" && spec->id != "project.version" && spec->id != "project.init") {
        Json remoteEnv; int code = 0;
        if (tryRemote(args, projectDir, spec->id, remoteEnv, code)) {
            if (remoteEnv.is_object()) { remoteEnv["meta"]["durationMs"] = sw.elapsedMs(); remoteEnv["meta"]["via"] = "serve"; }
            bool json = (mode == OutputMode::Json || mode == OutputMode::NdJson) || (mode == OutputMode::Auto && !stdoutIsTty());
            std::fputs((json ? remoteEnv.dump() : remoteEnv.dump(2)).c_str(), stdout); std::fputc('\n', stdout); std::fflush(stdout);
            return code;
        }
    }

    Envelope env;
    try {
        env = spec->run(ctx);
    } catch (const std::exception& e) {
        // 로직 오류는 크래시가 아니라 INTERNAL_ERROR envelope (exit 1). 진짜 크래시(SEH)는 Crash.cpp 가 잡는다 (§88.4)
        env = Envelope::failure(commandId, CommandError::make(ErrorCategory::Internal, "INTERNAL_ERROR",
            std::string("Unhandled C++ exception: ") + e.what() + ". This is an engine bug; please report with the command line.", Json{{"what", e.what()}}));
    }
    stopWatchdog();
    env.withMeta("durationMs", sw.elapsedMs());
    printEnvelope(env, mode);
    Logger::global().flush();
    return env.exitCode();
}
