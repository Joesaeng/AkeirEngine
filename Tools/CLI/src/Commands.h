// Tools/CLI/Commands.h — CLI 명령 테이블. 설계 문서 §8 (command id), §11, §15 (capabilities).
//
// 각 CLI 명령은 command id (예 "entity.create") 와 CLI 철자 ("entity create") 를 가진다.
// Phase 0–1 에서는 CLI 가 in-process 로 Command 를 직접 실행한다 (§88.1 one-shot 경로). Phase 4 에서 `akeir serve` RPC 클라이언트가 된다.
#pragma once

#include "Args.h"
#include "akeir/core/Envelope.h"
#include "akeir/commands/CommandBus.h"
#include "akeir/runtime/Project.h"
#include "akeir/testing/TestRunner.h"

#include <optional>

#include <functional>
#include <string>
#include <vector>

namespace akeir::cli {

struct Context {
    Args args;
    std::string projectDir;   // --project 또는 cwd 에서 project.json 을 찾은 디렉터리 ("" = 없음)
    std::vector<akeir::Diagnostic> loadDiagnostics;   // 프로젝트 로드 중 나온 진단 (envelope warnings 로 복사)
    // `akeir serve` 안에서 실행될 때만 채워진다 (§88.1 단일 writer): 명령은 디스크 대신 이 상주 상태를 쓴다
    Project* resident = nullptr;
    CommandBus* residentBus = nullptr;
    Json* runRegistry = nullptr;   // run_id → 마지막 run 결과 (run.status 용)
    bool inServe() const { return residentBus != nullptr; }
};

struct CommandSpec {
    std::string id;                 // "entity.create"
    std::vector<std::string> cli;   // {"entity", "create"}
    std::string kind;               // Mutation | Query | RuntimeControl | Meta
    std::string title;
    std::string description;
    std::string usage;              // "akeir entity create <name> [--world ID]"
    bool readOnly = false;
    bool destructive = false;
    bool idempotent = false;
    std::function<Envelope(Context&)> run;
};

const std::vector<CommandSpec>& commandTable();
/// ProjectCommands.cpp — project info / validate / fmt / schema / entity list / explain
void registerProjectCommands(std::vector<CommandSpec>& table);
/// RunCommands.cpp — run / dump / query (world 를 build 해서 실행)
void registerRunCommands(std::vector<CommandSpec>& table);
/// MutationCommands.cpp — CommandBus 경유 쓰기 명령: entity create/delete/rename/reparent, component add/remove, set, tag, prefab, world, apply, undo/redo/history, cmd
void registerMutationCommands(std::vector<CommandSpec>& table);
/// InitCommand.cpp — `akeir project init` (빈 프로젝트)
void registerInitCommands(std::vector<CommandSpec>& table);
/// TestCommands.cpp — `akeir test` (§23/§24)
void registerTestCommands(std::vector<CommandSpec>& table);
/// SdlCommands.cpp — capture / input map / 창 모드 run (§20, §27). SDL 없는 빌드에서는 FEATURE_UNAVAILABLE
void registerSdlCommands(std::vector<CommandSpec>& table);
bool sdlAvailable();
Envelope runWindowed(Context& ctx);
void installCaptureHooks(TestRunnerOptions& opts);


/// 공용: 프로젝트 열기 (실패 시 fail 에 PROJECT_NOT_FOUND envelope). ProjectCommands.cpp 에 정의.
std::optional<Project> openProject(Context& ctx, Envelope& fail, const std::string& command);
Json diagArray(const std::vector<Diagnostic>& v);
const CommandSpec* findCommand(const std::vector<std::string>& positionals, std::size_t& consumed);

/// §15: tools[] / commands[] / exitCodes / errorCodes
Json capabilitiesJson();
/// §12 envelope JSON Schema (자기완결)
Json envelopeSchema();

} // namespace akeir::cli
