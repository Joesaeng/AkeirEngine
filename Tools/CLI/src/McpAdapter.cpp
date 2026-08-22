// Tools/CLI/McpAdapter.cpp — `akeir mcp` front process (ADR-0034: resident tool vs. rebuild).
//
// Claude Code keeps `akeir mcp` alive for a whole session, which on Windows locks akeir.exe and made every
// Game/Source rebuild fail with LNK1168. The build now moves a running akeir.exe aside (cmake/UnlockExe.cmake);
// this adapter makes sure the *new* build is what actually runs afterwards:
//
//   Claude Code ──stdio──▶ akeir mcp (adapter, this file)  ──pipes──▶ akeir.exe mcp --worker (Mcp.cpp: ServeHost + MCP methods)
//
//   · The adapter relays newline-delimited JSON-RPC verbatim in both directions and tracks in-flight request ids.
//   · Before forwarding a request while nothing is in flight, it compares the launch path's akeir.exe (size/mtime,
//     then sha256) with the worker it spawned. Different → the old worker is closed (stdin EOF), a new one is
//     spawned from the new file, `initialize` + `notifications/initialized` are replayed silently, and the next
//     tools/call response carries a MCP_WORKER_RESTARTED note (open transactions of the old worker are gone).
//   · If the worker dies (crash), pending requests get a JSON-RPC error and the next request respawns it.
//   · The adapter process itself keeps running from the (possibly renamed) file it was started from; it holds no
//     engine state, so it never needs the new code.
//
// `akeir mcp --worker` runs the in-process server directly (Mcp.cpp); `--print-config` prints a .mcp.json.
#include "ExeInfo.h"
#include "Serve.h"
#include "akeir/core/Diagnostic.h"
#include "akeir/core/ExitCodes.h"
#include "akeir/core/Log.h"
#include "akeir/serialization/Canonical.h"

#include <atomic>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <fcntl.h>
#  include <io.h>
#endif

namespace akeir::cli {

int runMcpWorker(Context& ctx);   // Mcp.cpp

namespace {

std::string stripFraming(std::string line) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB && static_cast<unsigned char>(line[2]) == 0xBF) line.erase(0, 3);
    return line;
}

#ifdef _WIN32

class Adapter {
public:
    Adapter(std::string exePath, std::string projectDir, std::string actor)
        : exePath_(std::move(exePath)), projectDir_(std::move(projectDir)), actor_(std::move(actor)) {}
    ~Adapter() { stopWorker(true); }

    int run() {
        _setmode(_fileno(stdout), _O_BINARY);
        _setmode(_fileno(stdin), _O_BINARY);
        if (!spawnWorker()) AKEIR_LOG(Error, "mcp", "worker_spawn_failed", "akeir mcp: could not start the worker process; requests will be answered with errors until it can be started", Json{{"exe", exePath_}});
        std::string raw;
        while (std::getline(std::cin, raw)) {
            std::string line = stripFraming(std::move(raw));
            if (line.empty()) continue;
            auto reqOpt = parseJson(line);
            if (!reqOpt || !reqOpt->is_object()) { emit(rpcError(Json(), -32700, "Parse error")); continue; }
            const Json& req = *reqOpt;
            const std::string method = req.value("method", "");
            const bool isRequest = req.contains("id");
            if (method == "initialize") initLine_ = line;
            else if (method == "notifications/initialized") initializedLine_ = line;

            if (isRequest) {
                const std::string key = req["id"].dump();
                bool idle;
                { std::lock_guard<std::mutex> lock(stateMtx_); idle = pending_.empty(); }
                if (idle && worker_.alive.load() && exeChanged()) restartWorker("akeir.exe changed on disk (rebuilt)");
                if (!worker_.alive.load() && !spawnWorker()) {
                    emit(rpcError(req["id"], -32000, "akeir mcp worker is not running and could not be started from " + exePath_ + " (rebuild in progress? see stderr)"));
                    continue;
                }
                {
                    std::lock_guard<std::mutex> lock(stateMtx_);
                    pending_.insert(key);
                    if (method == "tools/call") toolCallIds_.insert(key);
                }
                if (!writeToWorker(line)) {
                    std::lock_guard<std::mutex> lock(stateMtx_);
                    pending_.erase(key); toolCallIds_.erase(key);
                    emit(rpcError(req["id"], -32000, "akeir mcp worker pipe closed"));
                }
            } else {
                if (worker_.alive.load()) writeToWorker(line);
            }
        }
        stopWorker(true);
        AKEIR_LOG(Info, "mcp", "adapter_stopped", "akeir mcp adapter stopped", Json{{"restarts", restarts_}});
        return kExitOk;
    }

private:
    struct Worker {
        HANDLE process = nullptr;
        HANDLE stdinWrite = nullptr;
        HANDLE stdoutRead = nullptr;
        DWORD pid = 0;
        ExeStamp stamp;
        std::string sha;
        std::thread pump;
        std::atomic<bool> alive{false};
    };

    static Json rpcError(const Json& id, int code, const std::string& msg) {
        return Json{{"jsonrpc", "2.0"}, {"id", id}, {"error", Json{{"code", code}, {"message", msg}}}};
    }

    void emit(const Json& j) { emitLine(j.dump()); }
    void emitLine(const std::string& s) {
        std::lock_guard<std::mutex> lock(outMtx_);
        std::fwrite(s.data(), 1, s.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }

    bool writeToWorker(const std::string& line) {
        std::string s = line + "\n";
        DWORD written = 0;
        return WriteFile(worker_.stdinWrite, s.data(), static_cast<DWORD>(s.size()), &written, nullptr) && written == s.size();
    }

    bool exeChanged() {
        ExeStamp now = exeStamp(exePath_);
        if (!now.exists) return false;                // mid-build: the old file was moved aside and the new one is not there yet — try again next request
        if (now == worker_.stamp) return false;
        std::string sha = fileSha256(exePath_);
        if (sha.empty()) return false;
        if (sha == worker_.sha) { worker_.stamp = now; return false; }   // relinked but byte-identical
        return true;
    }

    static std::wstring widen(const std::string& s) {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
        std::wstring w(static_cast<std::size_t>(n), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
        return w;
    }
    static std::wstring quote(const std::wstring& s) { return L"\"" + s + L"\""; }

    bool spawnWorker() {
        stopWorker(false);
        ExeStamp st = exeStamp(exePath_);
        if (!st.exists) return false;
        SECURITY_ATTRIBUTES sa{}; sa.nLength = sizeof sa; sa.bInheritHandle = TRUE;
        HANDLE inR = nullptr, inW = nullptr, outR = nullptr, outW = nullptr;
        if (!CreatePipe(&inR, &inW, &sa, 0)) return false;
        if (!CreatePipe(&outR, &outW, &sa, 0)) { CloseHandle(inR); CloseHandle(inW); return false; }
        SetHandleInformation(inW, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
        HANDLE err = GetStdHandle(STD_ERROR_HANDLE), errDup = nullptr;
        if (err && err != INVALID_HANDLE_VALUE) DuplicateHandle(GetCurrentProcess(), err, GetCurrentProcess(), &errDup, 0, TRUE, DUPLICATE_SAME_ACCESS);

        // inherit exactly the three std handles — not our own stdio pipes from the MCP client
        HANDLE inherit[3] = {inR, outW, errDup ? errDup : nullptr};
        DWORD inheritCount = errDup ? 3 : 2;
        SIZE_T attrSize = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
        std::string attrBuf(attrSize, '\0');
        auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
        bool attrsOk = InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize) &&
                       UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit, inheritCount * sizeof(HANDLE), nullptr, nullptr);

        STARTUPINFOEXW si{}; si.StartupInfo.cb = sizeof si;
        si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        si.StartupInfo.hStdInput = inR; si.StartupInfo.hStdOutput = outW; si.StartupInfo.hStdError = errDup ? errDup : err;
        si.lpAttributeList = attrsOk ? attrs : nullptr;
        std::wstring exeW = widen(exePath_);
        std::wstring cmd = quote(exeW) + L" mcp --worker --project " + quote(widen(projectDir_));
        if (!actor_.empty()) cmd += L" --actor " + quote(widen(actor_));
        PROCESS_INFORMATION pi{};
        BOOL ok = CreateProcessW(exeW.c_str(), cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | (attrsOk ? EXTENDED_STARTUPINFO_PRESENT : 0), nullptr, nullptr, &si.StartupInfo, &pi);
        DWORD lastErr = ok ? 0 : GetLastError();
        if (attrsOk) DeleteProcThreadAttributeList(attrs);
        CloseHandle(inR); CloseHandle(outW); if (errDup) CloseHandle(errDup);
        if (!ok) {
            CloseHandle(inW); CloseHandle(outR);
            AKEIR_LOG(Error, "mcp", "worker_spawn_failed", "CreateProcess failed", Json{{"exe", exePath_}, {"error", static_cast<long long>(lastErr)}});
            return false;
        }
        CloseHandle(pi.hThread);
        worker_.process = pi.hProcess; worker_.pid = pi.dwProcessId;
        worker_.stdinWrite = inW; worker_.stdoutRead = outR;
        worker_.stamp = st; worker_.sha = fileSha256(exePath_);
        worker_.alive = true;
        worker_.pump = std::thread([this] { pumpLoop(); });
        AKEIR_LOG(Info, "mcp", "worker_started", "akeir mcp worker started", Json{{"pid", static_cast<long long>(worker_.pid)}, {"exe", exePath_}, {"sha256", worker_.sha}});
        if (!initLine_.empty()) replayInit();
        return true;
    }

    /// The MCP handshake already happened with the client; a fresh worker needs it too. Its response is swallowed.
    void replayInit() {
        auto j = parseJson(initLine_);
        if (!j || !j->is_object()) return;
        (*j)["id"] = "akeir-adapter-replay";
        { std::lock_guard<std::mutex> lock(stateMtx_); silentIds_.insert((*j)["id"].dump()); }
        writeToWorker(j->dump());
        if (!initializedLine_.empty()) writeToWorker(initializedLine_);
    }

    void stopWorker(bool wait) {
        if (!worker_.process) return;
        if (worker_.stdinWrite) { CloseHandle(worker_.stdinWrite); worker_.stdinWrite = nullptr; }   // EOF → the worker's loop ends
        if (wait) {
            if (WaitForSingleObject(worker_.process, 3000) != WAIT_OBJECT_0) TerminateProcess(worker_.process, 1);
        } else if (WaitForSingleObject(worker_.process, 1500) != WAIT_OBJECT_0) {
            TerminateProcess(worker_.process, 1);
        }
        if (worker_.pump.joinable()) worker_.pump.join();   // ends when the pipe breaks
        if (worker_.stdoutRead) { CloseHandle(worker_.stdoutRead); worker_.stdoutRead = nullptr; }
        CloseHandle(worker_.process); worker_.process = nullptr; worker_.pid = 0;
        worker_.alive = false;
    }

    void restartWorker(const std::string& why) {
        std::string oldSha = worker_.sha;
        stopWorker(false);
        ++restarts_;
        if (spawnWorker()) {
            { std::lock_guard<std::mutex> lock(stateMtx_); noticePending_ = true; }
            AKEIR_LOG(Info, "mcp", "worker_restarted", "akeir mcp worker restarted with the rebuilt akeir.exe", Json{{"reason", why}, {"old", oldSha}, {"new", worker_.sha}, {"restarts", restarts_}});
        }
    }

    void pumpLoop() {
        std::string buf;
        char chunk[65536];
        DWORD n = 0;
        HANDLE h = worker_.stdoutRead;
        while (ReadFile(h, chunk, sizeof chunk, &n, nullptr) && n > 0) {
            buf.append(chunk, n);
            std::size_t pos;
            while ((pos = buf.find('\n')) != std::string::npos) {
                std::string line = buf.substr(0, pos);
                buf.erase(0, pos + 1);
                onWorkerLine(stripFraming(std::move(line)));
            }
        }
        if (!buf.empty()) onWorkerLine(stripFraming(std::move(buf)));
        worker_.alive = false;
        // whoever was waiting for an answer gets an error instead of silence
        std::vector<std::string> lost;
        { std::lock_guard<std::mutex> lock(stateMtx_); lost.assign(pending_.begin(), pending_.end()); pending_.clear(); toolCallIds_.clear(); }
        for (const auto& key : lost) {
            auto id = parseJson(key);
            emit(rpcError(id ? *id : Json(), -32000, "akeir mcp worker exited unexpectedly (crash?) — see stderr logs and <project>/Cache/crash; the next request starts a new worker"));
        }
        if (!lost.empty()) AKEIR_LOG(Error, "mcp", "worker_lost", "akeir mcp worker exited with requests in flight", Json{{"lost", static_cast<long long>(lost.size())}});
    }

    void onWorkerLine(std::string line) {
        if (line.empty()) return;
        auto j = parseJson(line);
        if (j && j->is_object() && j->contains("id") && (j->contains("result") || j->contains("error"))) {
            const std::string key = (*j)["id"].dump();
            bool silent = false, isToolCall = false, notice = false;
            {
                std::lock_guard<std::mutex> lock(stateMtx_);
                if (silentIds_.erase(key)) silent = true;
                pending_.erase(key);
                isToolCall = toolCallIds_.erase(key) > 0;
                if (isToolCall && noticePending_) { notice = true; noticePending_ = false; }
            }
            if (silent) return;
            if (notice && j->contains("result") && (*j)["result"].is_object() && (*j)["result"].contains("structuredContent") && (*j)["result"]["structuredContent"].is_object()) {
                Json& env = (*j)["result"]["structuredContent"];
                Diagnostic note = Diagnostic::note("MCP_WORKER_RESTARTED", "akeir.exe was rebuilt; the MCP worker restarted with the new build (" + worker_.sha + "). Transactions opened before the restart were discarded; run handles from the old worker are gone.");
                if (!env.contains("warnings") || !env["warnings"].is_array()) env["warnings"] = Json::array();
                env["warnings"].push_back(note.toJson());
                (*j)["result"]["content"] = Json::array({Json{{"type", "text"}, {"text", env.dump()}}});
                emit(*j);
                return;
            }
        }
        emitLine(line);
    }

    std::string exePath_, projectDir_, actor_;
    std::string initLine_, initializedLine_;
    Worker worker_;
    std::mutex outMtx_, stateMtx_;
    std::set<std::string> pending_, toolCallIds_, silentIds_;
    bool noticePending_ = false;
    long long restarts_ = 0;
};

#endif // _WIN32

} // namespace

int runMcp(Context& ctx) {
    if (ctx.args.has("print-config")) {
        // 절대 경로 .mcp.json 을 stdout 으로 — 상대 경로를 못 푸는 클라이언트용
        Json cfg = Json{{"mcpServers", Json{{"akeir", Json{{"command", ownExePath()}, {"args", Json::array({"mcp", "--project", ctx.projectDir})}}}}}};
        std::fputs(cfg.dump(2).c_str(), stdout); std::fputc('\n', stdout);
        return kExitOk;
    }
    if (ctx.args.has("worker")) return runMcpWorker(ctx);
#ifdef _WIN32
    if (ctx.projectDir.empty()) {
        std::fputs(Envelope::failure("mcp", CommandError::make(ErrorCategory::NotFound, "PROJECT_NOT_FOUND", "akeir mcp needs a project: pass --project <dir> or run inside one.")).toJson().dump().c_str(), stderr);
        std::fputc('\n', stderr);
        return kExitNotFound;
    }
    Adapter adapter(ownExePath(), ctx.projectDir, ctx.args.getOr("actor", ""));
    return adapter.run();
#else
    return runMcpWorker(ctx);   // no image-file lock on this platform; in-process server is enough
#endif
}

} // namespace akeir::cli
