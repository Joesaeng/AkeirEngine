// akeir/core/Crash.cpp — 설계 문서 §88.4
#include "akeir/core/Crash.h"
#include "akeir/core/Envelope.h"
#include "akeir/core/ExitCodes.h"
#include "akeir/core/Time.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <thread>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <dbghelp.h>
#endif

namespace akeir {

namespace {

CrashConfig g_cfg;
std::atomic<bool> g_installed{false};

// watchdog
std::mutex g_wdMtx;
std::condition_variable g_wdCv;
std::atomic<bool> g_wdActive{false};
std::thread g_wdThread;
std::string g_wdCommand;

Json lastLogsJson() {
    Json arr = Json::array();
    if (g_cfg.lastLogs) for (const auto& r : g_cfg.lastLogs->snapshot()) arr.push_back(r.toJson());
    return arr;
}

void emitEnvelopeAndFlush(const Json& env) {
    std::string s = env.dump();
    std::fputs(s.c_str(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
    Logger::global().flush();
}

std::string nowStamp() {
    auto iso = WallTime::now().iso8601(); // 2026-08-21T10:00:00.123Z
    for (auto& c : iso) if (c == ':' || c == '.') c = '-';
    return iso;
}

#ifdef _WIN32
LONG WINAPI unhandledFilter(EXCEPTION_POINTERS* info) {
    std::string dumpPath;
    try {
        std::filesystem::create_directories(g_cfg.dumpDir);
        dumpPath = (std::filesystem::path(g_cfg.dumpDir) / (g_cfg.stem + "-" + nowStamp() + ".dmp")).string();
        HANDLE file = CreateFileA(dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei{};
            mei.ThreadId = GetCurrentThreadId();
            mei.ExceptionPointers = info;
            mei.ClientPointers = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                              static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
                              &mei, nullptr, nullptr);
            CloseHandle(file);
        } else {
            dumpPath.clear();
        }
    } catch (...) { dumpPath.clear(); }

    char code[32];
    std::snprintf(code, sizeof code, "0x%08lx", static_cast<unsigned long>(info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0));
    Json details = Json::object();
    details["exceptionCode"] = code;
    details["minidump"] = dumpPath.empty() ? Json(nullptr) : Json(dumpPath);
    details["lastLogs"] = lastLogsJson();
    emitEnvelopeAndFlush(makeCrashEnvelope(g_cfg.command, "CRASH", "crash",
        "The process crashed with an unhandled exception. Open details.minidump in a debugger; details.lastLogs holds the last log records.", details));
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(kExitCrash));
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

} // namespace

Json makeCrashEnvelope(const std::string& command, const std::string& ruleId, const std::string& category,
                       const std::string& text, Json details) {
    ErrorCategory cat = (category == "timeout") ? ErrorCategory::Timeout : ErrorCategory::Crash;
    Envelope env = Envelope::failure(command, CommandError::make(cat, ruleId, text, std::move(details)));
    return env.toJson();
}

void installCrashHandler(const CrashConfig& cfg) {
    g_cfg = cfg;
    if (g_installed.exchange(true)) return;
#ifdef _WIN32
    SetUnhandledExceptionFilter(unhandledFilter);
    // abort()/terminate 도 dialog 없이 핸들러로 보낸다 (headless CI)
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
#endif
}

void startWatchdog(std::chrono::milliseconds timeout, const std::string& command) {
    stopWatchdog();
    g_wdCommand = command;
    g_wdActive = true;
    g_wdThread = std::thread([timeout] {
        std::unique_lock<std::mutex> lock(g_wdMtx);
        if (g_wdCv.wait_for(lock, timeout, [] { return !g_wdActive.load(); })) return; // 정상 종료
        Json details = Json::object();
        details["timeoutMs"] = timeout.count();
        details["lastLogs"] = lastLogsJson();
        emitEnvelopeAndFlush(makeCrashEnvelope(g_wdCommand, "TIMEOUT", "timeout",
            "The command exceeded its watchdog timeout and was terminated. Increase --timeout or check for an infinite loop.", details));
#ifdef _WIN32
        TerminateProcess(GetCurrentProcess(), static_cast<UINT>(kExitTimeout));
#else
        std::_Exit(kExitTimeout);
#endif
    });
}

void stopWatchdog() {
    if (!g_wdActive.exchange(false)) { if (g_wdThread.joinable()) g_wdThread.join(); return; }
    { std::lock_guard<std::mutex> lock(g_wdMtx); }
    g_wdCv.notify_all();
    if (g_wdThread.joinable()) g_wdThread.join();
}

[[noreturn]] void debugForceCrash() {
    volatile int* p = nullptr;
    *p = 42; // access violation
    std::abort();
}

} // namespace akeir
