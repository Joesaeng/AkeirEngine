// Tools/CLI/Serve.h — `akeir serve` 데몬 + 얇은 클라이언트 + ServeHost. 설계 문서 §88.1 (상주 프로세스, 단일 writer, `akeir <cmd>` 는 데몬이 있으면 RPC), §9.1 (tx = opaque handle + TTL),
// §46.2 (loopback + per-session token), §13 (exit code 는 envelope 에서).
//
//   전송 (ADR-0029): 127.0.0.1:<port> TCP, **NDJSON JSON-RPC 2.0** (요청 한 줄 ↔ 응답 한 줄). HTTP 가 아니다 — 외부 라이브러리 없이 Winsock 만 쓴다.
//     요청:  {"jsonrpc":"2.0","id":1,"method":"<command id>","params":{"argv":[...]} | {"args":{...},"tx":"tx_…","actor":"…","dryRun":bool},"token":"<session token>"}
//     응답:  {"jsonrpc":"2.0","id":1,"result":{"envelope":{…§12…},"exitCode":N}}   또는   {"jsonrpc":"2.0","id":1,"error":{"code":-32601,"message":"…"}}
//     method = CLI command id (`entity.create`, `validate`, `run.start` …; params.argv = CLI 인자 그대로)  또는  bus command id (params.args = 구조화 인자, §8)
//     서버 전용 method: serve.status, serve.stop, project.reload.   tx 는 CLI 명령 `tx begin|commit|rollback|list` 로 (argv 경로).
//   발견: <project>/Cache/serve.json = {pid, port, token, startedAt, projectDir}. 클라이언트는 이 파일이 있고 연결되면 RPC, 아니면 파일을 지우고 one-shot.
//   `--local` 을 주면 데몬을 무시하고 in-process 로 실행한다 (데몬이 잠근 파일을 두 프로세스가 쓰면 BASE_MISMATCH 로 드러난다).
//   `--stdio` 면 소켓 대신 stdin/stdout 으로 같은 NDJSON (Editor 임베딩용; token 검사 없음). 이때 envelope 출력은 stdout 이 아니라 RPC 응답이다.
//   `akeir mcp` (Mcp.cpp) 는 같은 ServeHost 위에 MCP 메서드(initialize/tools/list/tools/call)를 올린다.
#pragma once

#include "Commands.h"
#include "pme/commands/CommandBus.h"
#include "pme/core/Json.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pme::cli {

struct ServeInfo {
    int pid = 0;
    int port = 0;
    std::string token;
    std::string startedAt;
    std::string projectDir;
    Json toJson() const;
    static std::optional<ServeInfo> load(const std::string& projectDir);
    static std::string path(const std::string& projectDir);
};

/// 상주 상태 + JSON-RPC 디스패치 (serve 와 mcp 가 공유). 전송은 밖에서.
class ServeHost {
public:
    /// 프로젝트 로드 + bus + journal 복구. 실패 시 envelope 을 채우고 false. 성공해도 로드 진단은 fail.warnings 에 실린다.
    bool init(const std::string& projectDir, const std::string& actor, Envelope& fail);
    /// JSON-RPC 요청 한 개 → 응답 한 개 (머리말 형식). token 은 checkToken 이 true 일 때만 검사.
    Json dispatch(const Json& request, bool checkToken);
    ServeInfo& info() { return info_; }
    bool stopRequested() const { return stop_; }
    long long requests() const { return requests_; }
    std::size_t documents() const { return project_ ? project_->documents().size() : 0; }
    std::size_t journalRecovered() const { return recovered_; }
    /// 편의: CLI 철자(argv)로 한 명령 실행 → envelope JSON (MCP tools/call 이 쓴다)
    Json callArgv(const std::vector<std::string>& argv, const Json& extraParams = Json::object());
    Json callBus(const std::string& commandId, const Json& args, const Json& extraParams = Json::object());

private:
    std::string projectDir_;
    std::optional<Project> project_;
    std::unique_ptr<CommandBus> bus_;
    Json runRegistry_ = Json::object();
    ServeInfo info_;
    bool stop_ = false;
    long long requests_ = 0;
    std::size_t recovered_ = 0;
};

/// 데몬 본체. 돌아올 때 exit code 를 돌려준다.
int runServe(Context& ctx);

/// 데몬이 떠 있으면 명령을 RPC 로 보내고 true + (envelopeJson, exitCode). 없거나 연결 실패면 false (호출자가 in-process 로 진행).
bool tryRemote(const Args& args, const std::string& projectDir, const std::string& commandId, Json& envelopeOut, int& exitCodeOut);

void registerServeCommands(std::vector<CommandSpec>& table);

/// Mcp.cpp — `akeir mcp` (stdio MCP 서버, ServeHost 위에)
int runMcp(Context& ctx);
void registerMcpCommands(std::vector<CommandSpec>& table);

} // namespace pme::cli
